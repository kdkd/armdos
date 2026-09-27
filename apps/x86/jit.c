/*
 * jit.c - the x86 -> ARM dynamic binary translator ("X86 /JIT").
 *
 * Hot x86 code (16-bit; the common 8086/186 instructions) is translated into
 * ARM machine code in a code cache and run natively by the ARM926 - whose
 * emulator has a JIT of its own, so this is a double JIT: x86 -> ARM -> JS.
 * Anything the translator does not handle ends the block and the interpreter
 * executes it, so correctness never depends on coverage.
 *
 * Blocks are superblocks: an unconditional JMP is followed (up to three
 * times) and a conditional jump is a side exit, so a loop body becomes one
 * block whose back edge branches to its own entry.  Exits to static targets
 * are chained: once the target is translated, the exit is patched into a
 * direct ARM branch.
 *
 * Registers (jitasm.S): r11 = &cpu, r10 = rpt, r9 = wpt, r8 = &irq_pending;
 * r4/r5/r6 = ALU operands a, b and the result, r7 = saved ARM flags; r0-r3,
 * r12, lr scratch.  The x86 registers live in the cpu struct between
 * instructions.  16-bit arithmetic is done on values shifted into the top
 * half of an ARM register, so the ARM N/Z/C/V flags ARE the x86 SF/ZF/CF (or
 * !CF)/OF: a conditional jump after its CMP/ADD/SUB/TEST is one ARM branch.
 * Each flag-setting instruction also records the interpreter's lazy-flag
 * state (op, a, b, result - one STM), so flags are right at every exit.
 *
 * Self-modifying code: pages holding translated bytes are tagged in the
 * write page table; a store that hits translated bytes invalidates those
 * blocks and ends the current one after the store.
 *
 * Copyright (c) 2026 Europa Micro Systems. Part of ARM-DOS (apps/x86).
 */
#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include "x86.h"
#include "jit.h"

int jit_enabled;
int jit_dump;

/* helpers in cpu.c */
uint32_t jh_shift(uint32_t op, uint32_t v, uint32_t cnt, uint32_t sz);
int jh_string(uint32_t op, uint32_t sz, int ovr, uint32_t rep);
int jh_cond(uint32_t cc);
void jh_fix_cf(void);
int jh_div(uint32_t sgn, uint32_t d, uint32_t sz);
void jh_mul(uint32_t sgn, uint32_t v, uint32_t sz);
uint32_t jh_imul3(int32_t a, int32_t b);
void jh_flagop(uint32_t op);
void jh_pushf(void);
uint32_t jh_imul2(int32_t a, int32_t b, uint32_t sz);
uint32_t jh_shxd(uint32_t right_sz, uint32_t dst, uint32_t src, uint32_t cnt);
void jh_pusha(uint32_t o32);
void jh_popa(uint32_t o32);
void jh_farlog(uint32_t k, uint32_t fcs, uint32_t fip);
extern int farlog_on;
static uint32_t blk_cs;
extern uint32_t jit_enter(void *code);
extern void jit_exit(void);
extern void jit_exit_dyn(void);

/* ------------------------------------------------------------ cache */

#define CODE_WORDS  (2048 * 1024 / 4)
#define BLK_WORDS   (1280 * 1024 / 4)         /* blocks; then the stub area */
#define MAXBLK      6144
#define MAPSIZE     8192
#define MAXLINK     12288
#define HOT         2
#define MAXSEG      4
#define MAXINS      48

struct link { uint32_t *slot, *stub; struct link *next; };
struct jblk {
    uint32_t lin, cs;
    uint32_t seg[MAXSEG][2];              /* x86 byte ranges [start, end) the block was made from */
    uint32_t *code;
    uint32_t *end;                        /* end of the block's ARM code (its literal pool included) */
    struct link *in;
    struct jblk *hnext;                   /* the next block in the same map bucket */
    uint8_t nseg, dead;
};

static uint32_t *cache, *cend, *cp;          /* the block area */
static uint32_t *stubs, *send, *sp_;         /* the stub area (other 4 KB pages, see translate()) */
static struct jblk *blks;
static int nblk;
static struct jblk **map;
static struct link *links;
static int nlink;
static uint8_t *codebits;              /* 1 bit per x86 byte that is translated */
static int8_t hotc[4096];             /* visits before translating; negative = a failed attempt cooling off */
/* self-modifying code: how often code in each 256-byte page was rewritten by the
   program, and until when (cpu.icount) nothing starting in that page is translated.
   Code that keeps rewriting itself (Second Reality's per-frame generated inner
   loops) costs a translation - and the ARM emulator's JIT a recompile - each time,
   and code run once per rewrite never gets hot enough for the ARM JIT anyway. It is
   interpreted instead; the page is tried again after a pause that doubles with each
   rewrite (256K x86 instructions, then 512K, ... up to 8M); loading a file over
   the page resets it. Per page, not per
   address: the interpreter asks for a block at every instruction, so a cooled
   address would only move the next translation one instruction on. */
static uint8_t smcp[4096];
static uint32_t cool_until[4096];
static void smc_killed(const struct jblk *b)
{
    for (int s = 0; s < b->nseg; s++)
        for (uint32_t pg = b->seg[s][0] >> 8; pg <= (b->seg[s][1] - 1) >> 8; pg++) {
            uint32_t k = pg & 4095;
            if (smcp[k] < 6) smcp[k]++;
            cool_until[k] = cpu.icount + (0x40000u << (smcp[k] - 1));
        }
}
static uint32_t nstat_blocks, nstat_flush, nstat_inval;
static volatile int smc_hit;

static inline uint32_t mkey(uint32_t lin, uint32_t cs) { return (lin ^ (cs << 3) ^ (lin >> 13)) & (MAPSIZE - 1); }

/* map: hash buckets of chained blocks (a direct-mapped table lost blocks
   to collisions in big programs such as MASM 5.1, which were then
   translated again and again) */
static struct jblk *lookup(uint32_t lin, uint32_t cs)
{
    for (struct jblk *b = map[mkey(lin, cs)]; b; b = b->hnext)
        if (b->lin == lin && b->cs == cs) return b;
    return 0;
}

static void flush_all(void)
{
    for (int i = 0; i < nblk; i++) {
        struct jblk *b = &blks[i];
        for (int s = 0; s < b->nseg; s++)
            for (uint32_t l = b->seg[s][0] & ~0xFFu; l < b->seg[s][1]; l += 256) wpt[l >> 8] &= ~(uintptr_t)1;
    }
    memset(map, 0, MAPSIZE * sizeof *map);
    memset(codebits, 0, X86_MEMSIZE / 8);
    nblk = 0;
    nlink = 0;
    cp = cache;
    sp_ = stubs;
    nstat_flush++;
}

int jit_init(void)
{
    uint32_t *raw = malloc(CODE_WORDS * 4 + 4096);
    blks = malloc(MAXBLK * sizeof *blks);
    map = calloc(MAPSIZE, sizeof *map);
    links = malloc(MAXLINK * sizeof *links);
    codebits = calloc(X86_MEMSIZE / 8, 1);
    if (!raw || !blks || !map || !links || !codebits) return -1;
    cache = (uint32_t *)(((uintptr_t)raw + 4095) & ~(uintptr_t)4095);
    { int nz = 0; for (int i = 0; i < MAPSIZE; i++) if (map[i]) nz++; int cb = 0; for (int i = 0; i < (int)(X86_MEMSIZE / 8); i++) if (codebits[i]) cb++;
      if (nz || cb) dbg("[jit_init: calloc gave %d non-null map entries, %d non-zero codebits]\n", nz, cb); }
    cend = cache + BLK_WORDS - 1024;
    cp = cache;
    stubs = cache + BLK_WORDS;                    /* 4 KB aligned: no page holds both */
    send = cache + CODE_WORDS - 1024;
    sp_ = stubs;
    /* one plain store per word: whatever ARM code the emulator may still have
       compiled from an earlier program at these addresses is dropped */
    return 0;
}

/* ------------------------------------------------------------ ARM encoder */

enum { R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12, RSP, RLR, RPC };
#define RPEND R8
#define RWPT  R9
#define RRPT  R10
#define RCPU  R11
enum { EQ, NE, CS, CC, MI, PL, VS, VC, HI, LS, GE, LT, GT, LE, AL, NV };
enum { AND, EOR, SUB, RSB, ADD, ADC, SBC, RSC, TST, TEQ, CMP, CMN, ORR, MOV, BIC, MVN };
enum { LSL, LSR, ASR, ROR };

static uint32_t *p;                    /* emit pointer */
static uint32_t *plimit;               /* end of the area being emitted into */
static int emit_overflow;
static inline void E(uint32_t w) { if (p < plimit) *p++ = w; else emit_overflow = 1; }

static int enc_imm(uint32_t v, uint32_t *out)
{
    for (int rot = 0; rot < 16; rot++) {
        uint32_t r = rot ? (v << (2 * rot)) | (v >> (32 - 2 * rot)) : v;
        if (r <= 0xFF) { *out = (rot << 8) | r; return 1; }
    }
    return 0;
}
static void dpi(int cond, int op, int s, int rd, int rn, uint32_t imm)
{
    uint32_t e;
    if (!enc_imm(imm, &e)) { emit_overflow = 2; return; }
    E((uint32_t)cond << 28 | 1u << 25 | op << 21 | s << 20 | rn << 16 | rd << 12 | e);
}
static void dpr(int cond, int op, int s, int rd, int rn, int rm, int sh, int amt)
{
    E((uint32_t)cond << 28 | op << 21 | s << 20 | rn << 16 | rd << 12 | (amt & 31) << 7 | sh << 5 | rm);
}
#define MOVR(rd, rm)          do { if ((rd) != (rm)) dpr(AL, MOV, 0, rd, 0, rm, LSL, 0); } while (0)
#define LSLI(rd, rm, n)       dpr(AL, MOV, 0, rd, 0, rm, LSL, n)
#define LSRI(rd, rm, n)       dpr(AL, MOV, 0, rd, 0, rm, LSR, n)

static void mov_imm(int rd, uint32_t v)
{
    uint32_t e;
    if (enc_imm(v, &e)) { dpi(AL, MOV, 0, rd, 0, v); return; }
    if (enc_imm(~v, &e)) { dpi(AL, MVN, 0, rd, 0, ~v); return; }
    int first = 1;
    for (int sh = 0; sh < 32; sh += 8) {
        uint32_t part = v & (0xFFu << sh);
        if (!part) continue;
        if (first) { dpi(AL, MOV, 0, rd, 0, part); first = 0; }
        else dpi(AL, ORR, 0, rd, rd, part);
    }
    if (first) dpi(AL, MOV, 0, rd, 0, 0);
}
static void add_imm(int rd, int rn, int32_t v)
{
    uint32_t e;
    if (v == 0) { MOVR(rd, rn); return; }
    if (enc_imm((uint32_t)v, &e)) { dpi(AL, ADD, 0, rd, rn, v); return; }
    if (enc_imm((uint32_t)-v, &e)) { dpi(AL, SUB, 0, rd, rn, -v); return; }
    uint32_t u = v < 0 ? (uint32_t)-v : (uint32_t)v;
    int op = v < 0 ? SUB : ADD, src = rn;
    for (int sh = 0; sh < 32; sh += 8) {
        uint32_t part = u & (0xFFu << sh);
        if (!part) continue;
        dpi(AL, op, 0, rd, src, part);
        src = rd;
    }
}
/* LDR/STR word or byte, immediate offset */
static void ldst(int cond, int load, int byte, int rd, int rn, int off)
{
    int u = off >= 0;
    if (!u) off = -off;
    E((uint32_t)cond << 28 | 0x04000000u | 1u << 24 | u << 23 | byte << 22 | load << 20 | rn << 16 | rd << 12 | (off & 0xFFF));
}
#define LDR(rd, rn, off)   ldst(AL, 1, 0, rd, rn, off)
#define STR(rd, rn, off)   ldst(AL, 0, 0, rd, rn, off)
#define LDRB(rd, rn, off)  ldst(AL, 1, 1, rd, rn, off)
#define STRB(rd, rn, off)  ldst(AL, 0, 1, rd, rn, off)
/* LDR/STR with register offset (rm << sh), optional writeback */
static void ldst_r(int load, int byte, int rd, int rn, int rm, int lsl, int wb)
{
    E(0xE6000000u | 1u << 24 | 1u << 23 | byte << 22 | wb << 21 | load << 20 | rn << 16 | rd << 12 | (lsl & 31) << 7 | rm);
}
/* LDRH/STRH, immediate offset 0..255 */
static void ldsth(int load, int rd, int rn, int off)
{
    E(0xE1C000B0u | load << 20 | rn << 16 | rd << 12 | (off & 0xF0) << 4 | (off & 0xF));
}
#define LDRH(rd, rn, off) ldsth(1, rd, rn, off)
#define STRH(rd, rn, off) ldsth(0, rd, rn, off)
static void stmia(int rn, uint32_t list) { E(0xE8800000u | rn << 16 | list); }
static void branch(int cond, int link, uint32_t *target)
{
    int32_t off = (int32_t)(target - (p + 2));
    E((uint32_t)cond << 28 | 0x0A000000u | link << 24 | ((uint32_t)off & 0xFFFFFF));
}
static void patch_branch(uint32_t *at, uint32_t *target)
{
    int32_t off = (int32_t)(target - (at + 2));
    *at = (*at & 0xFF000000u) | ((uint32_t)off & 0xFFFFFF);
}
static int lfop_known;                 /* the lf_op value known to be in memory, or -1 */
#define BL(fn) do { branch(AL, 1, (uint32_t *)(void *)(fn)); lfop_known = -1; } while (0)
#define BLKEEP(fn) branch(AL, 1, (uint32_t *)(void *)(fn))     /* a helper that leaves lf_op alone */
static void mrs(int rd) { E(0xE10F0000u | rd << 12); }
static void msr_f(int rm) { E(0xE128F000u | rm); }
#define PUSH_R1() do { E(0xE92D0006u); } while (0)        /* push {r1, r2}: keeps sp 8-aligned */
#define POP_R1()  do { E(0xE8BD0006u); } while (0)

