/*
 * DEBUG for ARM-DOS - DOS 4.00's DEBUG re-imagined for an ARM PC.
 *
 * The command language, prompts, output formats and error pointer are those of
 * MS-DOS 4.00 DEBUG (CMD/DEBUG in Microsoft's MIT-licensed source, and
 * its documentation); what the CPU forces to change has changed: sixteen
 * 32-bit registers and the CPSR instead of the 8086's, flat 8-digit addresses,
 * an ARM/Thumb disassembler and an ARM assembler, and breakpoints made of
 * BKPT instructions (see run.c and trap.S).
 *
 * Addresses: 5 to 8 hex digits are a flat address (00012340); 1 to 4 digits
 * are an offset from the program's PSP, as DEBUG's offsets are from its
 * CS/DS (so "A 100" is where a .COM starts); SEG:OFF is SEG*16+OFF.
 */
#include <string.h>
#include "dbg.h"

JMPBUF cmdjb;
volatile int ctrlc_hit, abort24;
uint16_t my_psp, user_psp;
uint32_t ram_end = 0x01000000;
uint32_t ureg[NREGS];

extern uint32_t cstop[NREGS];
extern volatile int ctrlc_stop;

static uint16_t scratch;                /* DEBUG's own block for the program (no file loaded) */
static uint32_t user_end;               /* end of the program's memory block */
static int child;                       /* a program loaded with EXEC AL=01h */
static int quitting;
static uint32_t base;                   /* the "segment": the program's PSP address */
static uint32_t dump_addr, dis_addr, asm_addr;   /* dis_addr bit 0 = Thumb */
static char fname[80];                  /* N: the file name */
static char ftail[128];                 /* N: what follows it */
static int named;
static armdos_vect_t old23, old24;

/* ---------------------------------------------------------------- memory */

int ram_ok(uint32_t a, uint32_t n)
{
    return a + n >= a && a + n <= ram_end;
}

static int readable(uint32_t a)
{
    return a < ram_end || (a >= 0x10000000u && a < 0x10010000u) ||
           (a >= 0x11000000u && a < 0x11002000u) || a >= 0xFFF00000u;
}

uint8_t peek8(uint32_t a) { return readable(a) ? *(volatile uint8_t *)a : 0xFF; }
uint32_t peek16(uint32_t a) { return peek8(a) | (peek8(a + 1) << 8); }
uint32_t peek32(uint32_t a)
{
    if (!(a & 3) && a < ram_end) return *(volatile uint32_t *)a;
    return peek16(a) | (peek16(a + 2) << 16);
}
static void poke8(uint32_t a, uint8_t v) { if (readable(a)) *(volatile uint8_t *)a = v; }

/* ---------------------------------------------------------------- DOS */

int dos(struct armregs *r)
{
    int cf = _armdos_int21(r);
    if ((ctrlc_hit || abort24) && !quitting) {
        ctrlc_hit = abort24 = 0;
        LONGJMP(cmdjb, 1);
    }
    return cf;
}

void set_psp(uint16_t seg)
{
    struct armregs r = { 0 };
    r.r0 = 0x5000;
    r.r1 = seg;
    dos(&r);
}

/* INT 23h: ^C at DEBUG's prompt -> back to the prompt; ^C in the program -> stop it */
static void int23(struct armregs *f)
{
    f->cpsr &= ~ARM_CPSR_C;
    if (running) {
        uint32_t at = f->pc | ((f->cpsr & CPSR_T) ? 1 : 0);
        if (!ctrlc_stop && plant(at)) {
            memcpy(cstop, &f->r0, sizeof cstop);
            ctrlc_stop = 1;
            f->r0 = 0x0B00;             /* the call is re-issued: make it harmless */
            return;
        }
        f->cpsr |= ARM_CPSR_C;          /* abort the program */
        return;
    }
    ctrlc_hit = 1;
    f->r0 = 0x0B00;
}

/* INT 24h: the parent's handler decides; an Abort of DEBUG itself becomes Fail */
static void int24(struct armregs *f)
{
    if (old24) armdos_callold(old24, f);
    else f->r0 = (f->r0 & ~0xFFu) | 3;
    if ((f->r0 & 0xFF) == 2 && !running) {
        f->r0 = (f->r0 & ~0xFFu) | 3;
        abort24 = 1;
    }
}

/* ---------------------------------------------------------------- output */

static char obuf[256];
static int olen, col;

void flush(void)
{
    if (!olen) return;
    struct armregs r = { 0 };
    int n = olen;
    olen = 0;
    r.r0 = 0x4000;
    r.r1 = 1;
    r.r2 = n;
    r.r3 = (uint32_t)obuf;
    dos(&r);
}

void outc(int c)
{
    if (olen == (int)sizeof obuf) flush();
    obuf[olen++] = c;
    if (c == '\r') col = 0;
    else if (c == '\b') { if (col) col--; }
    else if (c == '\t') col = (col | 7) + 1;
    else if (c != '\n') col++;
}

void outs(const char *s) { while (*s) outc(*s++); }

void outhex(uint32_t v, int digits)
{
    while (digits--) outc("0123456789ABCDEF"[(v >> (digits * 4)) & 15]);
}

void crlf(void) { outs("\r\n"); flush(); }

static void spaces(int n) { while (n-- > 0) outc(' '); }
static void tocol(int c) { do outc(' '); while (col < c); }

/* ---------------------------------------------------------------- input */

static uint8_t kbuf[2 + 80] = { 80, 0, '\r' };  /* AH=0Ah buffer, kept: F3 repeats */
static char line[84];
static const char *sp;                  /* the parse cursor in line[] */

