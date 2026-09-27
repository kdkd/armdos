/*
 * cpu.c - the x86 CPU: an 8086/80186/80286 real-mode interpreter with the
 * 80386's real-mode extensions (operand/address size prefixes, FS/GS, 0F xx
 * instructions: Jcc near, SETcc, MOVZX/MOVSX, BT*, BSF/BSR, SHLD/SHRD, IMUL
 * r,rm, LSS/LFS/LGS, ...) and the few 486 ones (BSWAP, XADD, CMPXCHG).
 * No FPU: escape opcodes are decoded and ignored (FNSTSW leaves memory
 * alone, so the usual "is there an 8087?" test answers no).  It identifies
 * as a 386 (FLAGS bits 12-14 writable in real mode, bit 15 zero, no AC).
 *
 * Flags are lazy: arithmetic records (op, a, b, result) and the flags are
 * computed when something reads them (get_flags, Jcc, PUSHF, ...).
 *
 * Copyright (c) 2026 Europa Micro Systems. Part of ARM-DOS (apps/x86).
 */
#include "x86.h"
#include "jit.h"

/* page-aligned: written on every instruction, it must not share a 4 KB page (or a
 * 128-byte line) with ARM code the emulator has compiled */
X86 cpu __attribute__((aligned(4096)));
uint32_t *ophist;
#define RING 64
int ring_on;
static uint32_t ring[RING * 4], ring_pos;
void ring_dump(void)
{
    for (uint32_t i = 0; i < RING; i++) {
        uint32_t k = (ring_pos + i) & (RING - 1);
        dbg("  %04lX:%04lX f=%08lX AX=%04lX SI=%04lX SP=%04lX op=%02lX %02lX\n", (unsigned long)(ring[k*4] >> 16), (unsigned long)(ring[k*4] & 0xFFFF), (unsigned long)ring[k*4+1],
            (unsigned long)(ring[k*4+2] & 0xFFFF), (unsigned long)(ring[k*4+2] >> 16), (unsigned long)(ring[k*4+3] & 0xFFFF), (unsigned long)((ring[k*4+3] >> 16) & 0xFF), (unsigned long)(ring[k*4+3] >> 24));
    }
}

static const uint32_t szmask[3] = { 0xFFu, 0xFFFFu, 0xFFFFFFFFu };
static const uint32_t szsign[3] = { 0x80u, 0x8000u, 0x80000000u };

static uint8_t parity[256];     /* F_PF if even parity */

/* ------------------------------------------------------------ flags */

static inline __attribute__((always_inline)) void setlf(int kind, int sz, uint32_t a, uint32_t b, uint32_t r)
{
    cpu.lf_op = kind | (sz << 4);
    cpu.lf_a = a; cpu.lf_b = b; cpu.lf_res = r;
}

/* the carry flag alone (cheap for ADC/SBB/RCL/...) */
static uint32_t cf_now(void)
{
    uint32_t a = cpu.lf_a, b = cpu.lf_b, r = cpu.lf_res;
    switch (cpu.lf_op & 15) {
    case LF_NONE: case LF_INC: case LF_DEC: return cpu.flags & F_CF;
    case LF_ADD: return r < a;
    case LF_ADC: return r <= a;
    case LF_SUB: return a < b;
    case LF_SBB: return a <= b;
    case LF_SHL: return (a >> ((8 << (cpu.lf_op >> 4)) - b)) & 1;
    case LF_SHR: return (a >> (b - 1)) & 1;
    case LF_SAR: { int sh = 32 - (8 << (cpu.lf_op >> 4)); return ((uint32_t)((int32_t)(a << sh) >> sh) >> (b - 1)) & 1; }
    default: return 0;
    }
}

uint32_t get_flags(void)
{
    uint32_t op = cpu.lf_op;
    if (!op) return cpu.flags;
    int sz = op >> 4;
    uint32_t a = cpu.lf_a, b = cpu.lf_b, r = cpu.lf_res, sign = szsign[sz];
    uint32_t f = cpu.flags & ~F_ARITH;
    f |= cf_now();
    f |= parity[r & 0xFF];
    if (r == 0) f |= F_ZF;
    if (r & sign) f |= F_SF;
    switch (op & 15) {
    case LF_ADD: case LF_ADC: case LF_INC:
        if ((a ^ r) & (b ^ r) & sign) f |= F_OF;
        f |= (a ^ b ^ r) & F_AF;
        break;
    case LF_SUB: case LF_SBB: case LF_DEC:
        if ((a ^ b) & (a ^ r) & sign) f |= F_OF;
        f |= (a ^ b ^ r) & F_AF;
        break;
    case LF_SHL: if (((r & sign) != 0) ^ (f & F_CF)) f |= F_OF; break;
    case LF_SHR: if (a & sign) f |= F_OF; break;
    default: break;                 /* LOGIC, SAR: OF = AF = 0 */
    }
    cpu.flags = f;
    cpu.lf_op = LF_NONE;
    return f;
}

static void pend_clear(uint32_t bit)
{
    uint32_t s, t;
    __asm__ volatile("mrs %0, cpsr\n\torr %1, %0, #0x80\n\tmsr cpsr_c, %1" : "=r"(s), "=r"(t) :: "memory");
    irq_pending &= ~bit;
    __asm__ volatile("msr cpsr_c, %0" :: "r"(s) : "memory");
}

void set_flags(uint32_t f)
{
    cpu.flags = (f & 0x7FD5u) | 2;
    cpu.lf_op = LF_NONE;
    if (f & F_TF) pend_set(PEND_TF);
}

/* flags a native service has set (only these go back into an IRET frame) */
uint32_t hle_fset;
void set_cf(int c) { uint32_t f = get_flags(); cpu.flags = c ? f | F_CF : f & ~F_CF; hle_fset |= F_CF; }
void set_zf(int z) { uint32_t f = get_flags(); cpu.flags = z ? f | F_ZF : f & ~F_ZF; hle_fset |= F_ZF; }

static int cond_slow(int cc);
/* condition codes 0-15 (Jcc/SETcc low nibble) */
static inline __attribute__((always_inline)) int cond(int cc)
{
    uint32_t op = cpu.lf_op;
    int kind = op & 15;
    if (kind == LF_SUB) {                /* CMP / SUB: straight from the operands */
        uint32_t a = cpu.lf_a, b = cpu.lf_b;
        static const uint8_t shtab[3] = { 24, 16, 0 };
        int sh = shtab[op >> 4];
        int32_t sa = (int32_t)(a << sh), sb = (int32_t)(b << sh);
        switch (cc) {
        case 2: return a < b;   case 3: return a >= b;
        case 4: return a == b;  case 5: return a != b;
        case 6: return a <= b;  case 7: return a > b;
        case 12: return sa < sb; case 13: return sa >= sb;
        case 14: return sa <= sb; case 15: return sa > sb;
        }
    } else if (kind == LF_LOGIC) {
        uint32_t r = cpu.lf_res, s = r & szsign[op >> 4];
        switch (cc) {
        case 0: case 2: return 0;  case 1: case 3: return 1;
        case 4: case 6: return r == 0;  case 5: case 7: return r != 0;
        case 8: case 12: return s != 0;  case 9: case 13: return s == 0;
        case 14: return r == 0 || s;  case 15: return r != 0 && !s;
        }
    }
    return cond_slow(cc);
}

static int cond_slow(int cc)
{
    uint32_t f = get_flags();
    int v;
    switch (cc >> 1) {
    case 0: v = (f & F_OF) != 0; break;
    case 1: v = f & F_CF; break;
    case 2: v = (f & F_ZF) != 0; break;
    case 3: v = (f & (F_CF | F_ZF)) != 0; break;
    case 4: v = (f & F_SF) != 0; break;
    case 5: v = (f & F_PF) != 0; break;
    case 6: v = !(f & F_SF) != !(f & F_OF); break;
    default: v = (f & F_ZF) || (!(f & F_SF) != !(f & F_OF)); break;
    }
    return (cc & 1) ? !v : v;
}

/* ------------------------------------------------------------ registers */

static inline __attribute__((always_inline)) uint32_t getreg(int sz, int i)
{
    if (sz == 1) return cpu.r.w[i * 2];
    if (sz == 0) return cpu.r.b[((i & 3) << 2) | (i >> 2)];
    return cpu.r.e[i];
}
static inline __attribute__((always_inline)) void setreg(int sz, int i, uint32_t v)
{
    if (sz == 1) cpu.r.w[i * 2] = v;
    else if (sz == 0) cpu.r.b[((i & 3) << 2) | (i >> 2)] = v;
    else cpu.r.e[i] = v;
}
static inline __attribute__((always_inline)) uint32_t rdm(int sz, uint32_t lin) { return sz == 1 ? rd16(lin) : sz == 0 ? rd8(lin) : rd32(lin); }
static inline __attribute__((always_inline)) void wrm(int sz, uint32_t lin, uint32_t v) { if (sz == 1) wr16(lin, v); else if (sz == 0) wr8(lin, v); else wr32(lin, v); }

void set_sreg(int s, uint16_t v) { cpu.sreg[s] = v; cpu.sbase[s] = (uint32_t)v << 4; }