/* x86 state offsets */
#define O_R(i)    ((int)offsetof(X86, r) + (i) * 4)
#define O_R8(i)   ((int)offsetof(X86, r) + (((i) & 3) << 2) + ((i) >> 2))
#define O_EIP     ((int)offsetof(X86, eip))
#define O_FLAGS   ((int)offsetof(X86, flags))
#define O_LFOP    ((int)offsetof(X86, lf_op))
#define O_LFA     ((int)offsetof(X86, lf_a))
#define O_LFB     ((int)offsetof(X86, lf_b))
#define O_LFRES   ((int)offsetof(X86, lf_res))
#define O_SBASE(s) ((int)offsetof(X86, sbase) + (s) * 4)
#define O_SREG(s)  ((int)offsetof(X86, sreg) + (s) * 2)
#define O_JIC     ((int)offsetof(X86, jit_icount))

/* ------------------------------------------------------------ translation state */

static int cpsr_valid;                 /* the ARM flags hold the last x86 flag result */
static int fkind;                      /* ... of this kind */
enum { K_NONE, K_ADD, K_SUB, K_LOGIC, K_INCDEC, K_SHIFT };   /* SHIFT: C = CF, V invalid */
static uint32_t cur_next_eip;          /* where to resume after the instruction being translated */
static int cur_lazy = 1, cur_cpsr = 1;  /* this instruction's lazy flags are needed / the ARM flags are needed after it */
int jit_count;                         /* count translated instructions (/STATS) */
static int cur_count;                  /* x86 instructions completed before the current one */

/* ------------------------------------------------------------ out-of-line code */

enum { OOL_ST, OOL_PEND, OOL_EXIT };
struct ool { int kind, sz, rs, count; uint32_t *br, *back; uint32_t eip; };
static struct ool ools[160];
static int nool;

static void ool_add(int kind, int sz, int rs, uint32_t *br, uint32_t *back, uint32_t eip, int count)
{
    if (nool < 160) {
        struct ool *o = &ools[nool++];
        o->kind = kind; o->sz = sz; o->rs = rs; o->br = br; o->back = back; o->eip = eip; o->count = count;
    } else emit_overflow = 3;
}

/* add n to the translated-instruction count (conditionally) */
static void emit_count(int cond, int n)
{
    if (n <= 0 || !jit_count) return;
    ldst(cond, 1, 0, R12, RCPU, O_JIC);
    E((uint32_t)cond << 28 | 1u << 25 | ADD << 21 | R12 << 16 | R12 << 12 | (n & 0xFF));
    ldst(cond, 0, 0, R12, RCPU, O_JIC);
}

/* exit the block: eip = v (0xFFFFFFFF: already set), return code */
static void emit_exit_eip(uint32_t eip, int code, int count)
{
    emit_count(AL, count);
    if (eip != 0xFFFFFFFFu) {
        mov_imm(R0, eip & 0xFFFF);
        STR(R0, RCPU, O_EIP);
    }
    mov_imm(R0, code);
    branch(AL, 0, (uint32_t *)(void *)jit_exit);
}

/* ------------------------------------------------------------ memory access */

/* R1 = linear address -> rd (not R1-R3); clobbers R2, R3 */
static void emit_load(int sz, int rd)
{
    LSRI(R2, R1, 8);
    ldst_r(1, 0, R2, RRPT, R2, 2, 0);             /* ldr r2, [rpt, r2, lsl #2] */
    if (sz == 0) { ldst_r(1, 1, rd, R2, R1, 0, 0); return; }
    dpr(AL, ADD, 0, R2, R2, R1, LSL, 0);
    LDRB(rd, R2, 0);
    LDRB(R3, R2, 1);
    dpr(AL, ORR, 0, rd, rd, R3, LSL, 8);
    if (sz == 2) {                                /* (byte by byte: VGA latches see the x86's order) */
        LDRB(R3, R2, 2);
        dpr(AL, ORR, 0, rd, rd, R3, LSL, 16);
        LDRB(R3, R2, 3);
        dpr(AL, ORR, 0, rd, rd, R3, LSL, 24);
    }
}

/* store rs (R4-R6) at linear R1 (clobbers R2, R3; the ARM flags survive) */
static void emit_store(int sz, int rs)
{
    int save = cpsr_valid && cur_cpsr;
    if (cpsr_valid && !save) cpsr_valid = 0;          /* (nothing reads them any more) */
    if (save) mrs(R7);
    LSRI(R2, R1, 8);
    ldst_r(1, 0, R2, RWPT, R2, 2, 0);             /* ldr r2, [wpt, r2, lsl #2] */
    dpi(AL, TST, 1, 0, R2, 1);
    uint32_t *b1 = p; branch(NE, 0, p);
    uint32_t *b2 = 0;
    if (sz) {
        dpi(AL, AND, 0, R3, R1, 0xFF);
        dpi(AL, CMP, 1, 0, R3, sz == 2 ? 0xFC : 0xFF);
        b2 = p; branch(sz == 2 ? HI : EQ, 0, p);
    }
    if (sz == 0) ldst_r(0, 1, rs, R2, R1, 0, 0);
    else {
        ldst_r(0, 1, rs, R2, R1, 0, 1);            /* strb rs, [r2, r1]! */
        LSRI(R3, rs, 8);
        STRB(R3, R2, 1);
        if (sz == 2) { LSRI(R3, rs, 16); STRB(R3, R2, 2); LSRI(R3, rs, 24); STRB(R3, R2, 3); }
    }
    uint32_t *back = p;
    if (save) msr_f(R7);
    ool_add(OOL_ST, sz, rs, b1, back, cur_next_eip, cur_count + 1);
    if (b2) ool_add(OOL_ST, sz, rs, b2, back, cur_next_eip, cur_count + 1);
}

/* the slow store: v, lin, bytes; returns nonzero if translated code was hit */
int jh_store(uint32_t v, uint32_t lin, uint32_t bytes)
{
    smc_hit = 0;
    mem_slow_write(lin, v, bytes);
    return smc_hit;
}

/* ------------------------------------------------------------ decoding */

enum { F_NORMAL, F_FOLLOW, F_SIDE };   /* how an instruction ends up in the block */
struct insn {
    uint32_t ip, len;
    int ovr, rep;
    uint32_t op;
    int has_modrm, mod, reg, rm;
    int32_t disp;
    uint32_t imm;
    int flow;
    int o32;                                  /* 66h: 32-bit operands */
    uint8_t lazy_needed, cpsr_after;          /* liveness (translate()) */
    uint8_t cpsr_before;                      /* the ARM flags hold a known x86 result here */
};

static const uint8_t *cbytes;          /* x86 code at the segment base */
/* why blocks end early (a debugging aid read by apps/secondreality/tests/prof.mjs):
   [op] = the translator refused op (0x100 + second byte for 0Fh xx, + 0x200 with 66h),
   [0x400 + op] = the decoder refused it (0x800 + the byte: an unknown prefix) */
uint32_t jit_why[0x1000];
/* /JITOFF:n - leave instruction groups to the interpreter (bisecting the
   translator): 1 far CALL/JMP/RETF, 2 32-bit operands, 4 0Fh xx, 8 IN/OUT,
   16 LES/LDS/PUSHA/POPA/FS/GS pushes */
unsigned jit_off;

