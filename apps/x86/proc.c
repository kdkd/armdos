/*
 * proc.c - x86 processes inside the Compatibility Box: the x86 memory
 * arena (DOS's MCB chain, kept in x86 memory with DOS 4's layout and
 * algorithms), PSPs in the x86 layout, the .COM / MZ .EXE loader, EXEC of
 * x86 programs (nested inside this box) and of ARM programs (handed to the
 * real EXEC), terminate and TSR.
 *
 * Handles: an x86 program's handles ARE ARM-DOS handles (redirection,
 * devices and files work natively).  Each nested x86 process gets a shadow
 * ARM PSP (INT 21h AH=55h + 50h), so it has its own job file table exactly
 * as under DOS: closing a handle in a child does not close the parent's.
 *
 * Copyright (c) 2026 Europa Micro Systems. Part of ARM-DOS (apps/x86).
 */
#include <stdio.h>
#include <stdlib.h>
#include "dos86.h"
#include "jit.h"

struct xproc *cur_proc;
uint16_t cur_psp;
uint32_t dta_far;
int exit_code;
int x86_exited;
int arena_strategy;
uint16_t arena_end = 0xA000;
uint16_t last_exit;               /* AH=4Dh */
static uint16_t arm_own_psp;       /* X86.EXE's own PSP segment */

/* ------------------------------------------------------------ helpers */

#define MCB_TYPE(s)  (*hptr(LIN(s, 0)))
static uint16_t mcb_owner(uint16_t s) { const uint8_t *p = hptr(LIN(s, 1)); return p[0] | (p[1] << 8); }
static uint16_t mcb_size(uint16_t s) { const uint8_t *p = hptr(LIN(s, 3)); return p[0] | (p[1] << 8); }
static void mcb_set(uint16_t s, char type, uint16_t owner, uint16_t size)
{
    uint8_t *p = hptr(LIN(s, 0));
    p[0] = type; p[1] = owner; p[2] = owner >> 8; p[3] = size; p[4] = size >> 8;
}
static void w16(uint32_t lin, uint16_t v) { uint8_t *p = hptr(lin); p[0] = v; p[1] = v >> 8; }
static uint16_t r16(uint32_t lin) { const uint8_t *p = hptr(lin); return p[0] | (p[1] << 8); }

int arm21(struct armregs *r)
{
    int cf = _armdos_int21(r);
    return cf;
}

/* ------------------------------------------------------------ arena */

/* merge free blocks following s; -1 if the chain is broken */
static int coalesce(uint16_t s)
{
    while (MCB_TYPE(s) == 'M' && mcb_owner(s) == 0) {
        uint16_t n = s + mcb_size(s) + 1;
        char t = MCB_TYPE(n);
        if (t != 'M' && t != 'Z') return -1;
        if (mcb_owner(n) != 0) break;
        mcb_set(s, t, 0, mcb_size(s) + mcb_size(n) + 1);
    }
    return 0;
}

static int arena_walk_ok(void)
{
    uint16_t s = ARENA_FIRST;
    for (int i = 0; i < 4096; i++) {
        char t = MCB_TYPE(s);
        if (t == 'Z') return 1;
        if (t != 'M') return 0;
        s = s + mcb_size(s) + 1;
    }
    return 0;
}

int arena_alloc(uint32_t paras, uint16_t *seg, uint16_t *largest)
{
    uint16_t best = 0, bestsz = 0, big = 0;
    uint16_t s = ARENA_FIRST;
    for (;;) {
        char t = MCB_TYPE(s);
        if (t != 'M' && t != 'Z') { *largest = 0; return -7; }
        if (mcb_owner(s) == 0) {
            if (coalesce(s) < 0) { *largest = 0; return -7; }
            t = MCB_TYPE(s);
            uint16_t sz = mcb_size(s);
            if (sz > big) big = sz;
            if (sz >= paras) {
                int take = 0;
                if (!best) take = 1;
                else if ((arena_strategy & 3) == 1 && sz < bestsz) take = 1;   /* best fit */
                else if ((arena_strategy & 3) == 2) take = 1;                  /* last fit */
                if (take) { best = s; bestsz = sz; }
            }
        }
        if (t == 'Z') break;
        s = s + mcb_size(s) + 1;
    }
    if (!best || paras > 0xFFFF) { *largest = big; return -8; }
    char t = MCB_TYPE(best);
    uint16_t owner = cur_psp ? cur_psp : 8;
    if (bestsz == paras) { mcb_set(best, t, owner, paras); *seg = best + 1; }
    else if ((arena_strategy & 3) == 2) {
        /* last fit: the top of the block */
        uint16_t ns = best + bestsz - paras;
        mcb_set(best, 'M', 0, bestsz - paras - 1);
        mcb_set(ns, t, owner, paras);
        *seg = ns + 1;
    } else {
        uint16_t ns = best + paras + 1;
        mcb_set(ns, t, 0, bestsz - paras - 1);
        memset(hptr(LIN(ns, 5)), 0, 11);
        mcb_set(best, 'M', owner, paras);
        *seg = best + 1;
    }
    memset(hptr(LIN(*seg - 1, 5)), 0, 11);
    return 0;
}