/* INBUF: read a line, upper-case what is not in quotes, echo CR LF */
static void inbuf(void)
{
    struct armregs r = { 0 };
    flush();
    r.r0 = 0x0A00;
    r.r3 = (uint32_t)kbuf;
    dos(&r);
    int n = kbuf[1], q = 0, i;
    for (i = 0; i < n; i++) {
        char c = kbuf[2 + i];
        if (q) { if (c == q) q = 0; }
        else if (c == '"' || c == '\'') q = c;
        else if (c >= 'a' && c <= 'z') c -= 32;
        line[i] = c;
    }
    line[i] = '\r';
    line[i + 1] = 0;
    sp = line;
    col = 0;
    crlf();
}

static int getkey(void)                /* AH=01h: with echo */
{
    struct armregs r = { 0 };
    flush();
    r.r0 = 0x0100;
    dos(&r);
    int c = r.r0 & 0xFF;
    if (c == '\r') col = 0;
    else if (c == '\b') { if (col) col--; }
    else col++;
    return c;
}

/* ---------------------------------------------------------------- errors */

static int errcol = 1;                  /* width of the prompt before line[] */

__attribute__((noreturn)) static void perror_at(const char *p)
{
    spaces((int)(p - line) + errcol);
    outs("^ Error");
    crlf();
    LONGJMP(cmdjb, 1);
}

__attribute__((noreturn)) static void err2(const char *what)
{
    outs(what);
    outs(" Error");
    crlf();
    LONGJMP(cmdjb, 1);
}

__attribute__((noreturn)) static void message(const char *m)
{
    outs(m);
    flush();
    LONGJMP(cmdjb, 1);
}

/* ---------------------------------------------------------------- parsing */

static int scanb(void)
{
    while (*sp == ' ' || *sp == '\t') sp++;
    return *sp;
}

static int scanp(void)
{
    scanb();
    if (*sp == ',') { sp++; scanb(); }
    return *sp;
}

static int eol(void) { return scanb() == '\r'; }
static void geteol(void) { if (!eol()) perror_at(sp); }