static int decode(uint32_t ip, struct insn *in)
{
    uint32_t i = ip;
    memset(in, 0, sizeof *in);
    in->ip = ip;
    in->ovr = -1;
    uint32_t op;
    for (;;) {
        op = cbytes[i & 0xFFFF]; i++;
        if (op == 0x26 || op == 0x2E || op == 0x36 || op == 0x3E) { in->ovr = (op >> 3) & 3; continue; }
        if (op == 0x64 || op == 0x65) { in->ovr = op == 0x64 ? SEG_FS : SEG_GS; continue; }
        if (op == 0x66) { in->o32 = 1; continue; }
        if (op == 0xF2 || op == 0xF3) { in->rep = op; continue; }
        break;
    }
    if (op == 0x0F) op = 0x100 | cbytes[(i++) & 0xFFFF];     /* the 386's two-byte opcodes: 100h + the second byte */
    in->op = op;
    uint32_t whyk = 0x400 | op | (in->o32 ? 0x200 : 0);
#define FB() (cbytes[(i++) & 0xFFFF])
#define FW() (i += 2, (uint32_t)cbytes[(i - 2) & 0xFFFF] | ((uint32_t)cbytes[(i - 1) & 0xFFFF] << 8))
    int modrm = 0, imm = 0;          /* imm: 1 byte, 2 word, 3 byte-sign-ext, 4 dword */
    if (in->o32) {
        /* 32-bit operands: control transfers and segment pushes/pops (whose
           size would change) stay with the interpreter */
        if (op == 0xE8 || op == 0xE9 || op == 0xEB || op == 0xC2 || op == 0xC3 || (op >= 0x70 && op <= 0x7F) || op == 0xE2 || op == 0xE3 ||
            op == 0x06 || op == 0x0E || op == 0x16 || op == 0x1E || op == 0x07 || op == 0x1F || op == 0x9C || op == 0x8C || op == 0x8E ||
            op == 0x8F || op == 0xD7 || (op >= 0x180 && op <= 0x18F) || op == 0x9A || op == 0xEA || op == 0xCA || op == 0xCB ||
            op == 0x1A0 || op == 0x1A1 || op == 0x1A8 || op == 0x1A9)
            { jit_why[whyk & 0xFFF]++; return 0; }
    }
    switch (op) {
    case 0x1B6: case 0x1B7: case 0x1BE: case 0x1BF: case 0x1AF:   /* MOVZX, MOVSX, IMUL r, r/m */
        modrm = 1; break;
    case 0x1A4: case 0x1AC:                                       /* SHLD, SHRD r/m, r, imm8 */
        modrm = 1; imm = 1; break;
    case 0x180: case 0x181: case 0x182: case 0x183: case 0x184: case 0x185: case 0x186: case 0x187:
    case 0x188: case 0x189: case 0x18A: case 0x18B: case 0x18C: case 0x18D: case 0x18E: case 0x18F:
        imm = 2; break;                                           /* Jcc near */
    case 0xE4: case 0xE5: case 0xE6: case 0xE7: imm = 1; break;   /* IN/OUT imm8 */
    case 0x9A: case 0xEA: imm = 5; break;                         /* CALL/JMP far ptr16:16 */
    case 0xCA: imm = 2; break;                                    /* RETF imm16 */
    case 0xCB: case 0x60: case 0x61: case 0x1A0: case 0x1A1: case 0x1A8: case 0x1A9: break;
    case 0xC4: case 0xC5: modrm = 1; break;                       /* LES, LDS */
    case 0x1B4: case 0x1B5: modrm = 1; break;                     /* LFS, LGS */
    case 0xEC: case 0xED: case 0xEE: case 0xEF: break;            /* IN/OUT DX */
    case 0x00: case 0x01: case 0x02: case 0x03: case 0x08: case 0x09: case 0x0A: case 0x0B:
    case 0x10: case 0x11: case 0x12: case 0x13: case 0x18: case 0x19: case 0x1A: case 0x1B:
    case 0x20: case 0x21: case 0x22: case 0x23: case 0x28: case 0x29: case 0x2A: case 0x2B:
    case 0x30: case 0x31: case 0x32: case 0x33: case 0x38: case 0x39: case 0x3A: case 0x3B:
    case 0x84: case 0x85: case 0x86: case 0x87: case 0x88: case 0x89: case 0x8A: case 0x8B:
    case 0x8C: case 0x8D: case 0x8E: case 0xD0: case 0xD1: case 0xD2: case 0xD3: case 0xFE: case 0xFF:
    case 0x8F:
        modrm = 1; break;
    case 0x04: case 0x0C: case 0x14: case 0x1C: case 0x24: case 0x2C: case 0x34: case 0x3C: case 0xA8:
    case 0xB0: case 0xB1: case 0xB2: case 0xB3: case 0xB4: case 0xB5: case 0xB6: case 0xB7:
    case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75: case 0x76: case 0x77:
    case 0x78: case 0x79: case 0x7A: case 0x7B: case 0x7C: case 0x7D: case 0x7E: case 0x7F:
    case 0xEB: case 0xE2: case 0xE3: case 0x6A:
        imm = 1; break;
    case 0x05: case 0x0D: case 0x15: case 0x1D: case 0x25: case 0x2D: case 0x35: case 0x3D: case 0xA9:
    case 0xB8: case 0xB9: case 0xBA: case 0xBB: case 0xBC: case 0xBD: case 0xBE: case 0xBF:
    case 0xE8: case 0xE9: case 0xC2: case 0x68: case 0xA0: case 0xA1: case 0xA2: case 0xA3:
        imm = 2; break;
    case 0x80: case 0x82: case 0xC0: case 0xC1: case 0xC6: modrm = 1; imm = 1; break;
    case 0x81: case 0xC7: case 0x69: modrm = 1; imm = 2; break;
    case 0x83: case 0x6B: modrm = 1; imm = 3; break;
    case 0xF6: case 0xF7: modrm = 1; break;
    case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47:
    case 0x48: case 0x49: case 0x4A: case 0x4B: case 0x4C: case 0x4D: case 0x4E: case 0x4F:
    case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: case 0x55: case 0x56: case 0x57:
    case 0x58: case 0x59: case 0x5A: case 0x5B: case 0x5C: case 0x5D: case 0x5E: case 0x5F:
    case 0x06: case 0x0E: case 0x16: case 0x1E: case 0x07: case 0x1F:
    case 0x90: case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97:
    case 0x98: case 0x99: case 0xC3: case 0xF5: case 0xF8: case 0xF9: case 0xFC: case 0xFD:
    case 0xA4: case 0xA5: case 0xA6: case 0xA7: case 0xAA: case 0xAB: case 0xAC: case 0xAD: case 0xAE: case 0xAF:
    case 0xD7: case 0x9E: case 0x9F: case 0x9C: case 0xFA:
        break;
    default:
        jit_why[whyk & 0xFFF]++;
        return 0;
    }
    if (in->rep && !(op >= 0xA4 && op <= 0xAF && op != 0xA8 && op != 0xA9)) { jit_why[whyk & 0xFFF]++; return 0; }
    if (modrm) {
        uint32_t m = FB();
        in->has_modrm = 1;
        in->mod = m >> 6; in->reg = (m >> 3) & 7; in->rm = m & 7;
        if (in->mod == 1) in->disp = (int8_t)FB();
        else if (in->mod == 2) in->disp = (int16_t)FW();
        else if (in->mod == 0 && in->rm == 6) in->disp = FW();
    }
    if (in->o32 && imm == 2 && op != 0xA0 && op != 0xA1 && op != 0xA2 && op != 0xA3) imm = 4;
    if (imm == 1) in->imm = FB();
    else if (imm == 2) in->imm = FW();
    else if (imm == 3) in->imm = (uint32_t)(int32_t)(int8_t)FB();
    else if (imm == 4 || imm == 5) { uint32_t lo = FW(); in->imm = lo | (FW() << 16); }
    if (op == 0xF6 && in->reg < 2) in->imm = FB();
    if (op == 0xF7 && in->reg < 2) { if (in->o32) { uint32_t lo = FW(); in->imm = lo | (FW() << 16); } else in->imm = FW(); }
    in->len = i - ip;
#undef FB
#undef FW
    return 1;
}

/* ------------------------------------------------------------ operands */

/* effective address -> R1 (linear, or the offset if lin == 0).  Uses R2. */
static void emit_ea(const struct insn *in, int lin)
{
    int def = SEG_DS, n = 0;
    static const int8_t b1[8] = { 3, 3, 5, 5, 6, 7, -1, 3 }, b2[8] = { 6, 7, 6, 7, -1, -1, -1, -1 };
    if (in->mod == 0 && in->rm == 6) mov_imm(R1, (uint32_t)in->disp & 0xFFFF);
    else {
        int r1 = in->rm == 6 ? 5 : b1[in->rm], r2 = b2[in->rm];
        if (in->rm == 2 || in->rm == 3 || in->rm == 6) def = SEG_SS;
        LDRH(R1, RCPU, O_R(r1)); n++;
        if (r2 >= 0) { LDRH(R2, RCPU, O_R(r2)); dpr(AL, ADD, 0, R1, R1, R2, LSL, 0); n++; }
        if (in->disp) { add_imm(R1, R1, in->disp); n++; }
        if (n > 1) { LSLI(R1, R1, 16); LSRI(R1, R1, 16); }
    }
    if (!lin) return;
    int seg = in->ovr >= 0 ? in->ovr : def;
    LDR(R2, RCPU, O_SBASE(seg));
    dpr(AL, ADD, 0, R1, R1, R2, LSL, 0);
}

/* register operand -> rd (zero-extended) */
static uint32_t *last_st_p;             /* just after the last register store, what it stored */
static int last_st_rs, last_st_sz, last_st_r;
static void ld_reg(int rd, int sz, int r)
{
    if (p == last_st_p && last_st_sz == sz && last_st_r == r) {    /* stored just now: still in a register */
        MOVR(rd, last_st_rs);
        if (rd == last_st_rs) return;
        return;
    }
    if (sz == 2) LDR(rd, RCPU, O_R(r)); else if (sz) LDRH(rd, RCPU, O_R(r)); else LDRB(rd, RCPU, O_R8(r));
}
static void st_reg(int rs, int sz, int r)
{
    if (sz == 2) STR(rs, RCPU, O_R(r)); else if (sz) STRH(rs, RCPU, O_R(r)); else STRB(rs, RCPU, O_R8(r));
    last_st_p = p; last_st_rs = rs; last_st_sz = sz; last_st_r = r;
}

/* r/m operand -> rd (for memory, R1 keeps the linear address for a later store) */
static void ld_rm(const struct insn *in, int sz, int rd)
{
    if (in->mod == 3) { ld_reg(rd, sz, in->rm); return; }
    emit_ea(in, 1);
    emit_load(sz, rd);
}
/* rs -> the r/m operand; for memory R1 must still hold the address */
static void st_rm(const struct insn *in, int sz, int rs)
{
    if (in->mod == 3) { st_reg(rs, sz, in->rm); return; }
    emit_store(sz, rs);
}

/* ------------------------------------------------------------ flags */

/* record the lazy state: lf_op = op, lf_a = R4, lf_b = R5, lf_res = R6 */
static void lazy_abr(int lfop)
{
    if (!cur_lazy) return;
    if (lfop_known == lfop) {
        add_imm(R12, RCPU, O_LFA);
        stmia(R12, 1u << R4 | 1u << R5 | 1u << R6);
    } else {
        mov_imm(R3, lfop);
        add_imm(R12, RCPU, O_LFOP);
        stmia(R12, 1u << R3 | 1u << R4 | 1u << R5 | 1u << R6);
        lfop_known = lfop;
    }
}
/* logic ops: only the result matters */
static void lazy_r(int lfop)
{
    if (!cur_lazy) return;
    STR(R6, RCPU, O_LFRES);
    if (lfop_known != lfop) { mov_imm(R3, lfop); STR(R3, RCPU, O_LFOP); lfop_known = lfop; }
}

/* x86 ALU op aop (0-7) on R4 (a) and R5 (b) -> R6; ARM flags + lazy state */
/* rd = rm >> n; n = 0 is a move (LSR #0 would mean LSR #32) */
static void lsr_n(int rd, int rm, int n) { if (n) LSRI(rd, rm, n); else MOVR(rd, rm); }
static uint32_t szmask_of(int sz) { return sz == 2 ? 0xFFFFFFFFu : sz ? 0xFFFFu : 0xFFu; }

static void emit_alu(int aop, int sz)
{
    int sh = sz == 2 ? 0 : sz ? 16 : 24;
    int szbits = sz << 4;
    switch (aop) {
    case 0: case 5: case 7:                     /* ADD, SUB, CMP */
        LSLI(R2, R4, sh);
        dpr(AL, aop == 0 ? ADD : SUB, 1, R2, R2, R5, LSL, sh);
        lsr_n(R6, R2, sh);
        lazy_abr((aop == 0 ? LF_ADD : LF_SUB) | szbits);
        cpsr_valid = 1; fkind = aop == 0 ? K_ADD : K_SUB;
        break;
    case 1: case 4: case 6:                     /* OR, AND, XOR (TEST) */
        LSLI(R2, R4, sh);
        dpr(AL, aop == 1 ? ORR : aop == 4 ? AND : EOR, 1, R2, R2, R5, LSL, sh);
        lsr_n(R6, R2, sh);
        lazy_r(LF_LOGIC | szbits);
        cpsr_valid = 1; fkind = K_LOGIC;
        break;
    case 2: case 3: {                           /* ADC, SBB: the carry in */
        if (cpsr_valid && (fkind == K_ADD || fkind == K_SUB || fkind == K_LOGIC || fkind == K_SHIFT)) {
            /* the ARM flags hold the last result: CF = C (ADD, shifts), !C (SUB), 0 (logic)
               - the carry chains of mixing loops (ADD/ADC) need no helper */
            dpi(AL, MOV, 0, R3, 0, 0);
            if (fkind == K_ADD || fkind == K_SHIFT) dpi(CS, MOV, 0, R3, 0, 1);
            else if (fkind == K_SUB) dpi(CC, MOV, 0, R3, 0, 1);
        } else {
            PUSH_R1();                           /* (the address of a memory operand) */
            BLKEEP(jh_fix_cf);                   /* cpu.flags bit 0 = CF */
            POP_R1();
            LDR(R3, RCPU, O_FLAGS);
            dpi(AL, AND, 0, R3, R3, 1);
        }
        if (aop == 2) { dpr(AL, ADD, 0, R6, R4, R5, LSL, 0); dpr(AL, ADD, 0, R6, R6, R3, LSL, 0); }
        else { dpr(AL, SUB, 0, R6, R4, R5, LSL, 0); dpr(AL, SUB, 0, R6, R6, R3, LSL, 0); }
        if (sz == 1) { LSLI(R6, R6, 16); LSRI(R6, R6, 16); } else if (sz == 0) dpi(AL, AND, 0, R6, R6, 0xFF);
        dpi(AL, ADD, 0, R3, R3, (aop == 2 ? LF_ADD : LF_SUB) | szbits);    /* +1 = ADC/SBB when CF */
        add_imm(R12, RCPU, O_LFOP);
        stmia(R12, 1u << R3 | 1u << R4 | 1u << R5 | 1u << R6);
        lfop_known = -1;
        cpsr_valid = 0; fkind = K_NONE;
        break;
    }
    }
}