static int valid_block(uint16_t seg)
{
    uint16_t s = ARENA_FIRST;
    for (int i = 0; i < 4096; i++) {
        if (s + 1 == seg) return 1;
        char t = MCB_TYPE(s);
        if (t != 'M') return 0;
        s = s + mcb_size(s) + 1;
    }
    return 0;
}

int arena_free(uint16_t seg)
{
    if (!arena_walk_ok()) return -7;
    if (!valid_block(seg)) return -9;
    uint16_t s = seg - 1;
    mcb_set(s, MCB_TYPE(s), 0, mcb_size(s));
    return 0;
}

int arena_resize(uint16_t seg, uint32_t paras, uint16_t *largest)
{
    if (!arena_walk_ok()) { *largest = 0; return -7; }
    if (!valid_block(seg)) { *largest = 0; return -9; }
    uint16_t s = seg - 1, sz = mcb_size(s), owner = mcb_owner(s);
    char t = MCB_TYPE(s);
    if (paras <= sz) {
        if (paras < sz) {
            uint16_t ns = s + paras + 1;
            mcb_set(ns, t, 0, sz - paras - 1);
            memset(hptr(LIN(ns, 5)), 0, 11);
            mcb_set(s, 'M', owner, paras);
            coalesce(ns);
        }
        return 0;
    }
    /* grow into the free block(s) after it */
    uint32_t avail = sz;
    if (t == 'M') {
        uint16_t n = s + sz + 1;
        if (mcb_owner(n) == 0) { coalesce(n); avail = sz + mcb_size(n) + 1; }
        if (paras <= avail) {
            char nt = MCB_TYPE(n);
            if (paras == avail || paras + 1 > avail) {
                if (paras + 1 > avail && paras != avail) {
                    /* exactly fills except one para: take it all */
                }
                mcb_set(s, nt, owner, avail);
            } else {
                uint16_t ns = s + paras + 1;
                mcb_set(ns, nt, 0, avail - paras - 1);
                memset(hptr(LIN(ns, 5)), 0, 11);
                mcb_set(s, 'M', owner, paras);
            }
            return 0;
        }
    }
    /* DOS: the block is left at its maximum size */
    if (avail > sz) {
        uint16_t n = s + sz + 1;
        mcb_set(s, MCB_TYPE(n), owner, avail);
    }
    *largest = avail;
    return -8;
}

void arena_free_owner(uint16_t owner)
{
    uint16_t s = ARENA_FIRST;
    for (int i = 0; i < 4096; i++) {
        char t = MCB_TYPE(s);
        if (t != 'M' && t != 'Z') return;
        if (mcb_owner(s) == owner) mcb_set(s, t, 0, mcb_size(s));
        if (t == 'Z') return;
        s = s + mcb_size(s) + 1;
    }
}

static void mcb_name(uint16_t seg, const char *path)
{
    const char *b = path;
    for (const char *q = path; *q; q++) if (*q == '\\' || *q == '/' || *q == ':') b = q + 1;
    uint8_t *n = hptr(LIN(seg - 1, 8));
    int i = 0;
    for (; b[i] && b[i] != '.' && i < 8; i++) n[i] = b[i];
    if (i < 8) n[i] = 0;
}

/* ------------------------------------------------------------ DTA */

void set_dta(uint16_t seg, uint16_t off)
{
    struct armregs r;
    dta_far = ((uint32_t)seg << 16) | off;
    memset(&r, 0, sizeof r);
    r.r0 = 0x1A00;
    r.r3 = (uint32_t)hptr(LIN(seg, off));
    arm21(&r);
}

/* the x86 PSP's job file table mirrors the ARM one */
void jft_sync(void)
{
    struct armregs r;
    memset(&r, 0, sizeof r);
    r.r0 = 0x6200;
    arm21(&r);
    const uint8_t *arm = (const uint8_t *)((r.r1 & 0xFFFF) << 4);
    uint8_t *x = hptr(LIN(cur_psp, 0x18));
    memcpy(x, arm + 0x18, 20);
}

/* ------------------------------------------------------------ files */