void cpu_push16(uint32_t v) { rSP -= 2; wr16(cpu.sbase[SEG_SS] + rSP, v); }
uint32_t cpu_pop16(void) { uint32_t v = rd16(cpu.sbase[SEG_SS] + rSP); rSP += 2; return v; }
static inline void push32(uint32_t v) { rSP -= 4; wr32(cpu.sbase[SEG_SS] + rSP, v); }
static inline uint32_t pop32(void) { uint32_t v = rd32(cpu.sbase[SEG_SS] + rSP); rSP += 4; return v; }

void cpu_far_jump(uint16_t cs, uint32_t ip) { set_sreg(SEG_CS, cs); cpu.eip = ip; }

/* /FARLOG: a ring of the last far transfers, dumped (once) when one lands in
   low memory with CS < 100h - a debugging aid for wild far returns */
int farlog_on;
static struct { uint16_t k, fcs, fip, tcs, tip, sp; } farring[64];
static unsigned farpos, farbang;
void farlog(uint32_t k, uint32_t fcs, uint32_t fip, uint32_t tcs, uint32_t tip)
{
    unsigned i = farpos++ & 63;
    farring[i].k = k; farring[i].fcs = fcs; farring[i].fip = fip; farring[i].tcs = tcs; farring[i].tip = tip; farring[i].sp = rSP;
    if (tcs < 0x100 && tip >= 0x500 && !farbang) {
        farbang = 1;
        dbg("[FARLOG: %04lX:%04lX -> %04lX:%04lX; the last far transfers (k: 1 CALLF 2 JMPF 3 RETF 4 IRET 5 INT, +16 translated):\n", (unsigned long)fcs, (unsigned long)fip, (unsigned long)tcs, (unsigned long)tip);
        for (unsigned j = 0; j < 64; j++) {
            unsigned q = (farpos + j) & 63;
            if (!farring[q].k) continue;
            dbg("  %2u %04X:%04X -> %04X:%04X SP=%04X\n", farring[q].k, farring[q].fcs, farring[q].fip, farring[q].tcs, farring[q].tip, farring[q].sp);
        }
        dbg("]\n");
    }
}
void jh_farlog(uint32_t k, uint32_t fcs, uint32_t fip) { farlog(k, fcs, fip, cpu.sreg[SEG_CS], cpu.eip & 0xFFFF); }
#define FARLOG(k, fcs) do { if (__builtin_expect(farlog_on, 0)) farlog(k, fcs, pip, cpu.sreg[SEG_CS], ip); } while (0)

/* INT n through the x86 IVT (exceptions, IRQs, INT xx).  A vector that still
 * points at our stub is serviced natively at once, without the round trip. */
static int int_direct;               /* set while a native handler runs for a direct INT */
void cpu_interrupt(int n, uint32_t ret_ip)
{
    uint32_t v = rd32(n * 4);
    if (v == HLE_VEC(n)) {
        cpu.eip = ret_ip;
        int_direct = 1;
        int r = hle_int(n);
        int_direct = 0;
        if (r == HLE_RETRY) cpu.eip = cpu.prev_eip;
        return;
    }
    uint32_t f = get_flags();
    cpu_push16(f);
    cpu_push16(cpu.sreg[SEG_CS]);
    cpu_push16(ret_ip);
    cpu.flags = f & ~(F_IF | F_TF);
    uint32_t ocs = cpu.sreg[SEG_CS];
    cpu_far_jump(v >> 16, v & 0xFFFF);
    if (__builtin_expect(farlog_on, 0)) farlog(5 | n << 8, ocs, ret_ip, v >> 16, v & 0xFFFF);
}

int hle_in_direct(void) { return int_direct; }

/* ------------------------------------------------------------ ALU */

static inline __attribute__((always_inline)) uint32_t alu(int op, uint32_t a, uint32_t b, int sz)
{
    uint32_t m = szmask[sz], r, c;
    switch (op) {
    case 0: r = (a + b) & m; setlf(LF_ADD, sz, a, b, r); return r;
    case 1: r = a | b; setlf(LF_LOGIC, sz, 0, 0, r); return r;
    case 2: c = cf_now(); r = (a + b + c) & m; setlf(c ? LF_ADC : LF_ADD, sz, a, b, r); return r;
    case 3: c = cf_now(); r = (a - b - c) & m; setlf(c ? LF_SBB : LF_SUB, sz, a, b, r); return r;
    case 4: r = a & b; setlf(LF_LOGIC, sz, 0, 0, r); return r;
    case 5: case 7: r = (a - b) & m; setlf(LF_SUB, sz, a, b, r); return r;
    default: r = a ^ b; setlf(LF_LOGIC, sz, 0, 0, r); return r;
    }
}

static inline __attribute__((always_inline)) uint32_t inc_dec(int dec, uint32_t a, int sz)
{
    uint32_t cf = cf_now();
    cpu.flags = (cpu.flags & ~F_CF) | cf;     /* the other arithmetic bits come from the new lazy op */
    uint32_t r = (dec ? a - 1 : a + 1) & szmask[sz];
    setlf(dec ? LF_DEC : LF_INC, sz, a, 1, r);
    return r;
}

/* eager flags from a result (shifts, multiplies, BCD) */
static inline void szp(uint32_t r, int sz, uint32_t other)
{
    r &= szmask[sz];
    /* every arithmetic flag is set here, so the pending lazy ones need not be
       worked out first: drop them (cpu.flags holds the others) */
    cpu.lf_op = LF_NONE;
    uint32_t f = (cpu.flags & ~F_ARITH) | other | parity[r & 0xFF];
    if (!r) f |= F_ZF;
    if (r & szsign[sz]) f |= F_SF;
    cpu.flags = f;
}

/* group 2: ROL ROR RCL RCR SHL SHR SAL SAR */
static uint32_t shift(int op, uint32_t v, uint32_t cnt, int sz)
{
    int bits = 8 << sz;
    uint32_t m = szmask[sz], sign = szsign[sz];
    cnt &= 31;
    if (!cnt) return v;
    uint32_t f = get_flags() & ~(F_CF | F_OF);
    uint32_t r = v, cf = 0;
    switch (op) {
    case 0: {                                   /* ROL */
        int c = cnt & (bits - 1);
        r = c ? ((v << c) | (v >> (bits - c))) & m : v;
        cf = r & 1;
        if (((r & sign) != 0) ^ cf) f |= F_OF;
        cpu.flags = f | cf;
        return r;
    }
    case 1: {                                   /* ROR */
        int c = cnt & (bits - 1);
        r = c ? ((v >> c) | (v << (bits - c))) & m : v;
        cf = (r & sign) != 0;
        if (((r ^ (r << 1)) & sign)) f |= F_OF;
        cpu.flags = f | cf;
        return r;
    }
    case 2: {                                   /* RCL */
        int c = sz == 2 ? (int)cnt : (int)(cnt % (bits + 1));
        cf = get_flags() & F_CF;
        for (int i = 0; i < c; i++) { uint32_t nc = (r & sign) != 0; r = ((r << 1) | cf) & m; cf = nc; }
        f = cpu.flags & ~(F_CF | F_OF);
        if (((r & sign) != 0) ^ cf) f |= F_OF;
        cpu.flags = f | cf;
        return r;
    }
    case 3: {                                   /* RCR */
        int c = sz == 2 ? (int)cnt : (int)(cnt % (bits + 1));
        cf = get_flags() & F_CF;
        f = cpu.flags & ~(F_CF | F_OF);
        for (int i = 0; i < c; i++) { uint32_t nc = r & 1; r = (r >> 1) | (cf ? sign : 0); cf = nc; }
        if ((r ^ (r << 1)) & sign) f |= F_OF;
        cpu.flags = f | cf;
        return r;
    }
    case 4: case 6:                              /* SHL/SAL */
        if (cnt <= (uint32_t)bits) cf = (sz == 2 ? (cnt == 32 ? v & 1 : (v >> (32 - cnt)) & 1) : (v << cnt) >> bits & 1);
        r = cnt >= 32 ? 0 : (v << cnt) & m;
        cpu.flags = f;
        szp(r, sz, cf | ((((r & sign) != 0) ^ cf) ? F_OF : 0));
        return r;
    case 5:                                      /* SHR */
        cf = (v >> (cnt - 1)) & 1;
        r = v >> cnt;
        cpu.flags = f;
        szp(r, sz, cf | ((v & sign) ? F_OF : 0));
        return r;
    default: {                                   /* SAR */
        int32_t sv = (int32_t)(v << (32 - bits)) >> (32 - bits);
        cf = (sv >> (cnt - 1)) & 1;
        r = (uint32_t)(sv >> cnt) & m;
        cpu.flags = f;
        szp(r, sz, cf);
        return r;
    }
    }
}