/* INC/DEC R4 -> R6 */
static int cur_cpsr_before;
static void emit_incdec(int dec, int sz)
{
    int sh = sz == 2 ? 0 : sz ? 16 : 24;
    if (cur_cpsr_before && !cpsr_valid) { emit_overflow = 6; return; }   /* the liveness pass assumed otherwise */
    if (!(cpsr_valid && fkind == K_INCDEC)) {
        if (cpsr_valid && (fkind == K_ADD || fkind == K_SUB || fkind == K_LOGIC || fkind == K_SHIFT)) {
            /* CF straight from the ARM flags into cpu.flags */
            LDR(R3, RCPU, O_FLAGS);
            dpi(AL, BIC, 0, R3, R3, 1);
            if (fkind == K_ADD || fkind == K_SHIFT) dpi(CS, ORR, 0, R3, R3, 1);
            else if (fkind == K_SUB) dpi(CC, ORR, 0, R3, R3, 1);
            STR(R3, RCPU, O_FLAGS);
        } else {
            /* CF from the lazy state in memory, inline for the usual kinds
               (SUB/CMP: a < b, ADD: res < a, LOGIC: 0; INC/DEC/none: it is
               in cpu.flags already); anything else asks the helper */
            LDR(R3, RCPU, O_LFOP);
            dpi(AL, AND, 0, R3, R3, 15);
            LDR(R0, RCPU, O_FLAGS);
            dpi(AL, BIC, 0, R0, R0, 1);
            dpi(AL, CMP, 1, 0, R3, LF_SUB);  uint32_t *bsub = p; branch(EQ, 0, p);
            dpi(AL, CMP, 1, 0, R3, LF_ADD);  uint32_t *badd = p; branch(EQ, 0, p);
            dpi(AL, CMP, 1, 0, R3, LF_LOGIC); uint32_t *blog = p; branch(EQ, 0, p);
            /* NONE, INC, DEC: CF is in cpu.flags already.  (Not "<= LF_INC":
               that also caught ADC and SBB, whose carry-out then got lost -
               GW-BASIC's double-precision SBB/INC/LOOP chains) */
            dpi(AL, CMP, 1, 0, R3, LF_NONE); uint32_t *bn0 = p; branch(EQ, 0, p);
            dpi(AL, CMP, 1, 0, R3, LF_INC);  uint32_t *bn1 = p; branch(EQ, 0, p);
            dpi(AL, CMP, 1, 0, R3, LF_DEC);  uint32_t *bn2 = p; branch(EQ, 0, p);
            PUSH_R1();
            BLKEEP(jh_fix_cf);
            POP_R1();
            uint32_t *bn3 = p; branch(AL, 0, p);
            patch_branch(bsub, p);
            LDR(R2, RCPU, O_LFA); LDR(R12, RCPU, O_LFB);
            dpr(AL, CMP, 1, 0, R2, R12, LSL, 0);
            dpi(CC, ORR, 0, R0, R0, 1);                 /* borrow: a < b */
            uint32_t *bst = p; branch(AL, 0, p);
            patch_branch(badd, p);
            LDR(R2, RCPU, O_LFA); LDR(R12, RCPU, O_LFRES);
            dpr(AL, CMP, 1, 0, R12, R2, LSL, 0);
            dpi(CC, ORR, 0, R0, R0, 1);                 /* carry: res < a */
            patch_branch(blog, p); patch_branch(bst, p);
            STR(R0, RCPU, O_FLAGS);
            patch_branch(bn0, p); patch_branch(bn1, p); patch_branch(bn2, p); patch_branch(bn3, p);
        }
    }
    LSLI(R2, R4, sh);
    dpi(AL, dec ? SUB : ADD, 1, R2, R2, 1u << sh);
    lsr_n(R6, R2, sh);
    dpi(AL, MOV, 0, R5, 0, 1);
    lazy_abr((dec ? LF_DEC : LF_INC) | (sz << 4));
    cpsr_valid = 1; fkind = K_INCDEC;
}

/* ------------------------------------------------------------ branches */

/* ARM condition for x86 cc from the current flags: -1 = use the helper,
 * -2 = always, -3 = never */
static int arm_cond(int cc)
{
    if (!cpsr_valid) return -1;
    int neg = cc & 1, base = cc & ~1;
    int c;
    switch (base) {
    case 4: c = EQ; break;
    case 8: c = MI; break;
    case 0: if (fkind == K_LOGIC) return neg ? -2 : -3; if (fkind == K_SHIFT) return -1; c = VS; break;
    case 2:
        if (fkind == K_ADD || fkind == K_SHIFT) c = CS;
        else if (fkind == K_SUB) c = CC;
        else if (fkind == K_LOGIC) return neg ? -2 : -3;
        else return -1;
        break;
    case 6:
        if (fkind == K_SUB) c = LS;
        else if (fkind == K_LOGIC) c = EQ;
        else return -1;
        break;
    case 12: if (fkind == K_SHIFT) return -1; c = fkind == K_LOGIC ? MI : LT; break;
    case 14: if (fkind == K_LOGIC || fkind == K_SHIFT) return -1; c = LE; break;
    default: return -1;
    }
    return neg ? c ^ 1 : c;
}

#define MAXCHAIN 12
/* an exit to a static target: LDR<cond> pc, [pc, #lit] in the block; the
 * literal (in the block's pool, after its code) points at a stub in the stub
 * area until the target is translated, then at the target block.  (Plain
 * branches to other code in the same 4 KB page are avoided on purpose: the
 * ARM emulator's JIT turns such targets into extra region entries, and those
 * can go stale when the code is replaced.) */
struct chain { uint32_t *ldr, *lit; uint32_t eip; };
static struct chain chains[MAXCHAIN];
static int nchain;
static uint32_t *blk_entry;
static uint32_t blk_ip;

/* a (possibly conditional) exit to a static x86 target, linkable later */
static void emit_chain(int cond, uint32_t eip, int count)
{
    if (nchain >= MAXCHAIN) { emit_overflow = 4; return; }
    eip &= 0xFFFF;
    emit_count(cond, count);
    if (eip == blk_ip) { branch(cond, 0, blk_entry); return; }     /* a loop: straight back */
    chains[nchain].ldr = p;
    chains[nchain].lit = 0;
    chains[nchain].eip = eip;
    nchain++;
    E((uint32_t)cond << 28 | 0x059FF000u);    /* ldr<cond> pc, [pc, #0] - offset fixed later */
}

/* evaluate x86 cc into an ARM condition (calls the helper if need be) */
static int emit_cond(int cc)
{
    int c = arm_cond(cc);
    if (c == -1) {
        mov_imm(R0, cc);
        BL(jh_cond);
        dpi(AL, CMP, 1, 0, R0, 0);
        c = NE;
        cpsr_valid = 0;
    }
    return c;
}

/* push rs (R4-R6); clobbers R1-R3 */
static void emit_push_sz(int rs, int sz)
{
    LDRH(R1, RCPU, O_R(4));
    dpi(AL, SUB, 0, R1, R1, sz == 2 ? 4 : 2);
    STRH(R1, RCPU, O_R(4));
    LSLI(R1, R1, 16); LSRI(R1, R1, 16);
    LDR(R2, RCPU, O_SBASE(SEG_SS));
    dpr(AL, ADD, 0, R1, R1, R2, LSL, 0);
    emit_store(sz, rs);
}
/* pop -> rd (not R1-R3) */
static void emit_pop_sz(int rd, int sz)
{
    LDRH(R1, RCPU, O_R(4));
    dpi(AL, ADD, 0, R3, R1, sz == 2 ? 4 : 2);
    STRH(R3, RCPU, O_R(4));
    LDR(R2, RCPU, O_SBASE(SEG_SS));
    dpr(AL, ADD, 0, R1, R1, R2, LSL, 0);
    emit_load(sz, rd);
}
/* a far return address: CS (in R6) and IP (next) in one 32-bit store, so
   that a store that hits translated code (which ends the block at once)
   never leaves half of it pushed.  Clobbers R1-R3, R6. */
static void emit_push_far(uint32_t next)
{
    mov_imm(R2, next);
    dpr(AL, ORR, 0, R6, R2, R6, LSL, 16);
    emit_push_sz(R6, 2);
}
static void emit_push(int rs)
{
    LDRH(R1, RCPU, O_R(4));
    dpi(AL, SUB, 0, R1, R1, 2);
    STRH(R1, RCPU, O_R(4));
    LSLI(R1, R1, 16); LSRI(R1, R1, 16);
    LDR(R2, RCPU, O_SBASE(SEG_SS));
    dpr(AL, ADD, 0, R1, R1, R2, LSL, 0);
    emit_store(1, rs);
}
/* pop -> rd (not R1-R3) */
static void emit_pop(int rd)
{
    LDRH(R1, RCPU, O_R(4));
    dpi(AL, ADD, 0, R3, R1, 2);
    STRH(R3, RCPU, O_R(4));
    LDR(R2, RCPU, O_SBASE(SEG_SS));
    dpr(AL, ADD, 0, R1, R1, R2, LSL, 0);
    emit_load(1, rd);
}

/* ------------------------------------------------------------ one instruction */

/* returns: 0 = translated, continue; 1 = translated, block ends here;
 * -1 = not translatable (block ends before it) */