static int f_open(const char *path)
{
    struct armregs r;
    memset(&r, 0, sizeof r);
    r.r0 = 0x3D00; r.r3 = (uint32_t)path;
    if (arm21(&r)) return -(int)(r.r0 & 0xFFFF);
    return r.r0 & 0xFFFF;
}
static void f_close(int h)
{
    struct armregs r;
    memset(&r, 0, sizeof r);
    r.r0 = 0x3E00; r.r1 = h;
    arm21(&r);
}
static long f_seek(int h, long pos, int how)
{
    struct armregs r;
    memset(&r, 0, sizeof r);
    r.r0 = 0x4200 | how; r.r1 = h; r.r2 = (uint32_t)pos >> 16; r.r3 = pos & 0xFFFF;
    if (arm21(&r)) return -1;
    return ((r.r3 & 0xFFFF) << 16) | (r.r0 & 0xFFFF);
}
static long f_read(int h, void *buf, uint32_t n)
{
    long total = 0;
    uint8_t *p = buf;
    while (n) {
        struct armregs r;
        uint32_t k = n > 0x8000 ? 0x8000 : n;
        memset(&r, 0, sizeof r);
        r.r0 = 0x3F00; r.r1 = h; r.r2 = k; r.r3 = (uint32_t)p;
        if (arm21(&r)) return -1;
        uint32_t got = r.r0 & 0xFFFF;
        total += got; p += got; n -= got;
        if (got < k) break;
    }
    return total;
}

/* An ARM-DOS program?  MZ with an AR1 header, or a raw .COM whose first
 * two words are ARM instructions with the AL condition (every ARM-DOS .COM:
 * the SDK's self-relocating prologue, or hand-written code). */
int is_arm_image(const uint8_t *hdr, int n, uint32_t fsize, int fd)
{
    if (n >= 2 && ((hdr[0] == 'M' && hdr[1] == 'Z') || (hdr[0] == 'Z' && hdr[1] == 'M'))) {
        if (n < 0x40) return 0;
        uint32_t lfa = hdr[0x3C] | (hdr[0x3D] << 8) | (hdr[0x3E] << 16) | ((uint32_t)hdr[0x3F] << 24);
        if (lfa < 0x40 || lfa + 4 > fsize) return 0;
        uint8_t sig[4];
        if (f_seek(fd, lfa, 0) < 0 || f_read(fd, sig, 4) != 4) return 0;
        return memcmp(sig, "AR1", 4) == 0;
    }
    /* the same rule as the kernel's EXEC (kernel/dos/proc.c): ARM code
       starts with instructions of condition AL (top nibble E) - 3 of the
       first 4 words, and the first one not in the coprocessor space */
    int words = n / 4, al = 0;
    if (words > 4) words = 4;
    if (words == 0) return 1;
    if ((hdr[3] & 0xF0) != 0xE0 || (hdr[3] & 0x0C) == 0x0C) return 0;
    for (int i = 0; i < words; i++) if ((hdr[i * 4 + 3] & 0xF0) == 0xE0) al++;
    return words == 4 ? al >= 3 : 1;          /* (under 16 bytes: the first word decides) */
}

/* ------------------------------------------------------------ PSP */

static void build_psp(uint16_t psp, uint16_t memtop, uint16_t parent, uint16_t envseg)
{
    uint8_t *p = hptr(LIN(psp, 0));
    memset(p, 0, 256);
    p[0] = 0xCD; p[1] = 0x20;                       /* INT 20h */
    p[2] = memtop; p[3] = memtop >> 8;
    p[5] = 0x9A; p[6] = 0xF0; p[7] = 0xFE; p[8] = 0x1D; p[9] = 0xF0;   /* CALL F01D:FEF0 (CP/M) */
    uint32_t v22 = rd32(0x22 * 4), v23 = rd32(0x23 * 4), v24 = rd32(0x24 * 4);
    memcpy(p + 0x0A, &v22, 4); memcpy(p + 0x0E, &v23, 4); memcpy(p + 0x12, &v24, 4);
    p[0x16] = parent; p[0x17] = parent >> 8;
    memset(p + 0x18, 0xFF, 20);
    p[0x2C] = envseg; p[0x2D] = envseg >> 8;
    p[0x32] = 20;
    p[0x34] = 0x18; p[0x36] = psp; p[0x37] = psp >> 8;
    memset(p + 0x38, 0xFF, 4);
    p[0x40] = 4;                                    /* DOS version (for SETVER-aware programs) */
    p[0x50] = 0xCD; p[0x51] = 0x21; p[0x52] = 0xCB; /* INT 21h ; RETF */
    memset(p + 0x5D, ' ', 11);
    memset(p + 0x6D, ' ', 11);
    p[0x81] = 0x0D;
}

/* environment block: strings, 0, word 1, program path */
static int make_env(uint16_t srcseg, const char *srcflat, const char *path, uint16_t *out)
{
    const char *e = srcseg ? (const char *)hptr(LIN(srcseg, 0)) : srcflat;
    uint32_t len = 0;
    if (e) {
        while (e[len]) { while (e[len]) { len++; if (len > 32767) return -10; } len++; }
    }
    uint32_t plen = strlen(path);
    uint32_t total = (len ? len : 1) + 1 + 2 + plen + 1;
    uint16_t seg, big;
    int err = arena_alloc((total + 15) >> 4, &seg, &big);
    if (err) return err;
    uint8_t *d = hptr(LIN(seg, 0));
    if (len) { memcpy(d, e, len); d[len] = 0; }
    else { d[0] = 0; d[1] = 0; len = 1; }
    d[len + 1] = 1; d[len + 2] = 0;
    memcpy(d + len + 3, path, plen + 1);
    *out = seg;
    return 0;
}