/* SHLD / SHRD */
static uint32_t shxd(int right, uint32_t dst, uint32_t src, uint32_t cnt, int sz)
{
    int bits = 8 << sz;
    cnt &= 31;
    if (!cnt) return dst;
    uint32_t m = szmask[sz], sign = szsign[sz], r, cf;
    uint64_t w;
    if (sz == 1) {
        if (right) { w = ((uint64_t)src << 16) | dst; w |= (uint64_t)dst << 32; r = (w >> cnt) & m; cf = (w >> (cnt - 1)) & 1; }
        else { w = ((uint64_t)dst << 32) | ((uint64_t)src << 16) | dst; r = (w >> (32 - cnt)) & m; cf = (w >> (32 - cnt + 16)) & 1; }
    } else {
        if (right) { w = ((uint64_t)src << 32) | dst; r = (uint32_t)(w >> cnt); cf = (w >> (cnt - 1)) & 1; }
        else { w = ((uint64_t)dst << 32) | src; r = (uint32_t)(w >> (32 - cnt)); cf = (w >> (64 - cnt)) & 1; }
    }
    (void)bits;
    uint32_t of = ((r ^ dst) & sign) ? F_OF : 0;
    szp(r, sz, cf | of);
    return r;
}

/* DIV / IDIV: returns 0 on success, -1 for a divide error */
static int divide(int sgn, uint32_t d, int sz)
{
    if (!d) return -1;
    if (sz == 0) {
        uint32_t n = rAX;
        if (!sgn) { uint32_t q = n / d; if (q > 0xFF) return -1; rAL = q; rAH = n % d; }
        else {
            int32_t sn = (int16_t)n, sd = (int8_t)d, q = sn / sd;
            if (q > 127 || q < -128) return -1;
            rAL = q; rAH = sn % sd;
        }
    } else if (sz == 1) {
        uint32_t n = ((uint32_t)rDX << 16) | rAX;
        if (!sgn) { uint32_t q = n / d; if (q > 0xFFFF) return -1; rAX = q; rDX = n % d; }
        else {
            int32_t sn = (int32_t)n, sd = (int16_t)d;
            if (sn == INT32_MIN && sd == -1) return -1;
            int32_t q = sn / sd;
            if (q > 32767 || q < -32768) return -1;
            rAX = q; rDX = sn % sd;
        }
    } else {
        uint64_t n = ((uint64_t)rEDX << 32) | rEAX;
        if (!sgn) { uint64_t q = n / d; if (q > 0xFFFFFFFFu) return -1; rEAX = q; rEDX = n % d; }
        else {
            int64_t sn = (int64_t)n, sd = (int32_t)d;
            if (sn == INT64_MIN && sd == -1) return -1;
            int64_t q = sn / sd;
            if (q > INT32_MAX || q < INT32_MIN) return -1;
            rEAX = q; rEDX = sn % sd;
        }
    }
    return 0;
}

/* multiply flags: CF = OF = the upper half matters */
static inline void mulflags(int wide, uint32_t lowres, int sz)
{
    szp(lowres, sz, wide ? (F_CF | F_OF) : 0);
}

/* ------------------------------------------------------------ strings */

static inline uint32_t sidx(int a32, int r) { return a32 ? cpu.r.e[r] : cpu.r.w[r * 2]; }
static inline void sadv(int a32, int r, int32_t d) { if (a32) cpu.r.e[r] += d; else cpu.r.w[r * 2] += d; }

/* ------------------------------------------------------------ init */

void cpu_reset(void)
{
    memset(&cpu, 0, sizeof cpu);
    for (int i = 0; i < 256; i++) {
        int b = 0;
        for (int k = 0; k < 8; k++) b ^= (i >> k) & 1;
        parity[i] = b ? 0 : F_PF;
    }
    cpu.flags = 0x0202;
    cpu.cr0 = 0x00000010;
}

void cpu_unsupported_op(const uint8_t *p);

/* ------------------------------------------------------------ the loop */

#define F8()   (code[ip++])
#define F16()  (ip += 2, (uint32_t)code[ip - 2] | ((uint32_t)code[ip - 1] << 8))
#define F32()  (ip += 4, (uint32_t)code[ip - 4] | ((uint32_t)code[ip - 3] << 8) | ((uint32_t)code[ip - 2] << 16) | ((uint32_t)code[ip - 1] << 24))
#define FIMM(sz) ((sz) == 0 ? F8() : (sz) == 1 ? F16() : F32())
#define SYNC()  (cpu.eip = ip, cpu.prev_eip = pip)
#define RELOAD() (ip = cpu.eip, code = mem + cpu.sbase[SEG_CS])
#define SEGB(def) (ovr >= 0 ? cpu.sbase[ovr] : cpu.sbase[def])
#define RM_READ(sz)    (isreg ? getreg(sz, modrm & 7) : rdm(sz, lin))
#define RM_WRITE(sz,v) do { if (isreg) setreg(sz, modrm & 7, v); else wrm(sz, lin, v); } while (0)
#define PUSHV(v) do { if (o32) push32(v); else cpu_push16(v); } while (0)
#define POPV()   (o32 ? pop32() : cpu_pop16())
#define EXC(n)   do { ip = pip; SYNC(); cpu_interrupt(n, ip); RELOAD(); goto next; } while (0)

#define CAT2(a, b) a##b
#define CAT(a, b) CAT2(a, b)
#define MODRM() do { modrm = F8(); if (modrm >= 0xC0) isreg = 1; \
        else { isreg = 0; mret = &&CAT(ea_ret_, __LINE__); goto do_ea; CAT(ea_ret_, __LINE__): ; } } while (0)

/* the string instructions, one iteration or a REP loop */
static int string_op(int op, int sz, int a32, int ovr, int rep);