static int emit_insn(const struct insn *in)
{
    uint32_t op = in->op, next = (in->ip + in->len) & 0xFFFF;
    int osz = in->o32 ? 2 : 1;                   /* the operand size of a "word" instruction */
    int sz = (op & 1) ? osz : 0;
    int n1 = cur_count + 1;
    cur_next_eip = next;
    if (in->flow == F_FOLLOW) return 0;          /* a JMP we continued through */
    if (in->o32 && ((op == 0xFE || op == 0xFF) && in->reg >= 2)) return -1;   /* (32-bit CALL/JMP/PUSH m: the interpreter) */
    if (jit_off) {
        if ((jit_off & 1) && (op == 0x9A || op == 0xEA || op == 0xCA || op == 0xCB || ((op == 0xFF) && (in->reg == 3 || in->reg == 5)))) return -1;
        if ((jit_off & 2) && in->o32) return -1;
        if ((jit_off & 4) && op >= 0x100) return -1;
        if ((jit_off & 8) && ((op >= 0xE4 && op <= 0xE7) || (op >= 0xEC && op <= 0xEF))) return -1;
        if ((jit_off & 32) && op == 0x9A) return -1;
        if ((jit_off & 64) && (op == 0xCA || op == 0xCB)) return -1;
        if ((jit_off & 128) && op == 0xFF && (in->reg == 3 || in->reg == 5)) return -1;
        if ((jit_off & 256) && op == 0xEA) return -1;
        if ((jit_off & 16) && (op == 0xC4 || op == 0xC5 || op == 0x1B4 || op == 0x1B5 || op == 0x60 || op == 0x61 || op == 0x1A0 || op == 0x1A1 || op == 0x1A8 || op == 0x1A9)) return -1;
    }
    switch (op) {
    /* ---- far control flow, LES/LDS, PUSHA/POPA, FS/GS */
    case 0xC4: case 0xC5: case 0x1B4: case 0x1B5: {   /* LES / LDS / LFS / LGS r, m16:16 */
        if (in->mod == 3) return -1;
        emit_ea(in, 1);
        emit_load(osz, R4);
        add_imm(R1, R1, osz == 2 ? 4 : 2);
        emit_load(1, R5);
        st_reg(R4, osz, in->reg);
        int sr = op == 0xC4 ? SEG_ES : op == 0xC5 ? SEG_DS : op == 0x1B4 ? SEG_FS : SEG_GS;
        STRH(R5, RCPU, O_SREG(sr));
        LSLI(R5, R5, 4);
        STR(R5, RCPU, O_SBASE(sr));
        return 0;
    }
    case 0x9A: case 0xEA: {                      /* CALL / JMP far ptr16:16 */
        uint32_t tip = in->imm & 0xFFFF, tcs = in->imm >> 16;
        /* the new CS:eip first, so that a push that hits translated code
           (which ends the block there) resumes at the target */
        LDRH(R6, RCPU, O_SREG(SEG_CS));
        mov_imm(R4, tcs);
        STRH(R4, RCPU, O_SREG(SEG_CS));
        mov_imm(R4, tcs << 4);
        STR(R4, RCPU, O_SBASE(SEG_CS));
        mov_imm(R4, tip);
        STR(R4, RCPU, O_EIP);
        if (op == 0x9A) {
            cur_next_eip = 0xFFFFFFFFu;
            emit_push_far(next);
        }
        if (farlog_on) { mov_imm(R0, op == 0x9A ? 17 : 18); mov_imm(R1, blk_cs); mov_imm(R2, in->ip); BL(jh_farlog); }
        emit_count(AL, n1);
        branch(AL, 0, (uint32_t *)(void *)jit_exit_dyn);
        return 1;
    }
    case 0xCA: case 0xCB:                        /* RETF [imm16] */
        emit_pop(R4);
        emit_pop(R5);
        STR(R4, RCPU, O_EIP);
        STRH(R5, RCPU, O_SREG(SEG_CS));
        LSLI(R5, R5, 4);
        STR(R5, RCPU, O_SBASE(SEG_CS));
        if (op == 0xCA) { LDRH(R1, RCPU, O_R(4)); add_imm(R1, R1, in->imm); STRH(R1, RCPU, O_R(4)); }
        if (farlog_on) { mov_imm(R0, 19); mov_imm(R1, blk_cs); mov_imm(R2, in->ip); BL(jh_farlog); }
        emit_count(AL, n1);
        branch(AL, 0, (uint32_t *)(void *)jit_exit_dyn);
        return 1;
    case 0x60: case 0x61:                        /* PUSHA / POPA */
        mov_imm(R0, in->o32);
        if (op == 0x60) BL(jh_pusha); else BL(jh_popa);
        last_st_p = 0;
        cpsr_valid = 0; fkind = K_NONE;
        return 0;
    case 0x1A0: case 0x1A8:                      /* PUSH FS / GS */
        LDRH(R4, RCPU, O_SREG(op == 0x1A0 ? SEG_FS : SEG_GS)); emit_push(R4); return 0;
    case 0x1A1: case 0x1A9:                      /* POP FS / GS */
        emit_pop(R4);
        STRH(R4, RCPU, O_SREG(op == 0x1A1 ? SEG_FS : SEG_GS));
        LSLI(R4, R4, 4);
        STR(R4, RCPU, O_SBASE(op == 0x1A1 ? SEG_FS : SEG_GS));
        return 0;
    /* ---- the 386's additions (0Fh xx), IN/OUT */
    case 0x1B6: case 0x1BE: case 0x1B7: case 0x1BF: {      /* MOVZX / MOVSX r, r/m8 / r/m16 */
        int ssz = (op & 1) ? 1 : 0;
        ld_rm(in, ssz, R4);
        if (op >= 0x1BE) {
            LSLI(R4, R4, ssz ? 16 : 24);
            dpr(AL, MOV, 0, R4, 0, R4, ASR, ssz ? 16 : 24);
            if (osz == 1) { LSLI(R4, R4, 16); LSRI(R4, R4, 16); }
        }
        st_reg(R4, osz, in->reg);
        return 0;
    }
    case 0x1AF:                                  /* IMUL r, r/m */
        ld_rm(in, osz, R4);
        ld_reg(R0, osz, in->reg);
        MOVR(R1, R4);
        mov_imm(R2, osz);
        BL(jh_imul2);
        st_reg(R0, osz, in->reg);
        cpsr_valid = 0; fkind = K_NONE;
        return 0;
    case 0x1A4: case 0x1AC:                      /* SHLD / SHRD r/m, r, imm8 */
        ld_rm(in, osz, R4);
        MOVR(R5, R1);                            /* (the address of a memory operand) */
        mov_imm(R0, (op == 0x1AC) | osz << 1);
        MOVR(R1, R4);
        ld_reg(R2, osz, in->reg);
        mov_imm(R3, in->imm & 0xFF);
        BL(jh_shxd);
        MOVR(R6, R0);
        MOVR(R1, R5);
        cpsr_valid = 0; fkind = K_NONE;
        st_rm(in, osz, R6);
        return 0;
    case 0x180: case 0x181: case 0x182: case 0x183: case 0x184: case 0x185: case 0x186: case 0x187:
    case 0x188: case 0x189: case 0x18A: case 0x18B: case 0x18C: case 0x18D: case 0x18E: case 0x18F: {
        uint32_t taken = next + in->imm;         /* Jcc near (16-bit displacement) */
        int c = emit_cond(op & 15);
        if (c == -2) { emit_chain(AL, taken, n1); return 1; }
        if (c != -3) emit_chain(c, taken, n1);
        if (in->flow == F_SIDE) return 0;
        emit_chain(AL, next, n1);
        return 1;
    }
    case 0xE4: case 0xE5: case 0xEC: case 0xED: {  /* IN */
        int isz = (op & 1) ? osz : 0;
        if (op >= 0xEC) LDRH(R0, RCPU, O_R(2)); else mov_imm(R0, in->imm & 0xFF);
        mov_imm(R1, isz);
        BL(io_in);
        MOVR(R4, R0);
        st_reg(R4, isz, 0);
        cpsr_valid = 0; fkind = K_NONE;
        return 0;
    }
    case 0xE6: case 0xE7: case 0xEE: case 0xEF: {  /* OUT */
        int isz = (op & 1) ? osz : 0;
        ld_reg(R4, isz, 0);
        if (op >= 0xEE) LDRH(R0, RCPU, O_R(2)); else mov_imm(R0, in->imm & 0xFF);
        MOVR(R1, R4);
        mov_imm(R2, isz);
        BL(io_out);
        cpsr_valid = 0; fkind = K_NONE;
        return 0;
    }
    case 0x00: case 0x01: case 0x02: case 0x03: case 0x08: case 0x09: case 0x0A: case 0x0B:
    case 0x10: case 0x11: case 0x12: case 0x13: case 0x18: case 0x19: case 0x1A: case 0x1B:
    case 0x20: case 0x21: case 0x22: case 0x23: case 0x28: case 0x29: case 0x2A: case 0x2B:
    case 0x30: case 0x31: case 0x32: case 0x33: case 0x38: case 0x39: case 0x3A: case 0x3B: {
        int aop = (op >> 3) & 7;
        if (op & 2) {                            /* reg op= r/m */
            ld_rm(in, sz, R5);
            ld_reg(R4, sz, in->reg);
            emit_alu(aop, sz);
            if (aop != 7) st_reg(R6, sz, in->reg);
        } else {                                 /* r/m op= reg */
            ld_rm(in, sz, R4);
            ld_reg(R5, sz, in->reg);
            emit_alu(aop, sz);
            if (aop != 7) st_rm(in, sz, R6);
        }
        return 0;
    }
    case 0x04: case 0x0C: case 0x14: case 0x1C: case 0x24: case 0x2C: case 0x34: case 0x3C:
    case 0x05: case 0x0D: case 0x15: case 0x1D: case 0x25: case 0x2D: case 0x35: case 0x3D: {
        int aop = (op >> 3) & 7;
        ld_reg(R4, sz, 0);
        mov_imm(R5, in->imm & szmask_of(sz));
        emit_alu(aop, sz);
        if (aop != 7) st_reg(R6, sz, 0);
        return 0;
    }
    case 0x80: case 0x81: case 0x82: case 0x83:
        ld_rm(in, sz, R4);
        mov_imm(R5, in->imm & szmask_of(sz));
        emit_alu(in->reg, sz);
        if (in->reg != 7) st_rm(in, sz, R6);
        return 0;
    case 0x84: case 0x85:
        ld_rm(in, sz, R4);
        ld_reg(R5, sz, in->reg);
        emit_alu(4, sz);
        return 0;
    case 0xA8: case 0xA9:
        ld_reg(R4, sz, 0);
        mov_imm(R5, in->imm);
        emit_alu(4, sz);
        return 0;
    case 0x86: case 0x87:                        /* XCHG r/m, reg */
        ld_rm(in, sz, R4);
        ld_reg(R5, sz, in->reg);
        st_reg(R4, sz, in->reg);
        st_rm(in, sz, R5);
        return 0;
    case 0x88: case 0x89:
        ld_reg(R4, sz, in->reg);
        if (in->mod == 3) { st_reg(R4, sz, in->rm); return 0; }
        emit_ea(in, 1);
        emit_store(sz, R4);
        return 0;
    case 0x8A: case 0x8B:
        ld_rm(in, sz, R4);
        st_reg(R4, sz, in->reg);
        return 0;
    case 0x8C:
        if (in->reg > 5) return -1;
        LDRH(R4, RCPU, O_SREG(in->reg));
        if (in->mod == 3) { st_reg(R4, 1, in->rm); return 0; }
        emit_ea(in, 1); emit_store(1, R4);
        return 0;
    case 0x8E:
        if (in->reg == SEG_CS || in->reg > 5 || in->reg == SEG_SS) return -1;
        ld_rm(in, 1, R4);
        STRH(R4, RCPU, O_SREG(in->reg));
        LSLI(R4, R4, 4);
        STR(R4, RCPU, O_SBASE(in->reg));
        return 0;
    case 0x8D:
        if (in->mod == 3) return -1;
        emit_ea(in, 0);
        if (osz == 2) STR(R1, RCPU, O_R(in->reg));          /* (the 16-bit offset, zero-extended) */
        else STRH(R1, RCPU, O_R(in->reg));
        return 0;
    case 0xA0: case 0xA1: case 0xA2: case 0xA3:
        mov_imm(R1, in->imm);
        LDR(R2, RCPU, O_SBASE(in->ovr >= 0 ? in->ovr : SEG_DS));
        dpr(AL, ADD, 0, R1, R1, R2, LSL, 0);
        if (op & 2) { ld_reg(R4, sz, 0); emit_store(sz, R4); }
        else { emit_load(sz, R4); st_reg(R4, sz, 0); }
        return 0;
    case 0xB0: case 0xB1: case 0xB2: case 0xB3: case 0xB4: case 0xB5: case 0xB6: case 0xB7:
        mov_imm(R0, in->imm); st_reg(R0, 0, op & 7); return 0;
    case 0xB8: case 0xB9: case 0xBA: case 0xBB: case 0xBC: case 0xBD: case 0xBE: case 0xBF:
        mov_imm(R0, in->imm); st_reg(R0, osz, op & 7); return 0;
    case 0xC6: case 0xC7:
        if (in->mod == 3) { mov_imm(R0, in->imm); st_reg(R0, sz, in->rm); return 0; }
        emit_ea(in, 1);
        mov_imm(R4, in->imm & szmask_of(sz));
        emit_store(sz, R4);
        return 0;
    case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47:
    case 0x48: case 0x49: case 0x4A: case 0x4B: case 0x4C: case 0x4D: case 0x4E: case 0x4F:
        ld_reg(R4, osz, op & 7);
        emit_incdec(op >= 0x48, osz);
        st_reg(R6, osz, op & 7);
        return 0;
    case 0xFE: case 0xFF:
        switch (in->reg) {
        case 0: case 1:
            ld_rm(in, sz, R4);
            emit_incdec(in->reg, sz);
            st_rm(in, sz, R6);
            return 0;
        case 2:                                  /* CALL near r/m */
            if (op == 0xFE) return -1;
            ld_rm(in, 1, R4);
            STR(R4, RCPU, O_EIP);
            cur_next_eip = 0xFFFFFFFFu;           /* (an overwritten-code exit keeps eip = target) */
            mov_imm(R5, next); emit_push(R5);
            emit_count(AL, n1);
            branch(AL, 0, (uint32_t *)(void *)jit_exit_dyn);
            return 1;
        case 4:                                  /* JMP near r/m */
            if (op == 0xFE) return -1;
            ld_rm(in, 1, R4);
            STR(R4, RCPU, O_EIP);
            emit_count(AL, n1);
            branch(AL, 0, (uint32_t *)(void *)jit_exit_dyn);
            return 1;
        case 6:
            if (op == 0xFE) return -1;
            ld_rm(in, 1, R4); emit_push(R4);
            return 0;
        case 3: case 5:                          /* CALL / JMP far m16:16 */
            if (op == 0xFE || in->mod == 3) return -1;
            emit_ea(in, 1);
            emit_load(1, R4);
            add_imm(R1, R1, 2);
            emit_load(1, R5);
            LDRH(R6, RCPU, O_SREG(SEG_CS));
            STR(R4, RCPU, O_EIP);                /* the new CS:eip first (as for 9Ah) */
            STRH(R5, RCPU, O_SREG(SEG_CS));
            LSLI(R5, R5, 4);
            STR(R5, RCPU, O_SBASE(SEG_CS));
            if (in->reg == 3) {
                cur_next_eip = 0xFFFFFFFFu;
                emit_push_far(next);
            }
            if (farlog_on) { mov_imm(R0, in->reg == 3 ? 17 : 18); mov_imm(R1, blk_cs); mov_imm(R2, in->ip); BL(jh_farlog); }
            emit_count(AL, n1);
            branch(AL, 0, (uint32_t *)(void *)jit_exit_dyn);
            return 1;
        default: return -1;
        }
    case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: case 0x55: case 0x56: case 0x57:
        if (osz == 2) {
            if (op == 0x54) return -1;
            ld_reg(R4, 2, op & 7); emit_push_sz(R4, 2); return 0;
        }
        ld_reg(R4, 1, op & 7); emit_push(R4); return 0;
    case 0x58: case 0x59: case 0x5A: case 0x5B: case 0x5C: case 0x5D: case 0x5E: case 0x5F:
        if (osz == 2) {
            if (op == 0x5C) return -1;
            emit_pop_sz(R4, 2); st_reg(R4, 2, op & 7); return 0;
        }
        emit_pop(R4); st_reg(R4, 1, op & 7); return 0;
    case 0x06: case 0x0E: case 0x16: case 0x1E:
        LDRH(R4, RCPU, O_SREG(op >> 3)); emit_push(R4); return 0;
    case 0x07: case 0x1F:
        emit_pop(R4);
        STRH(R4, RCPU, O_SREG(op >> 3));
        LSLI(R4, R4, 4);
        STR(R4, RCPU, O_SBASE(op >> 3));
        return 0;
    case 0x68: case 0x6A:
        if (osz == 2) { mov_imm(R4, in->imm); emit_push_sz(R4, 2); return 0; }
        mov_imm(R4, in->imm & 0xFFFF); emit_push(R4); return 0;
    case 0x8F:
        if (in->reg) return -1;
        emit_pop(R4);
        if (in->mod == 3) { st_reg(R4, 1, in->rm); return 0; }
        emit_ea(in, 1); emit_store(1, R4);
        return 0;
    case 0x90: return 0;
    case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97:
        if (osz == 2) {
            LDR(R0, RCPU, O_R(0)); LDR(R1, RCPU, O_R(op & 7));
            STR(R1, RCPU, O_R(0)); STR(R0, RCPU, O_R(op & 7));
            last_st_p = 0;
            return 0;
        }
        LDRH(R0, RCPU, O_R(0)); LDRH(R1, RCPU, O_R(op & 7));
        STRH(R1, RCPU, O_R(0)); STRH(R0, RCPU, O_R(op & 7));
        return 0;
    case 0x98:                                   /* CBW / CWDE */
        if (osz == 2) {
            LDRH(R0, RCPU, O_R(0)); LSLI(R0, R0, 16); dpr(AL, MOV, 0, R0, 0, R0, ASR, 16);
            STR(R0, RCPU, O_R(0));
            last_st_p = 0;
            return 0;
        }
        LDRB(R0, RCPU, O_R8(0)); LSLI(R0, R0, 24); dpr(AL, MOV, 0, R0, 0, R0, ASR, 24);
        STRH(R0, RCPU, O_R(0));
        return 0;
    case 0x99:                                   /* CWD / CDQ */
        if (osz == 2) {
            LDR(R0, RCPU, O_R(0)); dpr(AL, MOV, 0, R0, 0, R0, ASR, 31);
            STR(R0, RCPU, O_R(2));
            last_st_p = 0;
            return 0;
        }
        LDRH(R0, RCPU, O_R(0)); LSLI(R0, R0, 16); dpr(AL, MOV, 0, R0, 0, R0, ASR, 31);
        STRH(R0, RCPU, O_R(2));
        return 0;
    case 0xD7:                                   /* XLAT */
        LDRB(R0, RCPU, O_R8(0)); LDRH(R1, RCPU, O_R(3));
        dpr(AL, ADD, 0, R1, R1, R0, LSL, 0);
        LSLI(R1, R1, 16); LSRI(R1, R1, 16);
        LDR(R2, RCPU, O_SBASE(in->ovr >= 0 ? in->ovr : SEG_DS));
        dpr(AL, ADD, 0, R1, R1, R2, LSL, 0);
        emit_load(0, R4);
        STRB(R4, RCPU, O_R8(0));
        return 0;
    case 0xF5: case 0xF8: case 0xF9: case 0xFC: case 0xFD: case 0x9E: case 0x9F:
        mov_imm(R0, op); BL(jh_flagop);
        cpsr_valid = 0; fkind = K_NONE;
        return 0;
    case 0x9C: BL(jh_pushf); cpsr_valid = 0; return 0;
    case 0xFA:
        LDR(R0, RCPU, O_FLAGS); dpi(AL, BIC, 0, R0, R0, F_IF); STR(R0, RCPU, O_FLAGS);
        return 0;
    case 0xD0: case 0xD1: case 0xD2: case 0xD3: case 0xC0: case 0xC1: {
        int bits = 8 << sz, g = in->reg;
        uint32_t cnt = op <= 0xC1 ? (in->imm & 31) : 1;
        if (op <= 0xD1 || op <= 0xC1) {
            if (op >= 0xD2 && op <= 0xD3) cnt = 0;
        }
        if (op >= 0xD2 || cnt == 0 || cnt >= (uint32_t)bits || !(g == 4 || g == 5 || g == 6 || g == 7)) {
            /* by CL, rotates, odd counts: the interpreter's routine */
            ld_rm(in, sz, R4);
            MOVR(R5, R1);
            if (op >= 0xD2 && op <= 0xD3) LDRB(R2, RCPU, O_R8(1));
            else mov_imm(R2, op <= 0xC1 ? (in->imm & 0xFF) : 1);
            MOVR(R1, R4);
            mov_imm(R0, g);
            mov_imm(R3, sz);
            BL(jh_shift);
            MOVR(R6, R0);
            MOVR(R1, R5);
            cpsr_valid = 0; fkind = K_NONE;
            st_rm(in, sz, R6);
            return 0;
        }
        int sh = 32 - bits;
        ld_rm(in, sz, R4);
        if (g == 5) {                            /* SHR: the zero-extended value */
            dpr(AL, MOV, 1, R6, 0, R4, LSR, cnt);
        } else {
            LSLI(R2, R4, sh);
            dpr(AL, MOV, 1, R2, 0, R2, g == 7 ? ASR : LSL, cnt);
            lsr_n(R6, R2, sh);
        }
        mov_imm(R5, cnt);
        lazy_abr((g == 5 ? LF_SHR : g == 7 ? LF_SAR : LF_SHL) | (sz << 4));
        cpsr_valid = 1; fkind = K_SHIFT;
        st_rm(in, sz, R6);
        return 0;
    }
    case 0xF6: case 0xF7:
        switch (in->reg) {
        case 0: case 1:
            ld_rm(in, sz, R4); mov_imm(R5, in->imm & szmask_of(sz)); emit_alu(4, sz); return 0;
        case 2:
            ld_rm(in, sz, R4); dpr(AL, MVN, 0, R4, 0, R4, LSL, 0);
            if (sz == 1) { LSLI(R4, R4, 16); LSRI(R4, R4, 16); } else if (sz == 0) dpi(AL, AND, 0, R4, R4, 0xFF);
            st_rm(in, sz, R4); return 0;
        case 3:
            ld_rm(in, sz, R5);
            dpi(AL, MOV, 0, R4, 0, 0);
            emit_alu(5, sz);
            st_rm(in, sz, R6);
            return 0;
        case 4: case 5:
            ld_rm(in, sz, R4); MOVR(R1, R4);
            mov_imm(R0, in->reg == 5); mov_imm(R2, sz);
            BL(jh_mul);
            cpsr_valid = 0; fkind = K_NONE;
            return 0;
        case 6: case 7: {
            ld_rm(in, sz, R4); MOVR(R1, R4);
            mov_imm(R0, in->reg == 7); mov_imm(R2, sz);
            BL(jh_div);
            dpi(AL, CMP, 1, 0, R0, 0);
            uint32_t *bx = p; branch(NE, 0, p);
            ool_add(OOL_EXIT, 0, 0, bx, 0, in->ip, cur_count);   /* divide error: the interpreter raises INT 0 */
            cpsr_valid = 0; fkind = K_NONE;
            return 0;
        }
        }
        return -1;
    case 0x69: case 0x6B:
        if (osz == 2) {
            ld_rm(in, 2, R4);
            MOVR(R0, R4);
            mov_imm(R1, in->imm);
            mov_imm(R2, 2);
            BL(jh_imul2);
            st_reg(R0, 2, in->reg);
            cpsr_valid = 0; fkind = K_NONE;
            return 0;
        }
        ld_rm(in, 1, R4);
        MOVR(R0, R4);
        mov_imm(R1, in->imm & 0xFFFF);
        if (op == 0x6B) { LSLI(R1, R1, 16); dpr(AL, MOV, 0, R1, 0, R1, ASR, 16); }
        BL(jh_imul3);
        st_reg(R0, 1, in->reg);
        cpsr_valid = 0; fkind = K_NONE;
        return 0;
    case 0xA4: case 0xA5: case 0xA6: case 0xA7: case 0xAA: case 0xAB: case 0xAC: case 0xAD: case 0xAE: case 0xAF: {
        mov_imm(R0, op); mov_imm(R1, sz);
        if (in->ovr >= 0) mov_imm(R2, in->ovr); else dpi(AL, MVN, 0, R2, 0, 0);
        mov_imm(R3, in->rep);
        BL(jh_string);
        cpsr_valid = 0; fkind = K_NONE;
        if (in->rep) {
            dpi(AL, CMP, 1, 0, R0, 0);
            uint32_t *bx = p; branch(NE, 0, p);
            ool_add(OOL_EXIT, 0, 0, bx, 0, in->ip, cur_count);   /* paused: resume from the dispatcher */
        }
        return 0;
    }

    /* ---- control flow */
    case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75: case 0x76: case 0x77:
    case 0x78: case 0x79: case 0x7A: case 0x7B: case 0x7C: case 0x7D: case 0x7E: case 0x7F: {
        uint32_t taken = next + (int8_t)in->imm;
        int c = emit_cond(op & 15);
        if (c == -2) { emit_chain(AL, taken, n1); return 1; }
        if (c != -3) emit_chain(c, taken, n1);
        if (in->flow == F_SIDE) return 0;        /* the block goes on at the fall-through */
        emit_chain(AL, next, n1);
        return 1;
    }
    case 0xEB: emit_chain(AL, next + (int8_t)in->imm, n1); return 1;
    case 0xE9: emit_chain(AL, next + in->imm, n1); return 1;
    case 0xE8:
        cur_next_eip = (next + in->imm) & 0xFFFF;
        mov_imm(R4, next); emit_push(R4);
        emit_chain(AL, next + in->imm, n1);
        return 1;
    case 0xC3: case 0xC2:
        emit_pop(R4);
        STR(R4, RCPU, O_EIP);
        if (op == 0xC2) { LDRH(R1, RCPU, O_R(4)); add_imm(R1, R1, in->imm); STRH(R1, RCPU, O_R(4)); }
        emit_count(AL, n1);
        branch(AL, 0, (uint32_t *)(void *)jit_exit_dyn);
        return 1;
    case 0xE2: case 0xE3: {                      /* LOOP, JCXZ (flags untouched) */
        uint32_t taken = next + (int8_t)in->imm;
        int valid = cpsr_valid;
        if (valid && !cur_cpsr) valid = cpsr_valid = 0;
        if (valid) mrs(R7);
        LDRH(R0, RCPU, O_R(1));
        if (op == 0xE2) {
            dpi(AL, SUB, 0, R0, R0, 1);
            STRH(R0, RCPU, O_R(1));
            dpr(AL, MOV, 1, R0, 0, R0, LSL, 16);
        } else dpi(AL, CMP, 1, 0, R0, 0);
        int c = op == 0xE2 ? NE : EQ;
        emit_chain(c, taken, n1);                /* (the target does not need the x86 flags in the ARM ones) */
        if (valid) msr_f(R7);
        if (in->flow == F_SIDE) return 0;
        emit_chain(AL, next, n1);
        return 1;
    }
    }
    return -1;
}