/* parse the command tail's first two arguments into the PSP's FCBs */
static uint16_t parse_fcbs(uint16_t psp)
{
    struct armregs r;
    uint8_t *tail = hptr(LIN(psp, 0x81));
    uint16_t ax = 0;
    memset(&r, 0, sizeof r);
    r.r0 = 0x2901; r.r4 = (uint32_t)tail; r.r5 = (uint32_t)hptr(LIN(psp, 0x5C));
    arm21(&r);
    ax = r.r0 & 0xFF;
    uint32_t next = r.r4;
    memset(&r, 0, sizeof r);
    r.r0 = 0x2901; r.r4 = next; r.r5 = (uint32_t)hptr(LIN(psp, 0x6C));
    arm21(&r);
    ax |= (r.r0 & 0xFF) << 8;
    return ax;
}

/* ------------------------------------------------------------ loading */

struct mzhdr {
    uint16_t sig, cblp, cp, crlc, cparhdr, minalloc, maxalloc, ss, sp, csum, ip, cs, lfarlc, ovno;
};

struct loadinfo {
    uint16_t psp, env;
    uint16_t cs, ip, ss, sp;
};

/* load an x86 program into a new process (not yet running). Returns 0 or -DOS error. */
static int load_program(const char *path, uint16_t envsrc, const char *envflat, const uint8_t *tail,
                        const uint8_t *fcb1, const uint8_t *fcb2, uint16_t parent, struct loadinfo *li)
{
    int fd = f_open(path);
    if (fd < 0) return fd;
    uint8_t hdr[64];
    memset(hdr, 0, sizeof hdr);
    long n = f_read(fd, hdr, 64);
    long fsize = f_seek(fd, 0, 2);
    if (n < 0 || fsize < 0) { f_close(fd); return -5; }
    int mz = n >= 28 && ((hdr[0] == 'M' && hdr[1] == 'Z') || (hdr[0] == 'Z' && hdr[1] == 'M'));
    struct mzhdr h;
    memcpy(&h, hdr, sizeof h);
    uint32_t imgoff = 0, imgsize = fsize, need, want;
    if (mz) {
        imgoff = (uint32_t)h.cparhdr * 16;
        uint32_t fl = h.cp ? (uint32_t)h.cp * 512 - (h.cblp ? 512 - h.cblp : 0) : 0;
        if (fl > (uint32_t)fsize || fl == 0) fl = fsize;
        if (fl < imgoff) { f_close(fd); return -11; }
        imgsize = fl - imgoff;
        uint32_t ip = (imgsize + 15) >> 4;
        need = ip + h.minalloc + 0x10;
        want = h.maxalloc == 0xFFFF ? 0xFFFFF : ip + h.maxalloc + 0x10;
        if (want < need) want = need;
    } else {
        if (fsize > 0xFF00) { f_close(fd); return -8; }
        need = ((imgsize + 0x100 + 15) >> 4) + 0x10;       /* image + a little stack */
        want = 0xFFFFF;
    }

    /* EXEPACK: the unpacker in Microsoft's packed EXEs (LINK, MASM's tools
       ...) says "Packed file is corrupt" when loaded in the first 64 KB with
       the A20 line on, as ELBOW's is.  DOS users ran LOADFIX; ELBOW does the
       same: a spacer block below the program while it is loaded. */
    int exepacked = 0;
    if (mz) {                       /* its message is near the end of the file */
        static const char msg[] = "Packed file is corrupt";
        static uint8_t tb[4096];
        long from = fsize > (long)sizeof tb ? fsize - (long)sizeof tb : 0;
        f_seek(fd, from, 0);
        long k = f_read(fd, tb, sizeof tb);
        for (long i = 0; i + (long)sizeof msg - 1 <= k && !exepacked; i++)
            if (tb[i] == 'P' && !memcmp(tb + i, msg, sizeof msg - 1)) exepacked = 1;
    }

    uint16_t env = 0;
    int err = make_env(envsrc, envflat, path, &env);
    if (err) { f_close(fd); return err; }

    /* the largest block */
    uint16_t seg, big, spacer = 0;
    arena_alloc(0xFFFF, &seg, &big);
    if (exepacked && big > need + 0x1000 && arena_alloc(0x1000, &spacer, &big) != 0) spacer = 0;
    if (spacer) arena_alloc(0xFFFF, &seg, &big);
    if (big < need) { if (spacer) arena_free(spacer); arena_free(env); f_close(fd); return -8; }
    uint32_t paras = want < big ? want : big;
    if ((err = arena_alloc(paras, &seg, &big)) != 0) { if (spacer) arena_free(spacer); arena_free(env); f_close(fd); return err; }
    uint16_t psp = seg;
    if (spacer) arena_free(spacer);
    uint16_t owner_fix = psp;
    /* owner = the new process */
    w16(LIN(psp - 1, 1), owner_fix);
    w16(LIN(env - 1, 1), owner_fix);
    mcb_name(psp, path);
    mcb_name(env, path);
    memset(hptr(LIN(env - 1, 8)), 0, 8);

    uint16_t loadseg = psp + 0x10;
    if (mz && h.minalloc == 0 && h.maxalloc == 0) {
        /* load high */
        loadseg = psp + paras - ((imgsize + 15) >> 4);
    }
    f_seek(fd, imgoff, 0);
    long got = f_read(fd, hptr(LIN(loadseg, 0)), imgsize);
    if (got < 0 || (mz && (uint32_t)got < imgsize && (uint32_t)got + 512 < imgsize)) {
        arena_free(psp); arena_free(env); f_close(fd); return -11;
    }
    x86_note_hle_write(LIN(loadseg, 0), imgsize);
    if (mz && h.crlc) {
        f_seek(fd, h.lfarlc, 0);
        uint8_t rb[256];
        uint32_t left = h.crlc;
        while (left) {
            uint32_t k = left > 64 ? 64 : left;
            if (f_read(fd, rb, k * 4) != (long)(k * 4)) break;
            for (uint32_t i = 0; i < k; i++) {
                uint16_t off = rb[i * 4] | (rb[i * 4 + 1] << 8), sg = rb[i * 4 + 2] | (rb[i * 4 + 3] << 8);
                uint32_t l = LIN(loadseg + sg, off);
                w16(l, r16(l) + loadseg);
            }
            left -= k;
        }
    }
    f_close(fd);

    build_psp(psp, psp + paras, parent ? parent : psp, env);
    if (tail) memcpy(hptr(LIN(psp, 0x80)), tail, 128);
    uint8_t *pt = hptr(LIN(psp, 0x80));
    if (pt[0] > 126) pt[0] = 126;
    pt[pt[0] + 1] = 0x0D;
    li->psp = psp; li->env = env;
    if (mz) {
        li->cs = loadseg + h.cs; li->ip = h.ip;
        li->ss = loadseg + h.ss; li->sp = h.sp;
    } else {
        li->cs = psp; li->ip = 0x100; li->ss = psp;
        uint32_t top = paras >= 0x1000 ? 0x10000 : paras * 16;
        li->sp = (top - 2) & 0xFFFE;
        w16(LIN(psp, li->sp), 0);               /* RET goes to PSP:0 (INT 20h) */
    }
    if (fcb1) memcpy(hptr(LIN(psp, 0x5C)), fcb1, 16);
    if (fcb2) memcpy(hptr(LIN(psp, 0x6C)), fcb2, 20);
    return 0;
}