static int hexdig(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/* a hex number of at most `max` digits at sp; *ndig = digits read */
static int gethx(int max, uint32_t *v, int *ndig)
{
    uint32_t x = 0;
    int n = 0;
    if (hexdig(*sp) < 0) return 0;
    while (hexdig(*sp) >= 0) {
        if (n == max) perror_at(sp);
        x = (x << 4) | hexdig(*sp++);
        n++;
    }
    *v = x;
    if (ndig) *ndig = n;
    return 1;
}

static uint32_t gethex(int max)
{
    uint32_t v;
    scanp();
    if (!gethx(max, &v, 0)) perror_at(sp);
    return v;
}

/* the "segment" a short offset belongs to, from the last address parsed */
static uint32_t lastseg;

/* ADDRESS: flat (5-8 digits), offset from the PSP (1-4 digits), or SEG:OFF */
static int get_address(const char **pp, uint32_t *out, uint32_t seg)
{
    const char *save = sp;
    uint32_t v, off;
    int n;
    sp = *pp;
    if (!gethx(8, &v, &n)) { sp = save; return 0; }
    if (*sp == ':') {
        if (n > 4) perror_at(sp);
        sp++;
        if (!gethx(4, &off, 0)) perror_at(sp);
        lastseg = v << 4;
        *out = lastseg + off;
    } else if (n <= 4) {
        lastseg = seg;
        *out = seg + v;
    } else {
        lastseg = v & 0xFFFF0000u;
        *out = v;
    }
    *pp = sp;
    sp = save;
    return 1;
}

static uint32_t address(void)
{
    uint32_t a;
    scanp();
    const char *p = sp;
    if (!get_address(&p, &a, base)) perror_at(sp);
    sp = p;
    return a;
}

/* for the assembler: a branch target is a DEBUG address (never fails loudly) */
int asm_address(const char **pp, uint32_t *v)
{
    const char *p = *pp;
    uint32_t x = 0, off = 0;
    int n = 0, k = 0;
    while (hexdig(*p) >= 0) { if (++n > 8) return 0; x = (x << 4) | hexdig(*p++); }
    if (!n) return 0;
    if (*p == ':') {
        if (n > 4) return 0;
        p++;
        while (hexdig(*p) >= 0) { if (++k > 4) return 0; off = (off << 4) | hexdig(*p++); }
        if (!k) return 0;
        x = (x << 4) + off;
    } else if (n <= 4) x += base;
    *pp = p;
    *v = x;
    return 1;
}

/* RANGE: address [end | L length]; default length deflen */
static void range(uint32_t *start, uint32_t *len, uint32_t deflen)
{
    *start = address();
    uint32_t seg = lastseg;
    scanp();
    if (*sp == 'L') {
        sp++;
        uint32_t l = gethex(8);
        if (!l || *start + l < *start) perror_at(sp);
        *len = l;
        return;
    }
    if (hexdig(*sp) < 0) { *len = deflen; return; }
    const char *at = sp, *p = sp;
    uint32_t end;
    if (!get_address(&p, &end, seg)) perror_at(sp);
    sp = p;

    if (end < *start) perror_at(sp);
    (void)at;
    *len = end - *start + 1;
}

/* DEFAULT: a range if given, else the default address and length */
static void defrange(uint32_t *start, uint32_t *len, uint32_t defaddr, uint32_t deflen)
{
    if (scanp() == '\r') { *start = defaddr; *len = deflen; return; }
    range(start, len, deflen);
    geteol();
}

/* LIST: hex bytes and quoted strings */
static uint8_t bytebuf[84];
static int list(void)
{
    int n = 0;
    for (;;) {
        scanp();
        if (hexdig(*sp) >= 0) {
            uint32_t v;
            gethx(2, &v, 0);
            bytebuf[n++] = v;
        } else if (*sp == '"' || *sp == '\'') {
            int q = *sp++;
            for (;;) {
                if (*sp == '\r') perror_at(sp);
                if (*sp == q) { sp++; if (*sp != q) break; }
                bytebuf[n++] = *sp++;
            }
        } else break;
    }
    if (!n) perror_at(sp);
    geteol();
    return n;
}

/* ---------------------------------------------------------------- R */

static const char rnames[][5] = { "R0", "R1", "R2", "R3", "R4", "R5", "R6", "R7", "R8", "R9", "R10", "R11", "R12",
                                  "SP", "LR", "PC", "CPSR", "AX", "BX", "CX", "DX", "SI", "DI", "BP", "DS", "ES",
                                  "R13", "R14", "R15", "IP" };
static const uint8_t rindex_[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
                                   0, 1, 2, 3, 4, 5, 6, 7, 8, 13, 14, 15, 15 };

/* flags: set name, clear name, CPSR bit */
static const struct { char set[3], clr[3]; uint32_t bit; } flagtab[] = {
    { "OV", "NV", 0x10000000 }, { "DI", "EI", 0x80 }, { "NG", "PL", 0x80000000 },
    { "ZR", "NZ", 0x40000000 }, { "CY", "NC", 0x20000000 }, { "TH", "AR", CPSR_T },
};

static void show_flags(void)
{
    for (unsigned i = 0; i < sizeof flagtab / sizeof flagtab[0]; i++) {
        outs(ureg[R_CPSR] & flagtab[i].bit ? flagtab[i].set : flagtab[i].clr);
        outc(' ');
    }
}

static void show_mode(void)
{
    static const struct { uint8_t m; char n[4]; } modes[] = {
        { 0x10, "USR" }, { 0x11, "FIQ" }, { 0x12, "IRQ" }, { 0x13, "SVC" }, { 0x17, "ABT" }, { 0x1B, "UND" }, { 0x1F, "SYS" } };
    for (unsigned i = 0; i < sizeof modes / sizeof modes[0]; i++)
        if ((ureg[R_CPSR] & 0x1F) == modes[i].m) { outs(modes[i].n); return; }
    outs("???");
}

static void regpair(const char *name, uint32_t v)
{
    outs(name);
    outc('=');
    outhex(v, 8);
    outs("  ");
}

#define MEMCOL 62

/* one line of disassembly at a (bit 0 = Thumb)
: "ADDRESS HEX  mnemonic operands"; returns size */
static int unasm_line(uint32_t a, int memop)
{
    char text[96];
    int thumb = a & 1, size;
    a &= ~1u;
    outhex(a, 8);
    outc(' ');
    int c0 = col;
    if (thumb) {
        uint32_t hw = peek16(a), nx = peek16(a + 2);
        size = disasm_thumb(hw, (hw & 0xF800) == 0xF000 ? (int)nx : -1, a, text);
        outhex(hw, 4);
        if (size == 4) { outc(' '); outhex(nx, 4); }
    } else {
        uint32_t w = peek32(a);
        disasm_arm(w, a, text);
        outhex(w, 8);
        size = 4;
    }
    tocol(c0 + 15);
    /* the mnemonic at column 24, a TAB, the operands (as DEBUG 4.00 prints them);
       objdump's comments at column 56; the line padded to the memory operand column */
    const char *t = text;
    while (*t && *t != '\t') outc(*t++);
    if (*t == '\t') {
        t++;
        if (*t != '\t') outc('\t');
        while (*t && *t != '\t') outc(*t++);
        while (*t == '\t') {
            while (*t == '\t') t++;
            if (col < 56) tocol(56); else outs("  ");
            while (*t && *t != '\t') outc(*t++);
        }
    }
    int long_ = col >= MEMCOL;
    if (!long_) tocol(MEMCOL);
    if (memop) {
        /* the memory operand of a load/store, as DEBUG shows DS:0015=7510 */
        uint32_t w = thumb ? 0 : peek32(a), ea = 0;
        int sz = 0;
        if (!thumb && (w >> 28) != 15) {
            uint32_t rn = (w >> 16) & 15, base_ = rn == 15 ? a + 8 : ureg[rn];
            if ((w & 0x0C000000u) == 0x04000000u && (w & 0x02000010u) != 0x02000010u) {
                uint32_t off = w & 0xFFF;
                if (w & 0x02000000u) {
                    uint32_t rm = w & 15, v = rm == 15 ? a + 8 : ureg[rm], amt = (w >> 7) & 31;
                    switch ((w >> 5) & 3) {
                    case 0: off = v << amt; break;
                    case 1: off = amt ? v >> amt : 0; break;
                    case 2: off = (uint32_t)((int32_t)v >> (amt ? amt : 31)); break;
                    default: off = amt ? (v >> amt) | (v << (32 - amt)) : ((ureg[R_CPSR] >> 29 & 1) << 31) | (v >> 1);
                    }
                }
                ea = (w & 0x01000000u) ? ((w & 0x00800000u) ? base_ + off : base_ - off) : base_;
                sz = (w & 0x00400000u) ? 1 : 4;
            } else if ((w & 0x0E000090u) == 0x00000090u && (w & 0x60)) {
                uint32_t off = (w & 0x00400000u) ? ((w >> 4) & 0xF0) | (w & 15) : ureg[w & 15];
                ea = (w & 0x01000000u) ? ((w & 0x00800000u) ? base_ + off : base_ - off) : base_;
                uint32_t sh = (w >> 5) & 3;
                sz = sh == 1 ? 2 : (w & 0x00100000u) ? (sh == 2 ? 1 : 2) : 8;
                if (sz == 8) sz = 4;
            }
        }
        if (sz) {
            if (long_) outs("  ");
            outhex(ea, 8);

            outc('=');
            if (sz == 1) outhex(peek8(ea), 2);
            else if (sz == 2) outhex(peek16(ea), 4);
            else outhex(peek32(ea), 8);
        }
    }
    crlf();
    return size;
}

static void dispreg(void)
{
    static const char *const l1[] = { "R0(AX)", "R1(BX)", "R2(CX)", "R3(DX)", "R4(SI)", "R5(DI)", "R6(BP)", "R7(DS)",
                                      "R8(ES)", "R9", "R10", "R11", "R12" };
    for (int i = 0; i < 13; i++) {
        regpair(l1[i], ureg[i]);
        if (i == 3 || i == 7) crlf();
    }
    crlf();
    regpair("SP", ureg[R_SP]);
    regpair("LR", ureg[R_LR]);
    regpair("PC", ureg[R_PC]);
    outc(' ');
    show_flags();
    show_mode();
    crlf();
    uint32_t pc = ureg[R_PC] | ((ureg[R_CPSR] & CPSR_T) ? 1 : 0);
    dis_addr = pc;
    unasm_line(pc, 1);
}

static void cmd_r(void)
{
    if (scanp() == '\r') { dispreg(); return; }
    const char *start = sp;
    char name[8];
    int n = 0;
    while ((*sp >= 'A' && *sp <= 'Z') || (*sp >= '0' && *sp <= '9')) { if (n < 7) name[n++] = *sp; sp++; }
    name[n] = 0;
    geteol();
    if (!strcmp(name, "F")) {
        /* flags */
        show_flags();
        outs(" -");
        inbuf();
        uint32_t cpsr = ureg[R_CPSR], done = 0;
        scanb();
        while (*sp != '\r') {
            if (sp[1] == '\r') { ureg[R_CPSR] = cpsr; err2("bf"); }
            unsigned i;
            int set = 0;
            for (i = 0; i < sizeof flagtab / sizeof flagtab[0]; i++) {
                if (sp[0] == flagtab[i].set[0] && sp[1] == flagtab[i].set[1]) { set = 1; break; }
                if (sp[0] == flagtab[i].clr[0] && sp[1] == flagtab[i].clr[1]) break;
            }
            if (i == sizeof flagtab / sizeof flagtab[0]) { ureg[R_CPSR] = cpsr; err2("bf"); }
            if (done & flagtab[i].bit) { ureg[R_CPSR] = cpsr; err2("df"); }
            done |= flagtab[i].bit;
            cpsr = set ? cpsr | flagtab[i].bit : cpsr & ~flagtab[i].bit;
            sp += 2;
            scanp();
        }
        ureg[R_CPSR] = cpsr;
        return;
    }
    unsigned i;
    for (i = 0; i < sizeof rnames / sizeof rnames[0]; i++) if (!strcmp(name, rnames[i])) break;
    if (!n || i == sizeof rnames / sizeof rnames[0]) { (void)start; err2("br"); }
    int r = rindex_[i];
    outs(name);
    outc(' ');
    if (r == R_PC) outhex(ureg[R_PC] | ((ureg[R_CPSR] & CPSR_T) ? 1 : 0), 8);
    else outhex(ureg[r], 8);
    outs("\r\n:");
    inbuf();
    if (eol()) return;
    uint32_t v;
    if (!gethx(8, &v, 0)) perror_at(sp);
    geteol();
    if (r == R_PC) {
        ureg[R_PC] = v & ~1u;
        ureg[R_CPSR] = (v & 1) ? ureg[R_CPSR] | CPSR_T : ureg[R_CPSR] & ~CPSR_T;
    } else if (r == R_CPSR) {
        uint32_t m = v & CPSR_MODE;
        if (m != MODE_USR && m != MODE_SYS) perror_at(line);
        ureg[R_CPSR] = v;
    } else ureg[r] = v;
}

/* ---------------------------------------------------------------- D E F M C S H I O */

static void cmd_d(void)
{
    uint32_t a, len;
    defrange(&a, &len, dump_addr, 128);
    while (len) {
        uint32_t row = a & ~15u, skip = a - row, n = 16 - skip;
        if (n > len) n = len;
        outhex(row, 8);
        outc(' ');
        spaces(3 * skip);
        for (uint32_t i = skip; i < skip + n; i++) {
            outc(i == 8 && i != skip ? '-' : ' ');
            outhex(peek8(row + i), 2);
        }
        spaces(3 * (16 - skip - n) + 3 + skip);
        for (uint32_t i = skip; i < skip + n; i++) {
            uint8_t c = peek8(row + i);
            outc(c >= 0x20 && c < 0x7F ? c : '.');
        }
        crlf();
        a += n;
        len -= n;
        if (len) {
            /* DOS looks for ^C on 0Bh */
            struct armregs r = { 0 };
            r.r0 = 0x0B00;
            dos(&r);
        }
    }
    dump_addr = a;
}

static void cmd_e(void)
{
    uint32_t a = address();
    if (scanb() != '\r') {
        int n = list();
        for (int i = 0; i < n; i++) poke8(a + i, bytebuf[i]);
        return;
    }
    for (;;) {
        /* a new row: the address */
        outhex(a, 8);
        outs("  ");
        for (;;) {
            outhex(peek8(a), 2);
            outc('.');
            int digits = 0;
            uint32_t v = 0;
            int c;
            for (;;) {
                c = getkey();
                int h = hexdig(c);
                if (h >= 0 && digits < 2) { v = (v << 4) | h; digits++; continue; }
                if (c == '\b' || c == 0x7F) {
                    if (c == 0x7F) outc('\b');
                    if (!digits) { outc('.'); continue; }
                    digits--;
                    v >>= 4;
                    outs(" \b");
                    continue;
                }
                if (c == '-' || c == '\r' || c == ' ') break;
                /* anything else: rub it out */
                outc('\b');
                outs(" \b");
            }
            if (digits) poke8(a, v);
            a++;
            if (c == '\r') { crlf(); return; }
            if (c == '-') { a -= 2; crlf(); break; }
            spaces(4 - digits);
            flush();
            if (!(a & 7)) { crlf(); break; }
        }
    }
}

static void cmd_f(void)
{
    uint32_t a, len;
    scanp();
    range(&a, &len, 128);
    int n = list();
    for (uint32_t i = 0; i < len; i++) poke8(a + i, bytebuf[i % n]);
}

static void cmd_m(void)
{
    uint32_t a, len;
    scanp();
    range(&a, &len, 128);
    uint32_t d = address();
    geteol();
    if (d < a) for (uint32_t i = 0; i < len; i++) poke8(d + i, peek8(a + i));
    else for (uint32_t i = len; i--;) poke8(d + i, peek8(a + i));
}

static void cmd_c(void)
{
    uint32_t a, len;
    scanp();
    range(&a, &len, 128);
    uint32_t d = address();
    geteol();
    for (uint32_t i = 0; i < len; i++) {
        uint8_t x = peek8(a + i), y = peek8(d + i);
        if (x == y) continue;
        outhex(a + i, 8);
        outs("  ");
        outhex(x, 2);
        outs("  ");
        outhex(y, 2);
        outs("  ");
        outhex(d + i, 8);
        crlf();
    }
}

static void cmd_s(void)
{
    uint32_t a, len;
    scanp();
    range(&a, &len, 128);
    int n = list();
    if ((uint32_t)n > len) return;
    for (uint32_t i = 0; i + n <= len; i++) {
        int k = 0;
        while (k < n && peek8(a + i + k) == bytebuf[k]) k++;
        if (k < n) continue;
        outhex(a + i, 8);
        outc(' ');
        crlf();
    }
}

static void cmd_h(void)
{
    uint32_t x = gethex(8), y = gethex(8);
    geteol();
    outhex(x + y, 8);
    outs("  ");
    outhex(x - y, 8);
    crlf();
}

static void cmd_i(void)
{
    uint32_t port = gethex(4);
    geteol();
    outhex(armdos_inb(port), 2);
    crlf();
}

static void cmd_o(void)
{
    uint32_t port = gethex(4), v = gethex(2);
    geteol();
    armdos_outb(port, v);
}

/* ---------------------------------------------------------------- U A */

static void cmd_u(void)
{
    uint32_t a, len;
    if (scanp() == '\r') { a = dis_addr; len = 32; }
    else { range(&a, &len, 32); geteol(); }
    int thumb = a & 1;
    a &= ~1u;
    for (uint32_t done = 0; done < len;) {
        int n = unasm_line(a | thumb, 0);
        a += n;
        done += n;
    }
    dis_addr = a | thumb;
}

static void cmd_a(void)
{
    if (scanp() != '\r') { asm_addr = address(); geteol(); }
    for (;;) {
        outhex(asm_addr, 8);
        outc(' ');
        inbuf();
        if (eol()) return;
        /* ORG sets the address */
        const char *s = sp;
        if (s[0] == 'O' && s[1] == 'R' && s[2] == 'G' && (s[3] == ' ' || s[3] == '\t')) {
            sp += 3;
            uint32_t a;
            const char *p;
            scanb();
            p = sp;
            if (!get_address(&p, &a, base)) goto bad;
            sp = p;
            if (!eol()) goto bad;
            asm_addr = a;
            continue;
        }
        {
            uint8_t out[84];
            const char *e;
            int n = assemble(sp, asm_addr, out, sizeof out, &e);
            if (!n) { sp = e; goto bad; }
            for (int i = 0; i < n; i++) poke8(asm_addr + i, out[i]);
            asm_addr += n;
            continue;
        }
    bad:
        spaces((int)(sp - line) + 9);
        outs("^ Error");
        crlf();
    }
}

/* ---------------------------------------------------------------- the program's memory */

static void set_defaults(uint32_t pc, int thumb)
{
    dump_addr = pc;
    asm_addr = pc;
    dis_addr = pc | thumb;
}

static void entry_regs(uint32_t psp, uint32_t end, uint32_t spv, uint32_t pc, int thumb)
{
    memset(ureg, 0, sizeof ureg);
    ureg[0] = psp;
    ureg[1] = psp + 0x100;
    ureg[2] = end;
    ureg[R_SP] = spv;
    ureg[R_LR] = psp | 1;
    ureg[R_PC] = pc;
    ureg[R_CPSR] = MODE_SYS | 0x40 | (thumb ? CPSR_T : 0);
}

static void make_psp(uint16_t seg, uint16_t top)
{
    struct armregs r = { 0 };
    r.r0 = 0x5500;
    r.r3 = seg;
    r.r4 = top;
    dos(&r);
    set_psp(my_psp);
    struct psp *p = (struct psp *)((uint32_t)seg << 4);
    p->int22 = (uint32_t)dbg_term22;
}

static void set_tail(uint16_t seg);

/* DEBUG's own block for a program: as a .COM, all the memory there is */
static int new_scratch(int keep_regs)
{
    struct armregs r = { 0 };
    r.r0 = 0x4800;
    r.r1 = 0xFFFF;
    _armdos_int21(&r);                  /* fails: BX = largest */
    uint32_t paras = r.r1 & 0xFFFF;
    if (paras < 0x20) return 0;
    memset(&r, 0, sizeof r);
    r.r0 = 0x4800;
    r.r1 = paras;
    if (_armdos_int21(&r)) return 0;
    scratch = r.r0 & 0xFFFF;
    user_psp = scratch;
    make_psp(scratch, scratch + paras);
    set_tail(scratch);
    base = (uint32_t)scratch << 4;
    user_end = base + paras * 16;
    if (!keep_regs) {
        entry_regs(base, user_end, user_end & ~7u, base + 0x100, 0);
        set_defaults(base + 0x100, 0);
    }
    memset(&r, 0, sizeof r);
    r.r0 = 0x1A00;
    r.r3 = base + 0x80;                 /* DTA = the program's PSP:80h */
    _armdos_int21(&r);
    return 1;
}

/* the scratch PSP inherited DEBUG's handles (AH=55h counts them): give them back */
static void close_scratch_handles(void)
{
    if (!scratch) return;
    struct psp *p = (struct psp *)((uint32_t)scratch << 4);
    struct armregs r = { 0 };
    r.r0 = 0x5000;
    r.r1 = scratch;
    _armdos_int21(&r);
    for (int h = 0; h < 20; h++) {
        if (p->jft[h] == 0xFF) continue;
        memset(&r, 0, sizeof r);
        r.r0 = 0x3E00;
        r.r1 = h;
        _armdos_int21(&r);
    }
    memset(&r, 0, sizeof r);
    r.r0 = 0x5000;
    r.r1 = my_psp;
    _armdos_int21(&r);
}

static void free_scratch(void)
{
    if (!scratch) return;
    close_scratch_handles();
    struct armregs r = { 0 };
    r.r0 = 0x4900;
    r.r8 = scratch;
    _armdos_int21(&r);
    scratch = 0;
}

/* the program terminated (or was killed): DEBUG is current again */
static void after_term(void)
{
    child = 0;
    if (scratch) {
        /* re-create the PSP: DOS closed its handles */
        struct mcb *m = (struct mcb *)(((uint32_t)scratch - 1) << 4);
        make_psp(scratch, scratch + m->size);
        set_tail(scratch);
        user_psp = scratch;
    } else {
        new_scratch(1);
    }
}

/* end a program loaded with EXEC AL=01h: it executes INT 21h AH=4Ch itself */
static void kill_child(void)
{
    if (!child) return;
    uint32_t save[NREGS];
    memcpy(save, ureg, sizeof save);
    ureg[0] = 0x4C00;
    ureg[R_PC] = ((uint32_t)user_psp << 4) + 0x50;      /* svc #0x21; bx lr */
    ureg[R_CPSR] = MODE_SYS | 0x40;
    go();
    memcpy(ureg, save, sizeof save);
    child = 0;
}

/* ---------------------------------------------------------------- N L W */

/* the command tail and FCBs of the program's PSP, as DEBUG's N sets them */
static void set_tail(uint16_t seg)
{
    uint8_t *p = (uint8_t *)((uint32_t)seg << 4);
    int n = 0;
    if (named && *fname) {
        const char *s = fname;

        p[0x81 + n++] = ' ';
        while (*s && n < 125) p[0x81 + n++] = *s++;
        s = ftail;
        while (*s && n < 125) p[0x81 + n++] = *s++;
    }
    p[0x80] = n;
    p[0x81 + n] = '\r';
    struct armregs r = { 0 };
    r.r0 = 0x2901;
    r.r4 = (uint32_t)p + 0x81;
    r.r5 = (uint32_t)p + 0x5C;
    _armdos_int21(&r);
    r.r0 = 0x2901;
    r.r5 = (uint32_t)p + 0x6C;
    _armdos_int21(&r);
}

static void cmd_n(void)
{
    const char *s = sp;
    int n = 0;
    while (*s == ' ' || *s == '\t') s++;
    while (*s != '\r' && *s != ' ' && *s != '\t' && *s != ',' && *s != ';' && *s != '=' && n < 79) fname[n++] = *s++;
    fname[n] = 0;
    int k = 0;
    while (*s != '\r' && k < 126) ftail[k++] = *s++;
    ftail[k] = 0;
    named = 1;                          /* even "N" alone: W then fails to create "" */
    set_tail(user_psp);
}

static int ext_is(const char *e)
{
    const char *dot = 0;
    for (const char *s = fname; *s; s++) {
        if (*s == '.') dot = s;
        if (*s == '\\' || *s == '/' || *s == ':') dot = 0;
    }
    return dot && !strcmp(dot + 1, e);
}

static const char *const drvmsg[4] = { "Disk error reading drive ", "Disk error writing drive ",
                                       "Write protect error reading drive ", "Write protect error writing drive " };

static void exec_load(void)
{
    kill_child();
    free_scratch();
    static uint8_t tail[128];
    static uint8_t fcb1[16], fcb2[16];
    static struct __attribute__((packed)) { uint16_t env; uint32_t tail, fcb1, fcb2, sp, pc; } pb;
    int n = 0;
    for (const char *s = ftail; *s && n < 126; s++) tail[1 + n++] = *s;
    tail[0] = n;
    tail[1 + n] = '\r';
    struct armregs r = { 0 };
    memset(fcb1, 0, 16); memset(fcb2, 0, 16);
    r.r0 = 0x2901; r.r4 = (uint32_t)tail + 1; r.r5 = (uint32_t)fcb1; _armdos_int21(&r);
    r.r0 = 0x2901; r.r5 = (uint32_t)fcb2; _armdos_int21(&r);
    memset(&pb, 0, sizeof pb);
    pb.tail = (uint32_t)tail;
    pb.fcb1 = (uint32_t)fcb1;
    pb.fcb2 = (uint32_t)fcb2;
    memset(&r, 0, sizeof r);
    r.r0 = 0x4B01;
    r.r1 = (uint32_t)&pb;
    r.r3 = (uint32_t)fname;
    if (_armdos_int21(&r)) {
        set_psp(my_psp);
        new_scratch(0);
        unsigned e = r.r0 & 0xFFFF;
        /* as 4.00 shows them: its "File not found" (message 2) and "Insufficient
           memory" (message 8) never make it to the screen, only the CR LF after them */
        message(e == 2 || e == 3 ? "\r\n" : e == 5 ? "Access denied\r\n\r\n" : e == 8 ? "\r\n\r\n" :
                e == 11 ? "Error in EXE or HEX file\r\n\r\n" : "EXEC failure\r\n");
    }
    memset(&r, 0, sizeof r);
    r.r0 = 0x6200;
    _armdos_int21(&r);
    user_psp = r.r1 & 0xFFFF;
    set_psp(my_psp);
    child = 1;
    base = (uint32_t)user_psp << 4;
    struct psp *p = (struct psp *)base;
    p->int22 = (uint32_t)dbg_term22;
    user_end = (uint32_t)p->memtop << 4;
    entry_regs(base, user_end, pb.sp, pb.pc & ~1u, pb.pc & 1);
    set_defaults(pb.pc & ~1u, pb.pc & 1);
    memset(&r, 0, sizeof r);
    r.r0 = 0x1A00;
    r.r3 = base + 0x80;
    _armdos_int21(&r);
}

/* absolute sectors: L/W address drive sector count */
static void absio(int write, uint32_t a)
{
    uint32_t drive = gethex(2), sec = gethex(8), cnt = gethex(3);
    geteol();
    static struct __attribute__((packed)) { uint32_t start; uint16_t count; uint32_t buf; } pk;
    pk.start = sec;
    pk.count = cnt;
    pk.buf = a;
    struct armregs r = { 0 };
    r.r0 = 0x0D00;
    dos(&r);                            /* disk reset: flush the buffers */
    memset(&r, 0, sizeof r);
    r.r0 = drive;
    r.r1 = (uint32_t)&pk;
    r.r2 = 0xFFFF;
    int cf = dbg_absdisk(write, &r);

    if (abort24) { abort24 = 0; LONGJMP(cmdjb, 1); }  /* Abort: back to the prompt, as 4.00 */
    if (cf) {

        int wp = (r.r0 & 0xFF) == 0;
        outs(drvmsg[(wp ? 2 : 0) + write]);
        outc('A' + drive);
        crlf();
    }
    memset(&r, 0, sizeof r);
    r.r0 = 0x0D00;
    dos(&r);
}

static void cmd_l(void)
{
    uint32_t a = base + 0x100;
    int given = 0;
    if (scanp() != '\r') {
        a = address();
        given = 1;
        if (scanb() != '\r') { absio(0, a); return; }
    }
    if (!named || !*fname) LONGJMP(cmdjb, 1);   /* 4.00 says nothing */
    if (ext_is("EXE") || ext_is("COM")) {
        if (given && a != base + 0x100) perror_at(sp);
        exec_load();
        return;
    }
    struct armregs r = { 0 };
    r.r0 = 0x3D00;
    r.r3 = (uint32_t)fname;
    if (dos(&r)) LONGJMP(cmdjb, 1);     /* 4.00's "File not found" is never shown */

    uint32_t h = r.r0 & 0xFFFF;
    memset(&r, 0, sizeof r);
    r.r0 = 0x4202; r.r1 = h;
    dos(&r);
    uint32_t size = ((r.r3 & 0xFFFF) << 16) | (r.r0 & 0xFFFF);
    memset(&r, 0, sizeof r);
    r.r0 = 0x4200; r.r1 = h;
    dos(&r);
    if (a + size > user_end || a < base) {
        memset(&r, 0, sizeof r);
        r.r0 = 0x3E00; r.r1 = h;
        dos(&r);
        message("Insufficient memory\r\n");
    }
    uint32_t done = 0;
    while (done < size) {
        uint32_t k = size - done > 0xFFF0 ? 0xFFF0 : size - done;
        memset(&r, 0, sizeof r);
        r.r0 = 0x3F00; r.r1 = h; r.r2 = k; r.r3 = a + done;
        if (dos(&r) || !(r.r0 & 0xFFFF)) break;
        done += r.r0 & 0xFFFF;
    }
    memset(&r, 0, sizeof r);
    r.r0 = 0x3E00; r.r1 = h;
    dos(&r);
    ureg[1] = 0;                        /* BX:CX = the size (CX holds all 32 bits) */
    ureg[2] = size;
}

static void cmd_w(void)
{
    uint32_t a = base + 0x100;
    if (scanp() != '\r') {
        a = address();
        if (scanb() != '\r') { absio(1, a); return; }
    }
    if (!named) message("(W)rite error, no destination defined\r\n");
    if (ext_is("EXE") || ext_is("HEX")) message("EXE and HEX files cannot be written\r\n");
    struct armregs r = { 0 };
    r.r0 = 0x3C00;
    r.r3 = (uint32_t)fname;
    if (dos(&r)) {
        memset(&r, 0, sizeof r);
        r.r0 = 0x4300;
        r.r3 = (uint32_t)fname;
        if (!dos(&r) && (r.r2 & 7)) message("Access denied\r\n");
        message("File creation error\r\n");
    }
    uint32_t h = r.r0 & 0xFFFF, size = ureg[2], done = 0;
    outs("Writing ");
    outhex(size, 8);
    outs(" bytes");
    crlf();
    int bad = 0;
    while (done < size) {
        uint32_t k = size - done > 0xFFF0 ? 0xFFF0 : size - done;
        memset(&r, 0, sizeof r);
        r.r0 = 0x4000; r.r1 = h; r.r2 = k; r.r3 = a + done;
        if (dos(&r) || (r.r0 & 0xFFFF) != k) { bad = 1; break; }
        done += k;
    }
    memset(&r, 0, sizeof r);
    r.r0 = 0x3E00; r.r1 = h;
    dos(&r);
    if (bad) {
        memset(&r, 0, sizeof r);
        r.r0 = 0x4100;
        r.r3 = (uint32_t)fname;
        dos(&r);
        message("Insufficient space on disk\r\n");
    }
}

/* ---------------------------------------------------------------- G T P */

static void stopped(uint32_t why)
{
    if (why == STOP_TERM) {
        after_term();
        message("\r\nProgram terminated normally\r\n");
    }
}

/* "=address" before the other parameters of G, T and P */
static void setadd(void)
{
    if (scanp() != '=') return;
    sp++;
    uint32_t a = address();
    ureg[R_PC] = a & ~1u;
    ureg[R_CPSR] = (a & 1) ? ureg[R_CPSR] | CPSR_T : ureg[R_CPSR] & ~CPSR_T;
}

static uint32_t cur_pc(void) { return ureg[R_PC] | ((ureg[R_CPSR] & CPSR_T) ? 1 : 0); }

/* one instruction; returns the stop reason */
static uint32_t step(int over, const char *at)
{
    uint32_t pc = ureg[R_PC], npc;
    int thumb = (ureg[R_CPSR] & CPSR_T) != 0;
    if (is_call(pc, thumb, &npc) && (over || (thumb ? (peek16(pc) & 0xFF00) == 0xDF00
                                                    : (peek32(pc) & 0x0F000000u) == 0x0F000000u && (peek32(pc) >> 28) != 15))) {
        /* P over BL/BLX/SVC; T over SVC too (the handler runs in SVC mode) */
    } else {
        next_pc(&npc);
        if (npc == cur_pc()) return STOP_BP;            /* b . */
    }
    if (!plant(npc)) perror_at(at);
    return go();
}

static void cmd_g(void)
{
    const char *at = sp;
    setadd();
    uint32_t bp[11];
    int n = 0;
    while (scanp() != '\r') {
        if (n == 10) err2("bp");
        const char *p = sp;
        bp[n] = address();
        if (!ram_ok(bp[n] & ~1u, 2)) perror_at(p);
        n++;
    }
    /* a breakpoint where we stand: first get off it */
    for (int i = 0; i < n; i++) {
        if (bp[i] == cur_pc()) {
            uint32_t why = step(0, at);
            if (why != STOP_BP) { stopped(why); return; }
            break;
        }
    }
    for (int i = 0; i < n; i++) plant(bp[i]);
    uint32_t why = go();
    stopped(why);
    crlf();
    dispreg();
}

static void cmd_tp(int over)
{
    const char *at = sp - 1;
    setadd();
    uint32_t count = 1;
    if (scanp() != '\r') {
        count = gethex(8);
        if (!count) perror_at(sp - 1);
    }
    geteol();
    while (count--) {
        uint32_t why = step(over, at);
        stopped(why);
        crlf();
        dispreg();
    }
}

/* ---------------------------------------------------------------- Q */

static void cmd_q(void)
{
    geteol();
    quitting = 1;
    kill_child();
    close_scratch_handles();
    run_remove();
    armdos_setvect(0x23, old23);
    armdos_setvect(0x24, old24);
    flush();
    struct armregs r = { 0 };
    r.r0 = 0x4C00;
    _armdos_int21(&r);
}

/* ---------------------------------------------------------------- main */

int main(void)
{
    struct armregs r = { 0 };
    r.r0 = 0x6200;
    _armdos_int21(&r);
    my_psp = r.r1 & 0xFFFF;

    /* RAM size from the CMOS (extended memory KB at 30h/31h) */
    armdos_outb(0x70, 0x30);
    uint32_t kb = armdos_inb(0x71);
    armdos_outb(0x70, 0x31);
    kb |= armdos_inb(0x71) << 8;
    if (kb) ram_end = 0x100000 + kb * 1024;

    run_install();
    old23 = armdos_getvect(0x23);
    armdos_setvect(0x23, int23);
    old24 = armdos_getvect(0x24);
    armdos_setvect(0x24, int24);
    armdos_setvect(0x22, (armdos_vect_t)(void *)dbg_term22);

    /* DEBUG [[d:][path]filename [testfile-parameters]] */
    const char *t = (const char *)((uint32_t)my_psp << 4) + 0x81;
    int tl = *(uint8_t *)((uint32_t)my_psp << 4 | 0x80);
    char cmdline[130];
    if (tl > 127) tl = 127;
    memcpy(cmdline, t, tl);
    cmdline[tl] = 0;
    for (int i = 0; i < tl; i++) if (cmdline[i] == '\r') cmdline[i] = 0;
    const char *s = cmdline;
    while (*s == ' ' || *s == '\t') s++;
    if (*s) {
        int n = 0;
        while (*s && *s != ' ' && *s != '\t' && *s != '/' && n < 79) {
            char c = *s++;
            fname[n++] = c >= 'a' && c <= 'z' ? c - 32 : c;
        }
        fname[n] = 0;
        strncpy(ftail, s, sizeof ftail - 1);
        named = 1;
    }
    if (!new_scratch(0)) {
        outs("Insufficient memory\r\n");
        flush();
        cmd_q();
    }
    if (named) {
        if (!SETJMP(cmdjb)) {
            line[0] = '\r';
            sp = line;
            cmd_l();
        }
    }

    for (;;) {
        SETJMP(cmdjb);
        errcol = 1;
        unplant_all();
        outc('-');
        inbuf();
        if (eol()) continue;
        int c = *sp++;
        switch (c) {
        case 'A': cmd_a(); break;
        case 'C': cmd_c(); break;
        case 'D': cmd_d(); break;
        case 'E': cmd_e(); break;
        case 'F': cmd_f(); break;
        case 'G': cmd_g(); break;
        case 'H': cmd_h(); break;
        case 'I': cmd_i(); break;
        case 'L': cmd_l(); break;
        case 'M': cmd_m(); break;
        case 'N': cmd_n(); break;
        case 'O': cmd_o(); break;
        case 'P': cmd_tp(1); break;
        case 'Q': cmd_q(); break;
        case 'R': cmd_r(); break;
        case 'S': cmd_s(); break;
        case 'T': cmd_tp(0); break;
        case 'U': cmd_u(); break;
        case 'W': cmd_w(); break;
        case 'X':                       /* XA XD XM XS: EMS - this machine has none */
            if (*sp != 'A' && *sp != 'D' && *sp != 'M' && *sp != 'S') perror_at(sp);
            outs("EMS not installed\r\n");
            flush();
            break;

        default: perror_at(sp - 1);
        }
    }
}