int cpu_run(void)
{
    const uint8_t *code = mem + cpu.sbase[SEG_CS];
    uint32_t ip = cpu.eip;
    uint32_t modrm = 0, lin = 0, eaoff = 0, ninsn = 0;
    int isreg = 0, tf_step = 0;
    uint32_t pip = ip;
    void *mret = 0;

    for (;;) {
        /* SMC cooldowns consult this counter while cpu_run is still active.
           Publish in batches so a long-running program can leave cooldown. */
        if (ninsn >= 1024) { cpu.icount += ninsn; ninsn = 0; }
        if (__builtin_expect(irq_pending != 0, 0)) {
            if (irq_pending & PEND_DEBUG) {
            if (__builtin_expect(ophist != 0, 0)) { uint32_t b0 = code[ip]; ophist[b0 == 0x0F ? 256u + code[ip + 1] : (b0 == 0x26 || b0 == 0x2E || b0 == 0x36 || b0 == 0x3E || b0 == 0x66 || b0 == 0xF3 || b0 == 0xF2) ? 512u + b0 : b0]++; }
                if (__builtin_expect(ring_on, 0)) {
                    uint32_t k = ring_pos++ & (RING - 1);
                    ring[k * 4] = ((uint32_t)cpu.sreg[SEG_CS] << 16) | ip;
                    ring[k * 4 + 1] = cpu.lf_op == LF_NONE ? cpu.flags : 0x80000000u | cpu.lf_op;
                    ring[k * 4 + 2] = rAX | ((uint32_t)rSI << 16);
                    ring[k * 4 + 3] = rSP | ((uint32_t)code[ip] << 16) | ((uint32_t)code[ip + 1] << 24);
                }
                if (__builtin_expect(opt_trace > 1, 0) && opt_trace < 200000) {
                    opt_trace++;
                    dbg("%04X:%04X %02X %02X %02X AX=%04X BX=%04X CX=%04X DX=%04X SP=%04X BP=%04X SI=%04X DI=%04X F=%04X\n", cpu.sreg[SEG_CS], ip, code[ip], code[ip + 1], code[ip + 2],
                                rAX, rBX, rCX, rDX, rSP, rBP, rSI, rDI, get_flags());
                }
            }
            if (irq_pending & PEND_INHIBIT) { pend_clear(PEND_INHIBIT); goto noirq; }
            if (irq_pending & PEND_STOP) {
                pend_clear(PEND_STOP);
                cpu.icount += ninsn; SYNC(); return 1;
            }
            if (irq_pending & PEND_TF) {
                if (tf_step) { tf_step = 0; SYNC(); cpu_interrupt(1, ip); RELOAD(); }
                if (cpu.flags & F_TF) tf_step = 1; else pend_clear(PEND_TF);
            }
            if ((irq_pending & ~(PEND_TF | PEND_DEBUG)) && (get_flags() & F_IF)) { SYNC(); irq_deliver(); RELOAD(); continue; }
        }
    noirq:
#ifdef X86_JIT
        /* translated blocks leave at once while anything is pending, so an
           interrupt that cannot be taken yet (IF=0: a CLI'd retrace poll, as
           in ZZT) must not send us round jit_try without progress */
        if (jit_enabled && !irq_pending) {
            SYNC();
            if (jit_try(ip)) { RELOAD(); if (cpu.stop) { cpu.icount += ninsn; SYNC(); return 1; } continue; }
        }
#endif
        pip = ip;
        ninsn++;
        int o32 = 0, a32 = 0, ovr = -1, rep = 0;
        uint32_t op;
    prefix:
        op = F8();
        switch (op) {
        /* ---- prefixes */
        case 0x26: ovr = SEG_ES; goto prefix;
        case 0x2E: ovr = SEG_CS; goto prefix;
        case 0x36: ovr = SEG_SS; goto prefix;
        case 0x3E: ovr = SEG_DS; goto prefix;
        case 0x64: ovr = SEG_FS; goto prefix;
        case 0x65: ovr = SEG_GS; goto prefix;
        case 0x66: o32 = 1; goto prefix;
        case 0x67: a32 = 1; goto prefix;
        case 0xF0: goto prefix;                              /* LOCK */
        case 0xF2: case 0xF3: rep = op; goto prefix;

        /* ---- ALU r/m, reg (00-3F) */
        case 0x00: case 0x01: case 0x02: case 0x03: case 0x08: case 0x09: case 0x0A: case 0x0B:
        case 0x10: case 0x11: case 0x12: case 0x13: case 0x18: case 0x19: case 0x1A: case 0x1B:
        case 0x20: case 0x21: case 0x22: case 0x23: case 0x28: case 0x29: case 0x2A: case 0x2B:
        case 0x30: case 0x31: case 0x32: case 0x33: case 0x38: case 0x39: case 0x3A: case 0x3B: {
            int sz = (op & 1) ? (o32 ? 2 : 1) : 0, aop = (op >> 3) & 7;
            MODRM();
            int r = (modrm >> 3) & 7;
            if (op & 2) {
                uint32_t v = alu(aop, getreg(sz, r), RM_READ(sz), sz);
                if (aop != 7) setreg(sz, r, v);
            } else {
                uint32_t v = alu(aop, RM_READ(sz), getreg(sz, r), sz);
                if (aop != 7) RM_WRITE(sz, v);
            }
            break;
        }
        case 0x04: case 0x0C: case 0x14: case 0x1C: case 0x24: case 0x2C: case 0x34: case 0x3C: {
            uint32_t v = alu((op >> 3) & 7, rAL, F8(), 0);
            if (op != 0x3C) rAL = v;
            break;
        }
        case 0x05: case 0x0D: case 0x15: case 0x1D: case 0x25: case 0x2D: case 0x35: case 0x3D: {
            int sz = o32 ? 2 : 1;
            uint32_t v = alu((op >> 3) & 7, getreg(sz, 0), FIMM(sz), sz);
            if (op != 0x3D) setreg(sz, 0, v);
            break;
        }
        case 0x06: case 0x0E: case 0x16: case 0x1E: PUSHV(cpu.sreg[op >> 3]); break;
        case 0x07: case 0x17: case 0x1F:
            set_sreg(op >> 3, POPV());
            if (op == 0x17) pend_set(PEND_INHIBIT);
            break;
        case 0x27: {                                         /* DAA */
            uint32_t f = get_flags(), al = rAL, cf = f & F_CF, nf = f & ~(F_CF | F_AF);
            if ((al & 0x0F) > 9 || (f & F_AF)) { al += 6; nf |= F_AF; }
            if (rAL > 0x99 || cf) { al += 0x60; nf |= F_CF; }
            rAL = al; cpu.flags = nf; szp(rAL, 0, cpu.flags & (F_CF | F_AF));
            break;
        }
        case 0x2F: {                                         /* DAS */
            uint32_t f = get_flags(), old = rAL, al = old, nf = f & ~(F_CF | F_AF);
            if ((al & 0x0F) > 9 || (f & F_AF)) { al -= 6; nf |= F_AF; if (old < 6) nf |= F_CF; }
            if (old > 0x99 || (f & F_CF)) { al -= 0x60; nf |= F_CF; }
            rAL = al; cpu.flags = nf; szp(rAL, 0, cpu.flags & (F_CF | F_AF));
            break;
        }
        case 0x37: case 0x3F: {                              /* AAA / AAS */
            uint32_t f = get_flags() & ~(F_CF | F_AF);
            if ((rAL & 0x0F) > 9 || (get_flags() & F_AF)) {
                if (op == 0x37) { rAX += 0x106; } else { rAX -= 6; rAH -= 1; }
                f |= F_CF | F_AF;
            }
            rAL &= 0x0F;
            cpu.flags = f;
            szp(rAL, 0, f & (F_CF | F_AF));
            break;
        }

        /* ---- INC/DEC/PUSH/POP reg */
        case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47: {
            int sz = o32 ? 2 : 1; setreg(sz, op & 7, inc_dec(0, getreg(sz, op & 7), sz)); break;
        }
        case 0x48: case 0x49: case 0x4A: case 0x4B: case 0x4C: case 0x4D: case 0x4E: case 0x4F: {
            int sz = o32 ? 2 : 1; setreg(sz, op & 7, inc_dec(1, getreg(sz, op & 7), sz)); break;
        }
        case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: case 0x55: case 0x56: case 0x57:
            if (o32) push32(cpu.r.e[op & 7]); else cpu_push16(cpu.r.w[(op & 7) * 2]);
            break;
        case 0x58: case 0x59: case 0x5A: case 0x5B: case 0x5C: case 0x5D: case 0x5E: case 0x5F:
            if (o32) { uint32_t v = pop32(); cpu.r.e[op & 7] = v; }
            else { uint32_t v = cpu_pop16(); cpu.r.w[(op & 7) * 2] = v; }
            break;
        case 0x60: {                                         /* PUSHA */
            if (o32) { uint32_t sp = rESP; for (int i = 0; i < 8; i++) push32(i == 4 ? sp : cpu.r.e[i]); }
            else { uint32_t sp = rSP; for (int i = 0; i < 8; i++) cpu_push16(i == 4 ? sp : cpu.r.w[i * 2]); }
            break;
        }
        case 0x61:                                           /* POPA */
            for (int i = 7; i >= 0; i--) {
                if (o32) { uint32_t v = pop32(); if (i != 4) cpu.r.e[i] = v; }
                else { uint32_t v = cpu_pop16(); if (i != 4) cpu.r.w[i * 2] = v; }
            }
            break;
        case 0x62: {                                         /* BOUND */
            MODRM();
            if (isreg) EXC(6);
            if (o32) {
                int32_t v = cpu.r.e[(modrm >> 3) & 7], lo = rd32(lin), hi = rd32(lin + 4);
                if (v < lo || v > hi) EXC(5);
            } else {
                int16_t v = cpu.r.w[((modrm >> 3) & 7) * 2], lo = rd16(lin), hi = rd16(lin + 2);
                if (v < lo || v > hi) EXC(5);
            }
            break;
        }
        case 0x68: { uint32_t v = FIMM(o32 ? 2 : 1); PUSHV(v); break; }
        case 0x6A: { uint32_t v = (uint32_t)(int32_t)(int8_t)F8(); PUSHV(o32 ? v : v & 0xFFFF); break; }
        case 0x69: case 0x6B: {                              /* IMUL r, r/m, imm */
            int sz = o32 ? 2 : 1;
            MODRM();
            int32_t a = sz == 2 ? (int32_t)RM_READ(2) : (int16_t)RM_READ(1);
            int32_t b = op == 0x6B ? (int8_t)F8() : sz == 2 ? (int32_t)F32() : (int16_t)F16();
            int64_t p = (int64_t)a * b;
            uint32_t r = (uint32_t)p & szmask[sz];
            int wide = sz == 2 ? p != (int32_t)p : p != (int16_t)p;
            setreg(sz, (modrm >> 3) & 7, r);
            mulflags(wide, r, sz);
            break;
        }
        case 0x6C: case 0x6D: case 0x6E: case 0x6F: case 0xA4: case 0xA5: case 0xA6: case 0xA7:
        case 0xAA: case 0xAB: case 0xAC: case 0xAD: case 0xAE: case 0xAF: {
            int sz = (op & 1) ? (o32 ? 2 : 1) : 0;
            SYNC();
            if (string_op(op, sz, a32, ovr, rep)) { ip = pip; }   /* interrupted REP: resume later */
            break;
        }

        /* ---- Jcc short */
        case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75: case 0x76: case 0x77:
        case 0x78: case 0x79: case 0x7A: case 0x7B: case 0x7C: case 0x7D: case 0x7E: case 0x7F: {
            int32_t d = (int8_t)F8();
            if (cond(op & 15)) ip = (ip + d) & (o32 ? 0xFFFFFFFFu : 0xFFFF);
            break;
        }

        /* ---- group 1 */
        case 0x80: case 0x81: case 0x82: case 0x83: {
            int sz = (op & 1) ? (o32 ? 2 : 1) : 0;
            MODRM();
            int aop = (modrm >> 3) & 7;
            uint32_t a = RM_READ(sz);
            uint32_t b = op == 0x83 ? (uint32_t)(int32_t)(int8_t)F8() & szmask[sz] : FIMM(sz);
            uint32_t v = alu(aop, a, b, sz);
            if (aop != 7) RM_WRITE(sz, v);
            break;
        }
        case 0x84: case 0x85: {                              /* TEST */
            int sz = (op & 1) ? (o32 ? 2 : 1) : 0;
            MODRM();
            alu(4, RM_READ(sz), getreg(sz, (modrm >> 3) & 7), sz);
            break;
        }
        case 0x86: case 0x87: {                              /* XCHG */
            int sz = (op & 1) ? (o32 ? 2 : 1) : 0;
            MODRM();
            int r = (modrm >> 3) & 7;
            uint32_t a = RM_READ(sz), b = getreg(sz, r);
            RM_WRITE(sz, b);
            setreg(sz, r, a);
            break;
        }
        case 0x88: case 0x89: {
            int sz = (op & 1) ? (o32 ? 2 : 1) : 0;
            MODRM();
            RM_WRITE(sz, getreg(sz, (modrm >> 3) & 7));
            break;
        }
        case 0x8A: case 0x8B: {
            int sz = (op & 1) ? (o32 ? 2 : 1) : 0;
            MODRM();
            setreg(sz, (modrm >> 3) & 7, RM_READ(sz));
            break;
        }
        case 0x8C: {                                         /* MOV r/m, sreg */
            MODRM();
            uint32_t v = cpu.sreg[(modrm >> 3) & 7];
            if (isreg) setreg(o32 ? 2 : 1, modrm & 7, v); else wr16(lin, v);
            break;
        }
        case 0x8D: {                                         /* LEA */
            MODRM();
            if (isreg) EXC(6);
            setreg(o32 ? 2 : 1, (modrm >> 3) & 7, eaoff);
            break;
        }
        case 0x8E: {                                         /* MOV sreg, r/m */
            MODRM();
            int s = (modrm >> 3) & 7;
            if (s == SEG_CS || s > SEG_GS) EXC(6);
            set_sreg(s, RM_READ(1));
            if (s == SEG_SS) pend_set(PEND_INHIBIT);
            break;
        }
        case 0x8F: {                                         /* POP r/m */
            if (o32) { uint32_t v = pop32(); MODRM(); RM_WRITE(2, v); }
            else { uint32_t v = cpu_pop16(); MODRM(); RM_WRITE(1, v); }
            break;
        }
        case 0x90: if (rep == 0xF3) { /* PAUSE */ } break;
        case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97:
            if (o32) { uint32_t t = rEAX; rEAX = cpu.r.e[op & 7]; cpu.r.e[op & 7] = t; }
            else { uint32_t t = rAX; rAX = cpu.r.w[(op & 7) * 2]; cpu.r.w[(op & 7) * 2] = t; }
            break;
        case 0x98: if (o32) rEAX = (int32_t)(int16_t)rAX; else rAX = (int16_t)(int8_t)rAL; break;
        case 0x99: if (o32) rEDX = (int32_t)rEAX < 0 ? 0xFFFFFFFFu : 0; else rDX = (rAX & 0x8000) ? 0xFFFF : 0; break;
        case 0x9A: {                                         /* CALL far */
            uint32_t off = FIMM(o32 ? 2 : 1), seg = F16();
            uint32_t ocs = cpu.sreg[SEG_CS];
            PUSHV(cpu.sreg[SEG_CS]); PUSHV(ip);
            set_sreg(SEG_CS, seg); ip = off;
            code = mem + cpu.sbase[SEG_CS];
            FARLOG(1, ocs);
            break;
        }
        case 0x9B: break;                                    /* WAIT */
        case 0x9C: {                                         /* PUSHF */
            uint32_t f = get_flags();
            if (o32) push32(f & 0x00FCFFFF); else cpu_push16(f);
            break;
        }
        case 0x9D: {                                         /* POPF */
            uint32_t f = o32 ? pop32() : (get_flags() & 0xFFFF0000u) | cpu_pop16();
            set_flags(f & ~0x40000u);
            break;
        }
        case 0x9E: { uint32_t f = get_flags(); cpu.flags = (f & ~0xD5u) | (rAH & 0xD5) | 2; break; }
        case 0x9F: rAH = (get_flags() & 0xD5) | 2; break;
        case 0xA0: case 0xA1: case 0xA2: case 0xA3: {        /* MOV AL/AX <-> moffs */
            int sz = (op & 1) ? (o32 ? 2 : 1) : 0;
            uint32_t off = a32 ? F32() : F16();
            uint32_t l = (SEGB(SEG_DS) + off) & X86_LINMASK;
            if (op & 2) wrm(sz, l, getreg(sz, 0)); else setreg(sz, 0, rdm(sz, l));
            break;
        }
        case 0xA8: alu(4, rAL, F8(), 0); break;
        case 0xA9: { int sz = o32 ? 2 : 1; alu(4, getreg(sz, 0), FIMM(sz), sz); break; }
        case 0xB0: case 0xB1: case 0xB2: case 0xB3: case 0xB4: case 0xB5: case 0xB6: case 0xB7:
            setreg(0, op & 7, F8()); break;
        case 0xB8: case 0xB9: case 0xBA: case 0xBB: case 0xBC: case 0xBD: case 0xBE: case 0xBF:
            if (o32) cpu.r.e[op & 7] = F32(); else cpu.r.w[(op & 7) * 2] = F16();
            break;

        /* ---- group 2 */
        case 0xC0: case 0xC1: case 0xD0: case 0xD1: case 0xD2: case 0xD3: {
            int sz = (op & 1) ? (o32 ? 2 : 1) : 0;
            MODRM();
            uint32_t v = RM_READ(sz);
            uint32_t n = op <= 0xC1 ? F8() : op <= 0xD1 ? 1 : rCL;
            v = shift((modrm >> 3) & 7, v, n, sz);
            RM_WRITE(sz, v);
            break;
        }
        case 0xC2: { uint32_t n = F16(); ip = POPV(); if (!o32) ip &= 0xFFFF; rSP += n; break; }
        case 0xC3: ip = POPV(); if (!o32) ip &= 0xFFFF; break;
        case 0xC4: case 0xC5: {                              /* LES / LDS */
            MODRM();
            if (isreg) EXC(6);
            int sz = o32 ? 2 : 1;
            uint32_t off = rdm(sz, lin), seg = rd16(lin + (o32 ? 4 : 2));
            setreg(sz, (modrm >> 3) & 7, off);
            set_sreg(op == 0xC4 ? SEG_ES : SEG_DS, seg);
            break;
        }
        case 0xC6: case 0xC7: {
            int sz = (op & 1) ? (o32 ? 2 : 1) : 0;
            MODRM();
            RM_WRITE(sz, FIMM(sz));
            break;
        }
        case 0xC8: {                                         /* ENTER */
            uint32_t size = F16(), lvl = F8() & 31;
            PUSHV(o32 ? rEBP : rBP);
            uint32_t fp = rSP;
            if (lvl) {
                uint32_t bp = rBP;
                for (uint32_t i = 1; i < lvl; i++) {
                    bp = (bp - (o32 ? 4 : 2)) & 0xFFFF;
                    PUSHV(o32 ? rd32(cpu.sbase[SEG_SS] + bp) : rd16(cpu.sbase[SEG_SS] + bp));
                }
                PUSHV(fp);
            }
            if (o32) rEBP = fp; else rBP = fp;
            rSP -= size;
            break;
        }
        case 0xC9: rSP = rBP; if (o32) rEBP = pop32(); else rBP = cpu_pop16(); break;   /* LEAVE */
        case 0xCA: case 0xCB: {                              /* RETF */
            uint32_t n = op == 0xCA ? F16() : 0;
            uint32_t nip = POPV(), cs = POPV(), ocs = cpu.sreg[SEG_CS];
            set_sreg(SEG_CS, cs); ip = o32 ? nip : nip & 0xFFFF;
            rSP += n;
            code = mem + cpu.sbase[SEG_CS];
            FARLOG(3, ocs);
            break;
        }
        case 0xCC: SYNC(); cpu_interrupt(3, ip); RELOAD(); break;
        case 0xCD: { uint32_t n = F8(); SYNC(); cpu_interrupt(n, ip); RELOAD(); break; }
        case 0xCE: if (cond(0)) { SYNC(); cpu_interrupt(4, ip); RELOAD(); } break;
        case 0xCF: {                                         /* IRET */
            uint32_t nip, cs, f;
            if (o32) { nip = pop32(); cs = pop32() & 0xFFFF; f = pop32(); }
            else { nip = cpu_pop16(); cs = cpu_pop16(); f = (get_flags() & 0xFFFF0000u) | cpu_pop16(); }
            uint32_t ocs = cpu.sreg[SEG_CS];
            set_sreg(SEG_CS, cs); ip = nip;
            FARLOG(4, ocs);
            set_flags(f);
            code = mem + cpu.sbase[SEG_CS];
            break;
        }
        case 0xD4: {                                         /* AAM */
            uint32_t b = F8();
            if (!b) EXC(0);
            uint32_t al = rAL;
            rAH = al / b; rAL = al % b;
            get_flags(); cpu.flags &= ~(F_CF | F_OF | F_AF);
            szp(rAL, 0, 0);
            break;
        }
        case 0xD5: {                                         /* AAD */
            uint32_t b = F8();
            rAL = rAL + rAH * b; rAH = 0;
            get_flags(); cpu.flags &= ~(F_CF | F_OF | F_AF);
            szp(rAL, 0, 0);
            break;
        }
        case 0xD6: rAL = cf_now() ? 0xFF : 0; break;         /* SALC */
        case 0xD7: rAL = rd8((SEGB(SEG_DS) + ((a32 ? rEBX : rBX) + rAL)) & (a32 ? X86_LINMASK : 0xFFFFFFFF)); break;
        case 0xD8: case 0xD9: case 0xDA: case 0xDB: case 0xDC: case 0xDD: case 0xDE: case 0xDF:
            MODRM();                                         /* no FPU: ESC is ignored */
            break;
        case 0xE0: case 0xE1: case 0xE2: {                   /* LOOPNZ LOOPZ LOOP */
            int32_t d = (int8_t)F8();
            uint32_t c;
            if (a32) c = --rECX; else c = --rCX;
            int take = c != 0;
            if (op == 0xE0) take = take && !cond(4);
            else if (op == 0xE1) take = take && cond(4);
            if (take) ip = (ip + d) & 0xFFFF;
            break;
        }
        case 0xE3: { int32_t d = (int8_t)F8(); if ((a32 ? rECX : rCX) == 0) ip = (ip + d) & 0xFFFF; break; }
        case 0xE4: SYNC(); rAL = io_in(F8(), 0); break;
        case 0xE5: { uint32_t p = F8(); SYNC(); setreg(o32 ? 2 : 1, 0, io_in(p, o32 ? 2 : 1)); break; }
        case 0xE6: { uint32_t p = F8(); SYNC(); io_out(p, rAL, 0); break; }
        case 0xE7: { uint32_t p = F8(); SYNC(); io_out(p, getreg(o32 ? 2 : 1, 0), o32 ? 2 : 1); break; }
        case 0xE8: {                                         /* CALL near */
            uint32_t d = FIMM(o32 ? 2 : 1);
            PUSHV(ip);
            ip = (ip + d) & (o32 ? 0xFFFFFFFFu : 0xFFFF);
            break;
        }
        case 0xE9: { uint32_t d = FIMM(o32 ? 2 : 1); ip = (ip + d) & (o32 ? 0xFFFFFFFFu : 0xFFFF); break; }
        case 0xEA: {
            uint32_t off = FIMM(o32 ? 2 : 1), seg = F16(), ocs = cpu.sreg[SEG_CS];
            set_sreg(SEG_CS, seg); ip = off;
            code = mem + cpu.sbase[SEG_CS];
            FARLOG(2, ocs);
            break;
        }
        case 0xEB: { int32_t d = (int8_t)F8(); ip = (ip + d) & 0xFFFF; break; }
        case 0xEC: SYNC(); rAL = io_in(rDX, 0); break;
        case 0xED: SYNC(); setreg(o32 ? 2 : 1, 0, io_in(rDX, o32 ? 2 : 1)); break;
        case 0xEE: SYNC(); io_out(rDX, rAL, 0); break;
        case 0xEF: SYNC(); io_out(rDX, getreg(o32 ? 2 : 1, 0), o32 ? 2 : 1); break;
        case 0xF1: SYNC(); cpu_interrupt(1, ip); RELOAD(); break;
        case 0xF4:                                           /* HLT */
            SYNC();
            if (!irq_pending) __asm__ volatile("mcr p15, 0, %0, c7, c0, 4" :: "r"(0) : "memory");
            break;
        case 0xF5: { uint32_t f = get_flags(); cpu.flags = f ^ F_CF; break; }
        case 0xF6: case 0xF7: {                              /* group 3 */
            int sz = (op & 1) ? (o32 ? 2 : 1) : 0;
            MODRM();
            int g = (modrm >> 3) & 7;
            uint32_t v = RM_READ(sz);
            switch (g) {
            case 0: case 1: alu(4, v, FIMM(sz), sz); break;
            case 2: RM_WRITE(sz, ~v & szmask[sz]); break;
            case 3: { uint32_t r = (0 - v) & szmask[sz]; setlf(LF_SUB, sz, 0, v, r); RM_WRITE(sz, r); break; }
            case 4:                                          /* MUL */
                if (sz == 0) { rAX = rAL * v; mulflags(rAH != 0, rAL, 0); }
                else if (sz == 1) { uint32_t p = rAX * v; rAX = p; rDX = p >> 16; mulflags(rDX != 0, rAX, 1); }
                else { uint64_t p = (uint64_t)rEAX * v; rEAX = p; rEDX = p >> 32; mulflags(rEDX != 0, rEAX, 2); }
                break;
            case 5:                                          /* IMUL */
                if (sz == 0) { int32_t p = (int8_t)rAL * (int8_t)v; rAX = p; mulflags(p != (int8_t)p, rAL, 0); }
                else if (sz == 1) { int32_t p = (int16_t)rAX * (int16_t)v; rAX = p; rDX = p >> 16; mulflags(p != (int16_t)p, rAX, 1); }
                else { int64_t p = (int64_t)(int32_t)rEAX * (int32_t)v; rEAX = p; rEDX = (uint64_t)p >> 32; mulflags(p != (int32_t)p, rEAX, 2); }
                break;
            case 6: case 7:
                if (divide(g == 7, v, sz) < 0) EXC(0);
                break;
            }
            break;
        }
        case 0xF8: get_flags(); cpu.flags &= ~F_CF; break;
        case 0xF9: get_flags(); cpu.flags |= F_CF; break;
        case 0xFA: cpu.flags &= ~F_IF; break;
        case 0xFB: cpu.flags |= F_IF; pend_set(PEND_INHIBIT); break;
        case 0xFC: cpu.flags &= ~F_DF; break;
        case 0xFD: cpu.flags |= F_DF; break;
        case 0xFE: case 0xFF: {                              /* group 4/5 */
            int sz = (op & 1) ? (o32 ? 2 : 1) : 0;
            MODRM();
            int g = (modrm >> 3) & 7;
            switch (g) {
            case 0: RM_WRITE(sz, inc_dec(0, RM_READ(sz), sz)); break;
            case 1: RM_WRITE(sz, inc_dec(1, RM_READ(sz), sz)); break;
            case 2: { if (op == 0xFE) EXC(6); uint32_t t = RM_READ(sz); PUSHV(ip); ip = o32 ? t : t & 0xFFFF; break; }
            case 3: case 5: {
                if (op == 0xFE || isreg) EXC(6);
                uint32_t off = o32 ? rd32(lin) : rd16(lin), seg = rd16(lin + (o32 ? 4 : 2));
                uint32_t ocs = cpu.sreg[SEG_CS];
                if (g == 3) { PUSHV(cpu.sreg[SEG_CS]); PUSHV(ip); }
                set_sreg(SEG_CS, seg); ip = off;
                code = mem + cpu.sbase[SEG_CS];
                FARLOG(g == 3 ? 1 : 2, ocs);
                break;
            }
            case 4: { if (op == 0xFE) EXC(6); uint32_t t = RM_READ(sz); ip = o32 ? t : t & 0xFFFF; break; }
            case 6: { if (op == 0xFE) EXC(6); uint32_t t = RM_READ(sz); PUSHV(t); break; }
            default: EXC(6);
            }
            break;
        }

        /* ---- 0F xx */
        case 0x0F: {
            uint32_t op2 = F8();
            switch (op2) {
            case 0x00: MODRM(); if (((modrm >> 3) & 7) < 2) { RM_WRITE(1, 0); break; } EXC(6);
            case 0x01: {
                MODRM();
                int g = (modrm >> 3) & 7;
                if (g == 4) { RM_WRITE(1, (cpu.cr0 & 0xFFFF) | 0xFFF0); break; }       /* SMSW */
                if (g == 6) {                                  /* LMSW */
                    uint32_t v = RM_READ(1);
                    if (v & 1) { SYNC(); cpu_unsupported("protected mode (LMSW)"); RELOAD(); goto next; }
                    break;
                }
                if (g == 0 || g == 1) { if (!isreg) { wr16(lin, 0); wr32(lin + 2, 0); } break; }  /* SGDT/SIDT */
                if (g == 2 || g == 3) break;                   /* LGDT/LIDT: accepted, unused in real mode */
                EXC(6);
            }
            case 0x06: break;                                  /* CLTS */
            case 0x08: case 0x09: break;                       /* INVD / WBINVD */
            case 0x20: MODRM(); cpu.r.e[modrm & 7] = ((modrm >> 3) & 7) == 0 ? cpu.cr0 : 0; break;
            case 0x22: MODRM();
                if (((modrm >> 3) & 7) == 0) {
                    uint32_t v = cpu.r.e[modrm & 7];
                    if (v & 1) { SYNC(); cpu_unsupported("protected mode (MOV CR0)"); RELOAD(); goto next; }
                    cpu.cr0 = v | 0x10;
                }
                break;
            case 0x80: case 0x81: case 0x82: case 0x83: case 0x84: case 0x85: case 0x86: case 0x87:
            case 0x88: case 0x89: case 0x8A: case 0x8B: case 0x8C: case 0x8D: case 0x8E: case 0x8F: {
                uint32_t d = FIMM(o32 ? 2 : 1);
                if (cond(op2 & 15)) ip = (ip + d) & (o32 ? 0xFFFFFFFFu : 0xFFFF);
                break;
            }
            case 0x90: case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97:
            case 0x98: case 0x99: case 0x9A: case 0x9B: case 0x9C: case 0x9D: case 0x9E: case 0x9F:
                MODRM(); RM_WRITE(0, cond(op2 & 15) ? 1 : 0); break;
            case 0xA0: PUSHV(cpu.sreg[SEG_FS]); break;
            case 0xA1: set_sreg(SEG_FS, POPV()); break;
            case 0xA8: PUSHV(cpu.sreg[SEG_GS]); break;
            case 0xA9: set_sreg(SEG_GS, POPV()); break;
            case 0xA3: case 0xAB: case 0xB3: case 0xBB: case 0xBA: {   /* BT BTS BTR BTC */
                int sz = o32 ? 2 : 1, bits = 8 << sz;
                MODRM();
                uint32_t bit; int g;
                if (op2 == 0xBA) { g = (modrm >> 3) & 7; if (g < 4) EXC(6); bit = F8() & (bits - 1); }
                else {
                    g = op2 == 0xA3 ? 4 : op2 == 0xAB ? 5 : op2 == 0xB3 ? 6 : 7;
                    bit = getreg(sz, (modrm >> 3) & 7);
                    if (!isreg) {                              /* the bit offset may reach other words */
                        int32_t sb = sz == 2 ? (int32_t)bit : (int16_t)bit;
                        lin = (lin + ((sb >> (sz == 2 ? 5 : 4)) * (bits / 8))) & X86_LINMASK;
                    }
                    bit &= bits - 1;
                }
                uint32_t v = RM_READ(sz), m = 1u << bit;
                uint32_t f = get_flags() & ~F_CF;
                cpu.flags = f | ((v & m) ? F_CF : 0);
                if (g == 5) v |= m; else if (g == 6) v &= ~m; else if (g == 7) v ^= m;
                if (g != 4) RM_WRITE(sz, v);
                break;
            }
            case 0xA4: case 0xA5: case 0xAC: case 0xAD: {       /* SHLD / SHRD */
                int sz = o32 ? 2 : 1;
                MODRM();
                uint32_t n = (op2 & 1) ? rCL : F8();
                uint32_t v = shxd(op2 >= 0xAC, RM_READ(sz), getreg(sz, (modrm >> 3) & 7), n, sz);
                RM_WRITE(sz, v);
                break;
            }
            case 0xAF: {                                       /* IMUL r, r/m */
                int sz = o32 ? 2 : 1;
                MODRM();
                int32_t a = sz == 2 ? (int32_t)getreg(2, (modrm >> 3) & 7) : (int16_t)getreg(1, (modrm >> 3) & 7);
                int32_t b = sz == 2 ? (int32_t)RM_READ(2) : (int16_t)RM_READ(1);
                int64_t p = (int64_t)a * b;
                uint32_t r = (uint32_t)p & szmask[sz];
                setreg(sz, (modrm >> 3) & 7, r);
                mulflags(sz == 2 ? p != (int32_t)p : p != (int16_t)p, r, sz);
                break;
            }
            case 0xB0: case 0xB1: {                            /* CMPXCHG */
                int sz = (op2 & 1) ? (o32 ? 2 : 1) : 0;
                MODRM();
                uint32_t d = RM_READ(sz), a = getreg(sz, 0);
                alu(7, a, d, sz);
                if (a == d) RM_WRITE(sz, getreg(sz, (modrm >> 3) & 7));
                else { setreg(sz, 0, d); if (!isreg) wrm(sz, lin, d); }
                break;
            }
            case 0xB2: case 0xB4: case 0xB5: {                 /* LSS LFS LGS */
                MODRM();
                if (isreg) EXC(6);
                int sz = o32 ? 2 : 1;
                uint32_t off = rdm(sz, lin), seg = rd16(lin + (o32 ? 4 : 2));
                setreg(sz, (modrm >> 3) & 7, off);
                set_sreg(op2 == 0xB2 ? SEG_SS : op2 == 0xB4 ? SEG_FS : SEG_GS, seg);
                if (op2 == 0xB2) pend_set(PEND_INHIBIT);
                break;
            }
            case 0xB6: case 0xB7: case 0xBE: case 0xBF: {      /* MOVZX / MOVSX */
                int sz = o32 ? 2 : 1, ssz = (op2 & 1) ? 1 : 0;
                MODRM();
                uint32_t v = RM_READ(ssz);
                if (op2 >= 0xBE) v = ssz ? (uint32_t)(int32_t)(int16_t)v : (uint32_t)(int32_t)(int8_t)v;
                setreg(sz, (modrm >> 3) & 7, v & szmask[sz]);
                break;
            }
            case 0xBC: case 0xBD: {                            /* BSF / BSR */
                int sz = o32 ? 2 : 1;
                MODRM();
                uint32_t v = RM_READ(sz);
                uint32_t f = get_flags() & ~F_ZF;
                if (!v) { cpu.flags = f | F_ZF; break; }
                cpu.flags = f;
                setreg(sz, (modrm >> 3) & 7, op2 == 0xBC ? (uint32_t)__builtin_ctz(v) : 31u - __builtin_clz(v));
                break;
            }
            case 0xC0: case 0xC1: {                            /* XADD */
                int sz = (op2 & 1) ? (o32 ? 2 : 1) : 0;
                MODRM();
                int r = (modrm >> 3) & 7;
                uint32_t d = RM_READ(sz), s = getreg(sz, r);
                uint32_t sum = alu(0, d, s, sz);
                setreg(sz, r, d);
                RM_WRITE(sz, sum);
                break;
            }
            case 0xC8: case 0xC9: case 0xCA: case 0xCB: case 0xCC: case 0xCD: case 0xCE: case 0xCF:
                cpu.r.e[op2 & 7] = __builtin_bswap32(cpu.r.e[op2 & 7]); break;
            case 0xFF: {                                       /* our trap: 0F FF nn = native INT nn in a stub */
                uint32_t n = F8();
                SYNC();
                hle_fset = 0;
                int r = hle_int(n);
                if (r == HLE_RETRY) { cpu.eip = cpu.prev_eip; cpu.flags |= F_IF; }
                else if (r == HLE_DONE && hle_fset) {
                    /* the flags the service returns (CF, ZF): into the FLAGS image IRET will pop */
                    uint32_t fl = get_flags(), sl = cpu.sbase[SEG_SS] + ((rSP + 4) & 0xFFFF);
                    uint32_t old = rd16(sl);
                    wr16(sl, (old & ~hle_fset) | (fl & hle_fset));
                }
                RELOAD();
                break;
            }
            case 0xFE: { uint32_t n = F8(); SYNC(); hle_special(n); RELOAD(); break; }
            default: EXC(6);
            }
            break;
        }
        default:
            EXC(6);
        }
        goto next;

    do_ea: {
        /* effective address of the ModRM operand -> lin (and eaoff) */
        uint32_t off; int def = SEG_DS, mod = modrm >> 6, rm = modrm & 7;
        if (!a32) {
            switch (rm) {
            case 0: off = rBX + rSI; break;
            case 1: off = rBX + rDI; break;
            case 2: off = rBP + rSI; def = SEG_SS; break;
            case 3: off = rBP + rDI; def = SEG_SS; break;
            case 4: off = rSI; break;
            case 5: off = rDI; break;
            case 6: if (mod == 0) off = F16(); else { off = rBP; def = SEG_SS; } break;
            default: off = rBX; break;
            }
            if (mod == 1) off += (uint32_t)(int32_t)(int8_t)F8();
            else if (mod == 2) off += F16();
            off &= 0xFFFF;
            eaoff = off;
            lin = SEGB(def) + off;
        } else {
            if (rm == 4) {
                uint32_t sib = F8();
                int base = sib & 7, idx = (sib >> 3) & 7, sc = sib >> 6;
                if (base == 5 && mod == 0) off = F32();
                else { off = cpu.r.e[base]; if (base == 4 || base == 5) def = SEG_SS; }
                if (idx != 4) off += cpu.r.e[idx] << sc;
            } else if (rm == 5 && mod == 0) off = F32();
            else { off = cpu.r.e[rm]; if (rm == 5) def = SEG_SS; }
            if (mod == 1) off += (uint32_t)(int32_t)(int8_t)F8();
            else if (mod == 2) off += F32();
            eaoff = off;
            lin = (SEGB(def) + off) & X86_LINMASK;
        }
        goto *mret;
    }
    next: ;
    }
}