static void enter(const struct loadinfo *li, uint16_t fcbax)
{
    X86 keep = cpu;
    memset(cpu.r.e, 0, sizeof cpu.r.e);
    set_sreg(SEG_CS, li->cs); cpu.eip = li->ip;
    set_sreg(SEG_SS, li->ss); rSP = li->sp;
    set_sreg(SEG_DS, li->psp); set_sreg(SEG_ES, li->psp);
    set_sreg(SEG_FS, 0); set_sreg(SEG_GS, 0);
    rAX = fcbax; rBX = 0; rCX = 0x00FF; rDX = li->psp;
    rSI = li->ip; rDI = li->sp; rBP = 0x091C;
    set_flags(0x7202);
    cpu.icount = keep.icount;
    cpu.cr0 = keep.cr0;
}

/* ------------------------------------------------------------ top level */

int x86_load_top(const char *path, const char *tail)
{
    struct armregs r;
    memset(&r, 0, sizeof r);
    r.r0 = 0x6200;
    arm21(&r);
    arm_own_psp = r.r1 & 0xFFFF;
    const struct psp *ap = (const struct psp *)((uint32_t)arm_own_psp << 4);
    const char *armenv = ap->envseg ? (const char *)((uint32_t)ap->envseg << 4) : 0;

    /* the arena */
    mcb_set(ARENA_FIRST, 'Z', 0, ARENA_END - ARENA_FIRST - 1);
    memset(hptr(LIN(ARENA_FIRST, 5)), 0, 11);

    uint8_t t[128];
    memset(t, 0, sizeof t);
    int n = strlen(tail);
    if (n > 126) n = 126;
    t[0] = n; memcpy(t + 1, tail, n); t[n + 1] = 0x0D;

    struct loadinfo li;
    cur_psp = 0;
    int e = load_program(path, 0, armenv, t, 0, 0, 0, &li);
    if (e < 0) return e;
    struct xproc *p = calloc(1, sizeof *p);
    p->psp = li.psp;
    cur_proc = p;
    cur_psp = li.psp;
    memcpy(hptr(LIN(li.psp, 0x18)), ap->jft, 20);
    uint16_t fax = parse_fcbs(li.psp);
    set_dta(li.psp, 0x80);
    enter(&li, fax);
    return 0;
}

/* ------------------------------------------------------------ EXEC */