/* ------------------------------------------------------------ blocks */

static void mark_code(uint32_t start, uint32_t end)
{
    for (uint32_t l = start; l < end; l++) codebits[l >> 3] |= 1 << (l & 7);
    for (uint32_t l = start & ~0xFFu; l < end; l += 256) wpt[l >> 8] |= 1;
}

static int is_cond_branch(uint32_t op) { return (op >= 0x70 && op <= 0x7F) || op == 0xE2 || op == 0xE3 || (op >= 0x180 && op <= 0x18F); }

/* what an instruction does to the flags: W writes the lazy state (the JIT's
 * own, eliminable), R reads it (a helper, or an exit), X can leave the block
 * (needs the lazy state then), C reads the ARM flags, M = a W whose own store
 * can leave the block after setting them */
enum { FW = 1, FR = 2, FX = 4, FC = 8, FM = 16 };
static int flag_use(const struct insn *in)
{
    uint32_t op = in->op;
    int mem = in->has_modrm && in->mod != 3;
    if (in->flow == F_FOLLOW) return 0;
    if (op < 0x40 && (op & 7) < 6) {                  /* ALU */
        int aop = (op >> 3) & 7, u = (aop == 2 || aop == 3) ? FR | FW : FW;
        if (!(op & 2) && (op & 7) < 4 && mem && aop != 7) u |= FX | FM;
        return u;
    }
    if (op >= 0x80 && op <= 0x83) { int u = in->reg == 2 || in->reg == 3 ? FR | FW : FW; if (mem && in->reg != 7) u |= FX | FM; return u; }
    if (op == 0x84 || op == 0x85 || op == 0xA8 || op == 0xA9) return FW;
    if (op >= 0x40 && op <= 0x4F) return FW | FC | (in->cpsr_before ? 0 : FR);
    if ((op == 0xFE || op == 0xFF) && in->reg < 2) return FW | FC | (in->cpsr_before ? 0 : FR) | (mem ? FX | FM : 0);
    if (op == 0xF6 || op == 0xF7) {
        if (in->reg < 2) return FW;
        if (in->reg == 2) return mem ? FX : 0;
        if (in->reg == 3) return FW | (mem ? FX | FM : 0);
        return FR | FW | FX;                          /* MUL/DIV: helpers */
    }
    if ((op >= 0xD0 && op <= 0xD3) || op == 0xC0 || op == 0xC1) {
        int bits = (op & 1) ? 16 : 8;
        uint32_t cnt = op <= 0xC1 ? (in->imm & 31) : 1;
        int inl = op < 0xD2 && cnt && cnt < (uint32_t)bits && in->reg >= 4;
        return inl ? FW | (mem ? FX | FM : 0) : FR | FW | FX | FM;
    }
    if (op == 0x69 || op == 0x6B || op == 0x1AF) return FR | FW;
    if (op == 0x1A4 || op == 0x1AC) return FR | FW | FX | FM;
    if (op == 0xF5 || op == 0xF8 || op == 0xF9 || op == 0xFC || op == 0xFD || op == 0x9E || op == 0x9F || op == 0x9C) return FR | FW | FX;
    if (op >= 0xA4 && op <= 0xAF) return FR | FX;
    if (is_cond_branch(op)) return FR | FX | FC;
    /* stores: the slow path can end the block */
    if ((op >= 0x88 && op <= 0x89 && mem) || (op == 0x8C && mem) || (op >= 0xC6 && op <= 0xC7 && mem) || (op >= 0x50 && op <= 0x57) ||
        op == 0x06 || op == 0x0E || op == 0x16 || op == 0x1E || op == 0x68 || op == 0x6A || op == 0xA2 || op == 0xA3 ||
        op == 0x86 || op == 0x87 || (op == 0x8F && mem) || op == 0xE8 || ((op == 0xFE || op == 0xFF) && in->reg >= 2))
        return FX;
    if (op == 0xEB || op == 0xE9 || op == 0xC3 || op == 0xC2 || op == 0x9A || op == 0xEA || op == 0xCA || op == 0xCB ||
        op == 0x60 || op == 0x1A0 || op == 0x1A8) return FX;
    return 0;
}