/* MOVS CMPS STOS LODS SCAS INS OUTS; returns 1 when a long REP is paused
 * (the instruction is re-executed so interrupts get a chance). */
static int string_op(int op, int sz, int a32, int ovr, int rep)
{
    int n = 1 << sz;
    int32_t d = (cpu.flags & F_DF) ? -n : n;
    uint32_t sbase = ovr >= 0 ? cpu.sbase[ovr] : cpu.sbase[SEG_DS];
    uint32_t ebase = cpu.sbase[SEG_ES];
    uint32_t amask = a32 ? X86_LINMASK : 0xFFFF;
    uint32_t count = rep ? (a32 ? rECX : rCX) : 1;
    int budget = 4096;
    if (rep && !count) return 0;
    switch (op) {
    case 0xA4: case 0xA5:                                   /* MOVS */
        if (rep && !a32 && d > 0) {
            /* forward: in runs that stay inside one page on both sides; a
               destination just above the source (the classic replicating
               fill) goes element by element, as the CPU does */
            while (count && budget > 0) {
                uint32_t s = rSI, t = rDI, sl = sbase + s, tl = ebase + t;
                uint32_t bytes = count << sz;
                if (bytes > 0x10000 - s) bytes = 0x10000 - s;
                if (bytes > 0x10000 - t) bytes = 0x10000 - t;
                if (bytes > 256 - (sl & 0xFF)) bytes = 256 - (sl & 0xFF);
                if (bytes > 256 - (tl & 0xFF)) bytes = 256 - (tl & 0xFF);
                bytes &= ~(uint32_t)(n - 1);
                uintptr_t wb = wpt[tl >> 8];
                /* video memory (A0000h-AFFFFh may be the VGA's planar
                   window, where every read loads the latches that the next
                   write uses): element by element, in order */
                if (!bytes || (wb & 1) || (tl > sl && tl < sl + bytes) || sl - 0xA0000u < 0x10000u || tl - 0xA0000u < 0x10000u) {
                    wrm(sz, tl, rdm(sz, sl));
                    bytes = n;
                } else memcpy((uint8_t *)(wb + tl), (const uint8_t *)(rpt[sl >> 8] + sl), bytes);
                rSI += bytes; rDI += bytes; count -= bytes >> sz; budget -= bytes / 32 + 1;
            }
            rCX = count;
            return count != 0;
        }
        while (count) {
            uint32_t v = rdm(sz, (sbase + sidx(a32, 6)) & (a32 ? X86_LINMASK : 0xFFFFFFFF));
            wrm(sz, (ebase + sidx(a32, 7)) & (a32 ? X86_LINMASK : 0xFFFFFFFF), v);
            sadv(a32, 6, d); sadv(a32, 7, d);
            count--;
            if (rep && --budget <= 0) break;
        }
        break;
    case 0xAA: case 0xAB: {                                 /* STOS */
        uint32_t v = getreg(sz, 0);
        if (rep && !a32 && d > 0 && sz < 2) {
            while (count && budget > 0) {
                uint32_t t = rDI, tl = ebase + t;
                uint32_t bytes = count << sz;
                if (bytes > 0x10000 - t) bytes = 0x10000 - t;
                if (bytes > 256 - (tl & 0xFF)) bytes = 256 - (tl & 0xFF);
                bytes &= ~(uint32_t)(n - 1);
                uintptr_t wb = wpt[tl >> 8];
                if (!bytes || (wb & 1)) { wrm(sz, tl, v); bytes = n; }
                else if (sz == 0) memset((uint8_t *)(wb + tl), v, bytes);
                else { uint8_t *q = (uint8_t *)(wb + tl); for (uint32_t i = 0; i < bytes; i += 2) { q[i] = v; q[i + 1] = v >> 8; } }
                rDI += bytes; count -= bytes >> sz; budget -= bytes / 32 + 1;
            }
            rCX = count;
            return count != 0;
        }
        while (count) {
            wrm(sz, (ebase + sidx(a32, 7)) & (a32 ? X86_LINMASK : 0xFFFFFFFF), v);
            sadv(a32, 7, d);
            count--;
            if (rep && --budget <= 0) break;
        }
        break;
    }
    case 0xAC: case 0xAD:                                   /* LODS */
        while (count) {
            setreg(sz, 0, rdm(sz, (sbase + sidx(a32, 6)) & (a32 ? X86_LINMASK : 0xFFFFFFFF)));
            sadv(a32, 6, d);
            count--;
            if (rep && --budget <= 0) break;
        }
        break;
    case 0xA6: case 0xA7:                                   /* CMPS */
        while (count) {
            uint32_t a = rdm(sz, (sbase + sidx(a32, 6)) & (a32 ? X86_LINMASK : 0xFFFFFFFF));
            uint32_t b = rdm(sz, (ebase + sidx(a32, 7)) & (a32 ? X86_LINMASK : 0xFFFFFFFF));
            alu(7, a, b, sz);
            sadv(a32, 6, d); sadv(a32, 7, d);
            count--;
            if (rep) {
                if ((rep == 0xF3) != (a == b)) break;
                if (--budget <= 0) break;
            }
        }
        if (rep) { if (a32) rECX = count; else rCX = count; }
        if (rep && count && budget <= 0 && ((rep == 0xF3) == (cond(4) != 0))) return 1;
        return 0;
    case 0xAE: case 0xAF: {                                 /* SCAS */
        uint32_t a = getreg(sz, 0);
        while (count) {
            uint32_t b = rdm(sz, (ebase + sidx(a32, 7)) & (a32 ? X86_LINMASK : 0xFFFFFFFF));
            alu(7, a, b, sz);
            sadv(a32, 7, d);
            count--;
            if (rep) {
                if ((rep == 0xF3) != (a == b)) break;
                if (--budget <= 0) break;
            }
        }
        if (rep) { if (a32) rECX = count; else rCX = count; }
        if (rep && count && budget <= 0 && ((rep == 0xF3) == (cond(4) != 0))) return 1;
        return 0;
    }
    case 0x6C: case 0x6D:                                   /* INS */
        while (count) {
            wrm(sz, (ebase + sidx(a32, 7)) & (a32 ? X86_LINMASK : 0xFFFFFFFF), io_in(rDX, sz));
            sadv(a32, 7, d);
            count--;
            if (rep && --budget <= 0) break;
        }
        break;
    case 0x6E: case 0x6F:                                   /* OUTS */
        while (count) {
            io_out(rDX, rdm(sz, (sbase + sidx(a32, 6)) & (a32 ? X86_LINMASK : 0xFFFFFFFF)), sz);
            sadv(a32, 6, d);
            count--;
            if (rep && --budget <= 0) break;
        }
        break;
    }
    (void)amask;
    if (rep) {
        if (a32) rECX = count; else rCX = count;
        return count != 0;
    }
    return 0;
}