/* the ARM program case: straight to the real EXEC */
static int exec_arm(const char *path, uint32_t pblin)
{
    struct armregs r;
    const uint8_t *pb = hptr(pblin);
    uint16_t xenv = pb[0] | (pb[1] << 8);
    uint32_t tailp = pb[2] | (pb[3] << 8) | (pb[4] << 16) | ((uint32_t)pb[5] << 24);
    uint32_t f1 = pb[6] | (pb[7] << 8) | (pb[8] << 16) | ((uint32_t)pb[9] << 24);
    uint32_t f2 = pb[10] | (pb[11] << 8) | (pb[12] << 16) | ((uint32_t)pb[13] << 24);
    if (!xenv) xenv = r16(LIN(cur_psp, 0x2C));
    /* the environment must live in ARM conventional memory */
    uint16_t aenv = 0;
    if (xenv) {
        const char *e = (const char *)hptr(LIN(xenv, 0));
        uint32_t len = 0;
        while (e[len] && len < 32767) { while (e[len]) len++; len++; }
        memset(&r, 0, sizeof r);
        r.r0 = 0x4800; r.r1 = (len + 2 + 15) >> 4;
        if (!arm21(&r)) {
            aenv = r.r0;
            char *d = (char *)((uint32_t)aenv << 4);
            memcpy(d, e, len); d[len] = 0; d[len + 1] = 0;
        }
    }
    uint8_t apb[14];
    uint32_t tl = tailp ? (uint32_t)hptr(LIN(tailp >> 16, tailp & 0xFFFF)) : 0;
    uint32_t a1 = f1 ? (uint32_t)hptr(LIN(f1 >> 16, f1 & 0xFFFF)) : 0;
    uint32_t a2 = f2 ? (uint32_t)hptr(LIN(f2 >> 16, f2 & 0xFFFF)) : 0;
    apb[0] = aenv; apb[1] = aenv >> 8;
    memcpy(apb + 2, &tl, 4); memcpy(apb + 6, &a1, 4); memcpy(apb + 10, &a2, 4);
    memset(&r, 0, sizeof r);
    r.r0 = 0x4B00; r.r1 = (uint32_t)apb; r.r3 = (uint32_t)path;
    x86_active = 0;
    irq_flush_to_bios();
    int cf = arm21(&r);
    x86_active = 1;
    uint32_t ax = r.r0 & 0xFFFF;
    if (aenv) { memset(&r, 0, sizeof r); r.r0 = 0x4900; r.r8 = aenv; arm21(&r); }
    if (cf) return -(int)ax;
    memset(&r, 0, sizeof r);
    r.r0 = 0x4D00;
    arm21(&r);
    last_exit = r.r0 & 0xFFFF;
    /* the child may have changed the DTA (the kernel resets it to our PSP:80h) */
    set_dta(dta_far >> 16, dta_far & 0xFFFF);
    return 0;
}

/* AH=4Bh. path = host string. Returns HLE_SWITCH when the child is entered,
 * HLE_DONE otherwise (CF/AX set). */