static struct jblk *translate(uint32_t cs, uint32_t ip)
{
    uint32_t base = (uint32_t)cs << 4;
    if (nblk >= MAXBLK || cp >= cend || sp_ >= send || nlink >= MAXLINK - 16) {
        if (jit_dump == 2) dbg("[flush: nblk=%d code=%ld stubs=%ld links=%d]\n", nblk, (long)(cp - cache), (long)(sp_ - stubs), nlink);
        flush_all();
    }
    cbytes = mem + base;
    blk_cs = cs;
    static struct insn ins[MAXINS];
    int n = 0, nfollow = 0;
    uint32_t cur = ip;
    /* decode the superblock */
    while (n < MAXINS) {
        struct insn *in = &ins[n];
        if (!decode(cur, in)) break;
        n++;
        uint32_t op = in->op, next = (cur + in->len) & 0xFFFF;
        if (op == 0xEB || op == 0xE9) {
            uint32_t t = (next + (op == 0xEB ? (uint32_t)(int32_t)(int8_t)in->imm : in->imm)) & 0xFFFF;
            int seen = t == ip;
            for (int k = 0; k < n && !seen; k++) if (ins[k].ip == t) seen = 1;
            if (seen || nfollow >= MAXSEG - 1) break;
            in->flow = F_FOLLOW;
            nfollow++;
            cur = t;
            continue;
        }
        if (is_cond_branch(op)) {
            if (n >= MAXINS - 1) break;
            in->flow = F_SIDE;
            cur = next;
            continue;
        }
        if (op == 0xE8 || op == 0xC3 || op == 0xC2 || (op == 0xFF && (in->reg >= 2 && in->reg <= 5)) ||
            op == 0x9A || op == 0xEA || op == 0xCA || op == 0xCB) break;
        cur = next;
    }
    if (n == 0) return 0;
    /* forward: where the ARM flags will hold a known result (so INC/DEC take
       CF from them instead of the lazy state) - mirrors emit_insn() */
    {
        int known = 0;
        for (int i = 0; i < n; i++) {
            const struct insn *in = &ins[i];
            uint32_t op = in->op;
            ins[i].cpsr_before = known;
            if (in->flow == F_FOLLOW) continue;
            if ((op < 0x40 && (op & 7) < 6)) known = ((op >> 3) & 7) == 2 || ((op >> 3) & 7) == 3 ? 0 : 1;
            else if (op >= 0x80 && op <= 0x83) known = !(in->reg == 2 || in->reg == 3);
            else if (op == 0x84 || op == 0x85 || op == 0xA8 || op == 0xA9 || (op >= 0x40 && op <= 0x4F)) known = 1;
            else if ((op == 0xFE || op == 0xFF) && in->reg < 2) known = 1;
            else if ((op == 0xF6 || op == 0xF7)) known = in->reg < 2 || in->reg == 3 ? 1 : in->reg == 2 ? known : 0;
            else if ((op >= 0xD0 && op <= 0xD3) || op == 0xC0 || op == 0xC1) known = (flag_use(in) & FR) ? 0 : 1;
            else if (op == 0x69 || op == 0x6B || op == 0xF5 || op == 0xF8 || op == 0xF9 || op == 0xFC || op == 0xFD ||
                     op == 0x9E || op == 0x9F || op == 0x9C || (op >= 0xA4 && op <= 0xAF)) known = 0;
            else if (op >= 0x70 && op <= 0x7F) known = known;      /* (a helper condition drops it: conservative below) */
            else if (op == 0x1AF || op == 0x1A4 || op == 0x1AC || (op >= 0xE4 && op <= 0xE7) || (op >= 0xEC && op <= 0xEF) ||
                     op == 0x60 || op == 0x61) known = 0;   /* helpers */
            if ((op >= 0x70 && op <= 0x7F) || (op >= 0x180 && op <= 0x18F)) {
                /* JP/JNP and some kinds need the helper, which clobbers the flags */
                int cc = op & 15;
                if (!(cc == 4 || cc == 5 || cc == 8 || cc == 9)) known = 0;
            }
        }
    }
    /* flag liveness, backwards: skip lazy-flag stores nobody can see, and
       ARM flag saves around stores nobody reads afterwards */
    {
        int lazy_live = 1, cpsr_live = 0;
        for (int i = n - 1; i >= 0; i--) {
            int u = flag_use(&ins[i]);
            ins[i].lazy_needed = lazy_live || (u & FM);
            ins[i].cpsr_after = cpsr_live;
            if (u & FW) { lazy_live = 0; cpsr_live = 0; }
            if (u & (FR | FX)) lazy_live = 1;
            if (u & FC) cpsr_live = 1;
        }
    }

    p = cp;
    plimit = cend + 900;
    emit_overflow = 0;
    nool = 0;
    nchain = 0;
    cpsr_valid = 0;
    fkind = K_NONE;
    lfop_known = -1;
    blk_entry = p;
    blk_ip = ip;
    /* prologue: anything pending? */
    LDR(R0, RPEND, 0);
    dpi(AL, CMP, 1, 0, R0, 0);
    uint32_t *bpend = p; branch(NE, 0, p);
    ool_add(OOL_PEND, 0, 0, bpend, 0, ip, 0);

    int done = 0, k;
    for (k = 0; k < n && !done; k++) {
        uint32_t *mark = p;
        int nool0 = nool, nchain0 = nchain, lk = lfop_known, cv = cpsr_valid, fk = fkind;
        cur_count = k;
        cur_lazy = ins[k].lazy_needed;
        cur_cpsr = ins[k].cpsr_after;
        cur_cpsr_before = ins[k].cpsr_before;
        int r = emit_insn(&ins[k]);
        if (r < 0) jit_why[(ins[k].op | (ins[k].o32 ? 0x200 : 0)) & 0x3FF]++;
        if (r < 0) { p = mark; nool = nool0; nchain = nchain0; lfop_known = lk; cpsr_valid = cv; fkind = fk; break; }
        if (r == 1) done = 1;
        if (emit_overflow) break;
    }
    if (k == 0 || emit_overflow) { if (jit_dump) dbg("[translate %04lX failed: k=%d overflow=%d]\n", (unsigned long)ip, k, emit_overflow); return 0; }
    if (!done) {
        /* ran out (or met an instruction for the interpreter): go on at the next one */
        const struct insn *l = &ins[k - 1];
        uint32_t nx = (l->ip + l->len) & 0xFFFF;
        if (l->flow == F_FOLLOW) nx = (nx + (l->op == 0xEB ? (uint32_t)(int32_t)(int8_t)l->imm : l->imm)) & 0xFFFF;
        if (k < n && l->flow != F_FOLLOW) nx = ins[k].ip;
        cur_lazy = 1; cur_cpsr = 0;
        emit_chain(AL, nx, k);
    }
    /* the byte ranges actually translated: runs of consecutive instructions */
    uint32_t seg[MAXSEG][2];
    int nseg = 0;
    uint32_t s0 = ins[0].ip;
    for (int i = 0; i < k; i++) {
        int last = i == k - 1 || ins[i].flow == F_FOLLOW || ins[i + 1].ip != ((ins[i].ip + ins[i].len) & 0xFFFF);
        if (!last) continue;
        uint32_t e = ins[i].ip + ins[i].len;
        if (nseg < MAXSEG) { seg[nseg][0] = s0; seg[nseg][1] = e; nseg++; }
        else seg[MAXSEG - 1][1] = e > seg[MAXSEG - 1][1] ? e : seg[MAXSEG - 1][1];
        if (i + 1 < k) s0 = ins[i + 1].ip;
    }
    /* the literal pool: one word per static exit */
    for (int i = 0; i < nchain; i++) {
        chains[i].lit = p;
        int32_t off = (int32_t)((p - (chains[i].ldr + 2)) * 4);
        if (off < 0) { *chains[i].ldr &= ~(1u << 23); off = -off; }     /* (the literal right after the LDR: pc+8 is past it) */
        if (off > 4095) { emit_overflow = 5; break; }
        *chains[i].ldr |= (uint32_t)off;
        E(0);
    }
    uint32_t *blk_end = p;
    /* out-of-line code and exit stubs: in the stub area */
    p = sp_;
    plimit = send + 900;
    for (int i = 0; i < nool; i++) {
        struct ool *o = &ools[i];
        patch_branch(o->br, p);
        if (o->kind == OOL_ST) {
            MOVR(R0, o->rs);
            mov_imm(R2, o->sz == 2 ? 4 : o->sz ? 2 : 1);
            BLKEEP(jh_store);
            dpi(AL, CMP, 1, 0, R0, 0);
            branch(EQ, 0, o->back);              /* (back: restores the x86 flags if saved) */
            emit_exit_eip(o->eip, 0, o->count);  /* translated code was overwritten: stop here */
        } else if (o->kind == OOL_EXIT) emit_exit_eip(o->eip, 0, o->count);
        else emit_exit_eip(o->eip, 1, 0);
    }
    for (int i = 0; i < nchain; i++) {
        if (!chains[i].lit) break;
        *chains[i].lit = (uint32_t)p;            /* unlinked: the literal points at this stub */
        mov_imm(R0, chains[i].eip);
        STR(R0, RCPU, O_EIP);
        mov_imm(R0, (uint32_t)chains[i].lit);    /* r0 = the literal, so the dispatcher can link it */
        branch(AL, 0, (uint32_t *)(void *)jit_exit);
    }
    uint32_t *stub_end = p;
    p = blk_end;
    if (emit_overflow) { if (jit_dump) dbg("[translate %04lX failed late: overflow=%d]\n", (unsigned long)ip, emit_overflow); return 0; }