/* ------------------------------------------------------------ for jit.c */
uint32_t jh_shift(uint32_t op, uint32_t v, uint32_t cnt, uint32_t sz) { return shift(op, v, cnt, sz); }
int jh_string(uint32_t op, uint32_t sz, int ovr, uint32_t rep) { return string_op(op, sz, 0, ovr, rep); }
int jh_cond(uint32_t cc) { return cond(cc); }
void jh_fix_cf(void) { uint32_t c = cf_now(); cpu.flags = (cpu.flags & ~F_CF) | c; }
int jh_div(uint32_t sgn, uint32_t d, uint32_t sz) { return divide(sgn, d, sz); }
void jh_mul(uint32_t sgn, uint32_t v, uint32_t sz)
{
    if (!sgn) {
        if (sz == 0) { rAX = rAL * v; mulflags(rAH != 0, rAL, 0); }
        else if (sz == 1) { uint32_t p = rAX * v; rAX = p; rDX = p >> 16; mulflags(rDX != 0, rAX, 1); }
        else { uint64_t p = (uint64_t)rEAX * v; rEAX = (uint32_t)p; rEDX = (uint32_t)(p >> 32); mulflags(rEDX != 0, rEAX, 2); }
    } else {
        if (sz == 0) { int32_t p = (int8_t)rAL * (int8_t)v; rAX = p; mulflags(p != (int8_t)p, rAL, 0); }
        else if (sz == 1) { int32_t p = (int16_t)rAX * (int16_t)v; rAX = p; rDX = p >> 16; mulflags(p != (int16_t)p, rAX, 1); }
        else { int64_t p = (int64_t)(int32_t)rEAX * (int32_t)v; rEAX = (uint32_t)p; rEDX = (uint32_t)((uint64_t)p >> 32); mulflags(p != (int32_t)p, rEAX, 2); }
    }
}
uint32_t jh_imul3(int32_t a, int32_t b)
{
    int32_t p = (int16_t)a * (int16_t)b;
    mulflags(p != (int16_t)p, p & 0xFFFF, 1);
    return p & 0xFFFF;
}
void jh_flagop(uint32_t op)
{
    uint32_t f = get_flags();
    switch (op) {
    case 0xF5: f ^= F_CF; break;
    case 0xF8: f &= ~F_CF; break;
    case 0xF9: f |= F_CF; break;
    case 0xFC: f &= ~F_DF; break;
    case 0xFD: f |= F_DF; break;
    case 0x9E: f = (f & ~0xD5u) | (rAH & 0xD5) | 2; break;
    case 0x9F: rAH = (f & 0xD5) | 2; break;
    }
    cpu.flags = f;
}
void jh_pushf(void) { cpu_push16(get_flags()); }
uint32_t jh_imul2(int32_t a, int32_t b, uint32_t sz)
{
    if (sz == 2) { int64_t p = (int64_t)a * b; mulflags(p != (int32_t)p, (uint32_t)p, 2); return (uint32_t)p; }
    int32_t p = (int16_t)a * (int16_t)b;
    mulflags(p != (int16_t)p, p & 0xFFFF, 1);
    return p & 0xFFFF;
}
void jh_pusha(uint32_t o32)
{
    if (o32) { uint32_t sp = rESP; for (int i = 0; i < 8; i++) push32(i == 4 ? sp : cpu.r.e[i]); }
    else { uint32_t sp = rSP; for (int i = 0; i < 8; i++) cpu_push16(i == 4 ? sp : cpu.r.w[i * 2]); }
}
void jh_popa(uint32_t o32)
{
    for (int i = 7; i >= 0; i--) {
        if (o32) { uint32_t v = pop32(); if (i != 4) cpu.r.e[i] = v; }
        else { uint32_t v = cpu_pop16(); if (i != 4) cpu.r.w[i * 2] = v; }
    }
}
uint32_t jh_shxd(uint32_t right_sz, uint32_t dst, uint32_t src, uint32_t cnt) { return shxd(right_sz & 1, dst, src, cnt, right_sz >> 1); }