int x86_exec(const char *path, int al, uint32_t pblin)
{
    uint8_t hdr[64];
    int fd = f_open(path);
    if (fd < 0) { rAX = -fd; set_cf(1); return HLE_DONE; }
    memset(hdr, 0, sizeof hdr);
    long n = f_read(fd, hdr, 64);
    long fsize = f_seek(fd, 0, 2);
    int arm = is_arm_image(hdr, (int)n, fsize, fd);
    f_close(fd);

    if (al == 3) {
        /* overlay: +0 load segment, +2 relocation factor */
        if (arm) { rAX = 11; set_cf(1); return HLE_DONE; }
        uint16_t lseg = r16(pblin), rf = r16(pblin + 2);
        fd = f_open(path);
        int mz = n >= 28 && hdr[0] == 'M' && hdr[1] == 'Z';
        struct mzhdr h; memcpy(&h, hdr, sizeof h);
        uint32_t off = mz ? h.cparhdr * 16u : 0, size = fsize - off;
        if (mz && h.cp) { uint32_t fl = h.cp * 512u - (h.cblp ? 512 - h.cblp : 0); if (fl <= (uint32_t)fsize) size = fl - off; }
        f_seek(fd, off, 0);
        f_read(fd, hptr(LIN(lseg, 0)), size);
        x86_note_hle_write(LIN(lseg, 0), size);
        if (mz && h.crlc) {
            f_seek(fd, h.lfarlc, 0);
            for (uint32_t i = 0; i < h.crlc; i++) {
                uint8_t e[4];
                if (f_read(fd, e, 4) != 4) break;
                uint32_t l = LIN(lseg + (e[2] | (e[3] << 8)), e[0] | (e[1] << 8));
                w16(l, r16(l) + rf);
            }
        }
        f_close(fd);
        set_cf(0);
        return HLE_DONE;
    }

    if (arm) {
        if (al != 0) { rAX = 11; set_cf(1); return HLE_DONE; }
        int e = exec_arm(path, pblin);
        if (e < 0) { rAX = -e; set_cf(1); } else set_cf(0);
        return HLE_DONE;
    }

    /* an x86 child, run inside this box */
    const uint8_t *pb = hptr(pblin);
    uint16_t xenv = pb[0] | (pb[1] << 8);
    uint32_t tailp = pb[2] | (pb[3] << 8) | (pb[4] << 16) | ((uint32_t)pb[5] << 24);
    uint32_t f1 = pb[6] | (pb[7] << 8) | (pb[8] << 16) | ((uint32_t)pb[9] << 24);
    uint32_t f2 = pb[10] | (pb[11] << 8) | (pb[12] << 16) | ((uint32_t)pb[13] << 24);
    if (!xenv) xenv = r16(LIN(cur_psp, 0x2C));
    uint8_t tail[128], fcb1[16], fcb2[20];
    memcpy(tail, hptr(LIN(tailp >> 16, tailp & 0xFFFF)), 128);
    if (f1) memcpy(fcb1, hptr(LIN(f1 >> 16, f1 & 0xFFFF)), 16); else { memset(fcb1, 0, 16); memset(fcb1 + 1, ' ', 11); }
    if (f2) memcpy(fcb2, hptr(LIN(f2 >> 16, f2 & 0xFFFF)), 16); else { memset(fcb2, 0, 16); memset(fcb2 + 1, ' ', 11); }
    memset(fcb2 + 16, 0, 4);

    /* a shadow ARM PSP, so the child has its own handle table */
    struct armregs r;
    uint16_t shadow = 0;
    memset(&r, 0, sizeof r);
    r.r0 = 0x4800; r.r1 = 0x10;
    if (arm21(&r)) { rAX = 8; set_cf(1); return HLE_DONE; }
    shadow = r.r0 & 0xFFFF;

    struct loadinfo li;
    uint16_t parent_psp = cur_psp;
    int e = load_program(path, xenv, 0, tail, fcb1, fcb2, parent_psp, &li);
    if (e < 0) {
        memset(&r, 0, sizeof r); r.r0 = 0x4900; r.r8 = shadow; arm21(&r);
        rAX = -e; set_cf(1); return HLE_DONE;
    }
    memset(&r, 0, sizeof r);
    r.r0 = 0x5500; r.r3 = shadow; r.r4 = shadow + 0x10;
    arm21(&r);
    memset(&r, 0, sizeof r);
    r.r0 = 0x5000; r.r1 = shadow;
    arm21(&r);

    struct xproc *p = calloc(1, sizeof *p);
    p->psp = li.psp;
    p->arm_psp = shadow;
    p->parent = cur_proc;
    p->saved_dta = dta_far;
    /* the parent resumes after its INT 21h with CF clear */
    set_cf(0);
    get_flags();
    p->saved = cpu;
    p->saved_direct = hle_in_direct();
    uint32_t ret22 = ((uint32_t)cpu.sreg[SEG_CS] << 16) | (cpu.eip & 0xFFFF);
    p->int22 = ret22;
    uint8_t *pp = hptr(LIN(li.psp, 0));
    memcpy(pp + 0x0A, &ret22, 4);

    {
        uint8_t *iv = hptr(0x22 * 4);
        memcpy(iv, &ret22, 4);
    }
    /* the parent's SS:SP, as DOS keeps it at PSP:2Eh */
    w16(LIN(parent_psp, 0x2E), rSP); w16(LIN(parent_psp, 0x30), cpu.sreg[SEG_SS]);

    cur_proc = p;
    cur_psp = li.psp;
    jft_sync();
    set_dta(li.psp, 0x80);
    uint8_t v1 = 0, v2 = 0;
    (void)v1; (void)v2;
    uint16_t fax = 0;
    {
        /* FCB drive validity: FF if the drive letter is invalid */
        uint8_t d1 = fcb1[0], d2 = fcb2[0];
        struct armregs q;
        memset(&q, 0, sizeof q);
        q.r0 = 0x1900; arm21(&q);
        uint8_t lastdrv = 26;
        if (d1 > lastdrv) fax |= 0x00FF;
        if (d2 > lastdrv) fax |= 0xFF00;
    }
    if (al == 1) {
        /* load, don't execute: report the entry state, the child is current */
        uint8_t *q = hptr(pblin);
        uint16_t sp = li.sp - 2;
        w16(LIN(li.ss, sp), fax);                 /* DOS leaves AX on the child's stack */
        q[0x0E] = sp; q[0x0F] = sp >> 8; q[0x10] = li.ss; q[0x11] = li.ss >> 8;
        q[0x12] = li.ip; q[0x13] = li.ip >> 8; q[0x14] = li.cs; q[0x15] = li.cs >> 8;
        set_cf(0);
        return HLE_DONE;
    }
    enter(&li, fax);
    return HLE_SWITCH;
}

/* ------------------------------------------------------------ terminate */