    uint32_t lin = (base + ip) & X86_LINMASK;
    struct jblk *b = &blks[nblk++];
    b->lin = lin; b->cs = cs;
    b->nseg = nseg;
    for (int i = 0; i < nseg; i++) {
        b->seg[i][0] = (base + seg[i][0]) & X86_LINMASK;
        b->seg[i][1] = (base + seg[i][1]) & X86_LINMASK;
        if (b->seg[i][1] <= b->seg[i][0]) b->seg[i][1] = b->seg[i][0] + 1;
        mark_code(b->seg[i][0], b->seg[i][1]);
    }
    b->code = blk_entry;
    b->end = blk_end;
    b->in = 0;
    b->dead = 0;
    cp = blk_end;
    sp_ = stub_end;
    { uint32_t k = mkey(lin, cs); b->hnext = map[k]; map[k] = b; }
    nstat_blocks++;
    if (jit_dump) {
        dbg("JIT %04lX:%04lX %d insns %d words @%p stub %p-%p:", (unsigned long)cs, (unsigned long)ip, k, (int)(p - blk_entry), (void *)blk_entry, (void *)sp_, (void *)stub_end);
        for (uint32_t *q = blk_entry; q < p; q++) dbg(" %08lX", (unsigned long)*q);
        dbg("\n");
    }
    return b;
}

/* the block for CS:eip, translating it if it is hot */
uint32_t jit_watch_eip = 0xFFFFFFFF;
static struct jblk *get_block(uint32_t cs, uint32_t eip)
{
    if (eip == jit_watch_eip) {
        dbg("[get_block %04lX: AX=%04X BX=%04X CX=%04X DX=%04X SI=%04X DI=%04X SP=%04X DS=%04X SS=%04X lf=%lX", (unsigned long)eip, rAX, rBX, rCX, rDX, rSI, rDI, rSP, cpu.sreg[SEG_DS], cpu.sreg[SEG_SS], (unsigned long)cpu.lf_op);
        dbg(" [DS:DI+4D6]=%04lX stack:", (unsigned long)rd16(cpu.sbase[SEG_DS] + ((rDI + 0x4D6) & 0xFFFF)));
        for (int i = -4; i < 4; i++) dbg(" %04lX", (unsigned long)rd16(cpu.sbase[SEG_SS] + ((rSP + i * 2) & 0xFFFF)));
        dbg("]\n");
    }
    uint32_t lin = (((uint32_t)cs << 4) + eip) & X86_LINMASK;
    struct jblk *b = lookup(lin, cs);
    if (b) return b;
    if (smcp[(lin >> 8) & 4095] && (int32_t)(cpu.icount - cool_until[(lin >> 8) & 4095]) < 0) return 0;
    uint32_t h = (lin ^ (lin >> 12)) & 4095;
    if (++hotc[h] < HOT) return 0;
    hotc[h] = 0;
    b = translate(cs, eip);
    if (!b) hotc[h] = -120;              /* (an instruction the translator refuses: interpret for a while) */
    return b;
}

static void link_slot(uint32_t *slot, struct jblk *to)
{
    if (nlink >= MAXLINK) return;
    struct link *l = &links[nlink++];
    l->slot = slot; l->stub = (uint32_t *)*slot; l->next = to->in;
    to->in = l;
    *slot = (uint32_t)to->code;              /* (data: the exit's literal) */
}

/* called from translated code (jit_exit_dyn): the next block for cpu.eip */
void *jit_dyn_lookup(void)
{
    if (irq_pending) return 0;
    struct jblk *b = get_block(cpu.sreg[SEG_CS], cpu.eip & 0xFFFF);
    return b ? b->code : 0;
}

int jit_seqlog;
int jit_try(uint32_t ip)
{
    struct jblk *b = get_block(cpu.sreg[SEG_CS], ip);
    if (!b) return 0;
    if (jit_seqlog) dbg("E%04lX AX%04X BX%04X CX%04X DX%04X SI%04X DI%04X SP%04X lf%02lX\n", (unsigned long)ip, rAX, rBX, rCX, rDX, rSI, rDI, rSP, (unsigned long)cpu.lf_op);
    uint32_t r = jit_enter(b->code);
    if (jit_seqlog && r >= 0x1000) {
        uint32_t *sl = (uint32_t *)r;
        dbg("  literal %p=%08lX\n", (void *)sl, (unsigned long)*sl);
    }
    if (jit_seqlog) dbg("X%04lX r%lX AX%04X BX%04X CX%04X DX%04X SI%04X DI%04X SP%04X lf%02lX\n", (unsigned long)cpu.eip, (unsigned long)(r >= 0x1000 ? 2 : r), rAX, rBX, rCX, rDX, rSI, rDI, rSP, (unsigned long)cpu.lf_op);
    if ((cpu.eip & 0xFFFF) == jit_watch_eip) dbg("[block %04lX exited r=%08lX eip=%04lX]\n", (unsigned long)ip, (unsigned long)r, (unsigned long)cpu.eip);
    for (int guard = 0; r >= 0x1000 && guard < 64; guard++) {
        /* a chain exit: link the slot to the target block, then go on */
        uint32_t fl = nstat_flush;
        struct jblk *t = get_block(cpu.sreg[SEG_CS], cpu.eip & 0xFFFF);
        if (!t) break;
        if (fl == nstat_flush) link_slot((uint32_t *)r, t);
        if (irq_pending) break;
        r = jit_enter(t->code);
    }
    return 1;
}

/* ------------------------------------------------------------ invalidation */

static int blk_has(const struct jblk *b, uint32_t lo, uint32_t hi)
{
    for (int s = 0; s < b->nseg; s++) if (b->seg[s][0] < hi && b->seg[s][1] > lo) return 1;
    return 0;
}

static void kill(struct jblk *b)
{
    b->dead = 1;
    for (struct link *l = b->in; l; l = l->next) *l->slot = (uint32_t)l->stub;
    b->in = 0;
    uint32_t k = mkey(b->lin, b->cs);
    for (struct jblk **pp = &map[k]; *pp; pp = &(*pp)->hnext)
        if (*pp == b) { *pp = b->hnext; break; }
    nstat_inval++;
}

static void recompute_page(uint32_t page)
{
    uint32_t lo = page << 8, hi = lo + 256;
    int any = 0;
    memset(codebits + (lo >> 3), 0, 32);
    for (int i = 0; i < nblk; i++) {
        struct jblk *b = &blks[i];
        if (b->dead) continue;
        for (int s = 0; s < b->nseg; s++) {
            if (b->seg[s][1] <= lo || b->seg[s][0] >= hi) continue;
            uint32_t st = b->seg[s][0] < lo ? lo : b->seg[s][0], en = b->seg[s][1] > hi ? hi : b->seg[s][1];
            for (uint32_t l = st; l < en; l++) codebits[l >> 3] |= 1 << (l & 7);
            any = 1;
        }
    }
    if (!any) wpt[page] &= ~(uintptr_t)1;
}

void jit_invalidate(uint32_t lin)
{
    if (!codebits || !(codebits[lin >> 3] & (1 << (lin & 7)))) return;
    if (jit_dump == 2) dbg("[inval %05lX by %04X:%04lX nblk=%d]\n", (unsigned long)lin, cpu.sreg[SEG_CS], (unsigned long)cpu.prev_eip, nblk);
    for (int i = 0; i < nblk; i++) {
        struct jblk *b = &blks[i];
        if (!b->dead && blk_has(b, lin, lin + 1)) { smc_killed(b); kill(b); }
    }
    recompute_page(lin >> 8);
    smc_hit = 1;
}

void jit_invalidate_range(uint32_t lin, uint32_t len)
{
    if (!codebits || !len) return;
    uint32_t end = lin + len;
    if (end > X86_MEMSIZE) end = X86_MEMSIZE;
    if (lin >= end) return;
    int touched = 0;
    for (uint32_t pg = lin >> 8; pg <= ((end - 1) >> 8); pg++) if (wpt[pg] & 1) { touched = 1; break; }
    /* new code or data loaded here (a file read, an EMS mapping): forget that the old
       contents rewrote themselves */
    for (uint32_t pg = lin >> 8; pg <= ((end - 1) >> 8); pg++) smcp[pg & 4095] = 0;
    if (!touched) return;
    for (int i = 0; i < nblk; i++) {
        struct jblk *b = &blks[i];
        if (!b->dead && blk_has(b, lin, end)) kill(b);          /* (file reads, EMS mapping: not counted as self-modifying) */
    }
    for (uint32_t pg = lin >> 8; pg <= ((end - 1) >> 8); pg++) if (wpt[pg] & 1) recompute_page(pg);
}

void jit_stats(char *buf, int n)
{
    if (!jit_enabled) { if (n) buf[0] = 0; return; }
    snprintf(buf, n, "%lu blocks, %lu KB of ARM code%s%s", (unsigned long)nstat_blocks,
             (unsigned long)((cp - cache + sp_ - stubs) * 4 / 1024), nstat_inval ? ", code rewritten" : "",
             nstat_flush ? ", cache refilled" : "");
}

/* ------------------------------------------------------------ the page's ELBOW view */

/* A descriptor of where things are, announced to the machine through the
   system board's ports FCh-FFh (ARCH.md 4.7) so the web page can show which
   x86 block runs and the ARM code made from it, by reading guest memory while
   its inspector is open.  Nothing here runs per block or per instruction. */
extern void _start(void);
extern char __image_end[];
static uint32_t edesc[30], edesc_prev;
static int edesc_on;
#define BOARD_PORT(n) (*(volatile uint8_t *)(0x10000000u + (n)))   /* (armdos.h's I/O window; its BL macro clashes here) */
static void desc_port(uint32_t a)
{
    for (int i = 0; i < 4; i++) BOARD_PORT(0xFC + i) = (uint8_t)(a >> (i * 8));
}
void elbow_desc_announce(void)
{
    uint32_t *d = edesc;
    d[0] = 0x57424C45u;                     /* "ELBW" */
    d[1] = 1;                               /* version */
    d[2] = sizeof edesc / 4;
    d[3] = (uint32_t)&cpu;
    d[4] = (uint32_t)&mem;
    d[5] = (uint32_t)rpt;
    d[6] = (uint32_t)&blks;
    d[7] = (uint32_t)&nblk;
    d[8] = sizeof(struct jblk);
    d[9] = offsetof(struct jblk, lin);
    d[10] = offsetof(struct jblk, cs);
    d[11] = offsetof(struct jblk, seg);
    d[12] = offsetof(struct jblk, code);
    d[13] = offsetof(struct jblk, end);
    d[14] = offsetof(struct jblk, nseg);
    d[15] = offsetof(struct jblk, dead);
    d[16] = (uint32_t)cache;                /* block area [16], [17); exit stubs [17], [18) */
    d[17] = cache ? (uint32_t)(cache + BLK_WORDS) : 0;
    d[18] = cache ? (uint32_t)(cache + CODE_WORDS) : 0;
    d[19] = (uint32_t)&cp;
    d[20] = offsetof(X86, eip);
    d[21] = offsetof(X86, sreg);
    d[22] = (uint32_t)wpt;
    d[23] = (uint32_t)&irq_pending;
    d[24] = (uint32_t)_start;               /* ELBOW's image (code, then data) */
    d[25] = (uint32_t)__image_end;
    d[26] = (uint32_t)&jit_enabled;
    d[27] = (uint32_t)&nstat_blocks;
    d[28] = (uint32_t)&nstat_flush;
    d[29] = MAXSEG;
    /* (an ELBOW started from inside an x86 program's ARM child gives the port back) */
    edesc_prev = BOARD_PORT(0xFC) | BOARD_PORT(0xFD) << 8 | BOARD_PORT(0xFE) << 16 | (uint32_t)BOARD_PORT(0xFF) << 24;
    if (edesc_prev == 0xFFFFFFFFu) edesc_prev = 0;
    desc_port((uint32_t)edesc);
    edesc_on = 1;
}
void elbow_desc_clear(void)
{
    if (!edesc_on) return;
    edesc_on = 0;
    edesc[0] = 0;
    desc_port(edesc_prev);
}