static void end_shadow(struct xproc *p)
{
    struct armregs r;
    if (!p->arm_psp) return;
    /* close what the child left open (in its own table) */
    const uint8_t *jft = (const uint8_t *)(((uint32_t)p->arm_psp << 4) + 0x18);
    for (int h = 0; h < 20; h++) {
        if (jft[h] == 0xFF) continue;
        memset(&r, 0, sizeof r); r.r0 = 0x3E00; r.r1 = h; arm21(&r);
    }
    memset(&r, 0, sizeof r);
    r.r0 = 0x5000;
    r.r1 = p->parent && p->parent->arm_psp ? p->parent->arm_psp : arm_own_psp;
    arm21(&r);
    memset(&r, 0, sizeof r); r.r0 = 0x4900; r.r8 = p->arm_psp; arm21(&r);
}

void x86_terminate(int code, int type)
{
    if (x86_defer_exit(code, type)) return;
    struct xproc *p = cur_proc;
    last_exit = ((type & 0xFF) << 8) | (code & 0xFF);
    if (p && cur_psp != p->psp) {
        /* the current PSP was made by the program itself (AH=26h/55h + 50h, a
           debugger's child): end that one, DOS style - back to its INT 22h */
        const uint8_t *cp = hptr(LIN(cur_psp, 0));
        uint32_t v22, v23, v24;
        memcpy(&v22, cp + 0x0A, 4); memcpy(&v23, cp + 0x0E, 4); memcpy(&v24, cp + 0x12, 4);
        memcpy(hptr(0x22 * 4), &v22, 4); memcpy(hptr(0x23 * 4), &v23, 4); memcpy(hptr(0x24 * 4), &v24, 4);
        uint16_t parent = r16(LIN(cur_psp, 0x16));
        if (type != 3) arena_free_owner(cur_psp);
        cur_psp = parent;
        set_dta(parent, 0x80);
        cpu_far_jump(v22 >> 16, v22 & 0xFFFF);
        return;
    }
    if (!p || !p->parent) {
        exit_code = code & 0xFF;
        x86_exited = 1;
        cpu_stop_run(STOP_EXIT);
        return;
    }
    const uint8_t *pp = hptr(LIN(p->psp, 0));
    uint32_t v22, v23, v24;
    memcpy(&v22, pp + 0x0A, 4); memcpy(&v23, pp + 0x0E, 4); memcpy(&v24, pp + 0x12, 4);
    memcpy(hptr(0x22 * 4), &v22, 4); memcpy(hptr(0x23 * 4), &v23, 4); memcpy(hptr(0x24 * 4), &v24, 4);
    if (type != 3) arena_free_owner(p->psp);
    end_shadow(p);

    uint64_t ic = cpu.icount;
    cpu = p->saved;
    cpu.icount = ic;
    cpu.stop = 0;
    if (v22 != p->int22) cpu_far_jump(v22 >> 16, v22 & 0xFFFF);
    else if (!p->saved_direct) {
        /* the parent's EXEC came through the INT 21h stub: CF in the IRET frame */
        uint32_t fl = cpu.sbase[SEG_SS] + ((rSP + 4) & 0xFFFF);
        w16(fl, r16(fl) & ~F_CF);
    }
    cur_proc = p->parent;
    cur_psp = cur_proc->psp;
    set_dta(p->saved_dta >> 16, p->saved_dta & 0xFFFF);
    jft_sync();
    free(p);
}

/* INT 21h AH=31h / INT 27h: keep 'paras' paragraphs */
void x86_keep(uint32_t paras, int code)
{
    uint16_t big;
    if (paras < 6) paras = 6;
    arena_resize(cur_psp, paras, &big);
    x86_terminate(code, 3);
}

/* INT 21h AH=4Dh */
uint16_t x86_take_exit(void) { uint16_t v = last_exit; last_exit = 0; return v; }

/* INT 21h AH=26h: copy the current PSP to seg (DOS 1/2 style) */
void x86_create_psp(uint16_t seg, int child)
{
    uint8_t *d = hptr(LIN(seg, 0));
    const uint8_t *s = hptr(LIN(cur_psp, 0));
    memmove(d, s, 256);
    /* AH=55h (DOS 3+): SI = the value for the memory size field (the end
       of the new program's block); AH=26h copies the current one */
    uint16_t top = child ? rSI : r16(LIN(cur_psp, 2));
    d[2] = top; d[3] = top >> 8;
    if (child) {
        d[0x16] = cur_psp; d[0x17] = cur_psp >> 8;
        d[0x34] = 0x18; d[0x35] = 0; d[0x36] = seg; d[0x37] = seg >> 8;
    }
    uint32_t v22 = rd32(0x22 * 4), v23 = rd32(0x23 * 4), v24 = rd32(0x24 * 4);
    memcpy(d + 0x0A, &v22, 4); memcpy(d + 0x0E, &v23, 4); memcpy(d + 0x12, &v24, 4);
}
