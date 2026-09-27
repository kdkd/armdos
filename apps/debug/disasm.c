/*
 * disasm.c - ARMv5TE (ARM926EJ-S) disassembler for DEBUG's U command and the
 * register display: ARM and Thumb state, GNU objdump syntax.
 *
 * A C port of emu/disasm.mjs, which is checked against arm-none-eabi-objdump
 * (100% on random encodings).  Output is objdump's text: lowercase mnemonic,
 * TAB, operands, and "\t@ ..." comments; the caller lays the TABs out.
 * Registers use objdump's names (sl fp ip sp lr pc); immediates are decimal
 * with a "@ 0x.." comment for values > 32 or < -16; branch targets are
 * absolute "0x..." addresses.  Undefined encodings print ".word 0x%08x" /
 * ".short 0x%04x".  Coprocessors 10 and 11 are the VFP9-S: VFPv2 instructions
 * in objdump's UAL text (vadd.f32, vldmia, vmov r0, r1, d0, vmrs ...); every
 * other cp10/cp11 encoding (VFPv3 and later, NEON, generic forms) is UNDEFINED
 * on this machine and prints as .word.
 */
#include <stdint.h>
#include <string.h>
#include "debug.h"

static const char RN[16][3] = { "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7",
                                "r8", "r9", "sl", "fp", "ip", "sp", "lr", "pc" };
static const char CC[16][3] = { "eq", "ne", "cs", "cc", "mi", "pl", "vs", "vc",
                                "hi", "ls", "ge", "lt", "gt", "le", "", "" };
static const char SH[4][4] = { "lsl", "lsr", "asr", "ror" };
static const char DP[16][4] = { "and", "eor", "sub", "rsb", "add", "adc", "sbc", "rsc",
                                "tst", "teq", "cmp", "cmn", "orr", "mov", "bic", "mvn" };

/* ------------------------------------------------------------ string out */

static char *o;                         /* output cursor */
static int32_t vic;                     /* value for the "@ 0x" comment */
static int has_vic, unp, uReg, UReg;

static void s(const char *t) { while (*t) *o++ = *t++; }
static void c(char ch) { *o++ = ch; }
static void dec(int32_t v)
{
    char b[12]; int n = 0;
    uint32_t u = v < 0 ? -(uint32_t)v : (uint32_t)v;
    if (v < 0) c('-');
    do { b[n++] = '0' + u % 10; u /= 10; } while (u);
    while (n) c(b[--n]);
}
static void udec(uint32_t u)
{
    char b[12]; int n = 0;
    do { b[n++] = '0' + u % 10; u /= 10; } while (u);
    while (n) c(b[--n]);
}
static void hexn(uint32_t v, int digits)       /* lowercase, at least `digits` */
{
    char b[8]; int n = 0;
    do { b[n++] = "0123456789abcdef"[v & 15]; v >>= 4; } while (v);
    while (n < digits) b[n++] = '0';
    while (n) c(b[--n]);
}
static void hx(uint32_t v) { s("0x"); hexn(v, 1); }
static void hx8(uint32_t v) { s("0x"); hexn(v, 8); }

static void setvic(int32_t v) { vic = v; has_vic = 1; }

static void reg(int n, int P, int u, int U)
{
    if (P && n == 15) unp = 1;
    if (u) { if (uReg == n) unp = 1; uReg = n; }
    if (U) { if (UReg == n) unp = 1; UReg = n; }
    s(RN[n]);
}
#define R(n) s(RN[n])
static void tail(void)
{
    if (has_vic && (vic > 32 || vic < -16)) { s("\t@ "); hx((uint32_t)vic); }
    if (unp) s("\t@ <UNPREDICTABLE>");
}

/* arm_decode_shift */
static void shift(uint32_t w, int printShift)
{
    R(w & 15);
    if (w & 0xff0) {
        if (!(w & 0x10)) {
            int amt = (w >> 7) & 31, t = (w >> 5) & 3;
            if (amt == 0) { if (t == 3) { s(", rrx"); return; } amt = 32; }
            if (printShift) { s(", "); s(SH[t]); s(" #"); } else s(", #");
            dec(amt);
        } else if (w & 0x80) s("\t@ <illegal shifter operand>");
        else {
            if (printShift) { s(", "); s(SH[(w >> 5) & 3]); c(' '); } else s(", ");
            R((w >> 8) & 15);
        }
    }
}

/* shifter operand */
static void op2(uint32_t w)
{
    if (w & 0x02000000) {
        int rot = (w >> 7) & 30, imm = w & 0xff, i;
        uint32_t a = (imm >> rot) | (imm << ((32 - rot) & 31));
        for (i = 0; i < 32; i += 2) if (((a << i) | (a >> ((32 - i) & 31))) <= 0xff) break;
        setvic((int32_t)a);
        c('#');
        if (i != rot) { dec(imm); s(", "); dec(rot); }
        else dec((int32_t)a);
        return;
    }
    shift(w, 1);
}

/* addressing mode 2 */
static void addr2(uint32_t w, uint32_t pc)
{
    uint32_t P = w & 0x01000000, U = w & 0x00800000, W = w & 0x00200000;
    const char *neg = U ? "" : "-";
    if ((w & 0x020f0000) == 0x000f0000) {
        uint32_t off = w & 0xfff;
        s("[pc");
        if (P) {
            if (W || !U || off) { s(", #"); s(neg); udec(off); }
            c(']'); if (W) c('!');
            off = pc + 8 + (U ? off : -off);
        } else { s("], #"); s(neg); udec(off); off = pc + 8; }
        s("\t@ "); hx(off);
        return;
    }
    uint32_t off = 0;
    c('['); R((w >> 16) & 15);
    if (P) {
        if (!(w & 0x02000000)) { off = w & 0xfff; if (W || !U || off) { s(", #"); s(neg); udec(off); } }
        else { s(", "); s(neg); shift(w, 1); }
        c(']'); if (W) c('!');
    } else if (!(w & 0x02000000)) { off = w & 0xfff; s("], #"); s(neg); udec(off); }
    else { s("], "); s(neg); shift(w, 1); }
    setvic(U ? (int32_t)off : -(int32_t)off);
}

/* addressing mode 3 */
static void addr3(uint32_t w, uint32_t pc)
{
    uint32_t P = w & 0x01000000, U = w & 0x00800000, W = w & 0x00200000, I = w & 0x00400000;
    const char *neg = U ? "" : "-";
    uint32_t off = ((w >> 4) & 0xf0) | (w & 15);
    if ((w & 0x004f0000) == 0x004f0000) {
        if (P) {
            if (off || !U) { s("[pc, #"); s(neg); udec(off); c(']'); } else s("[pc]");
            s("\t@ "); hx(pc + 8 + (U ? off : -off));
            return;
        }
        unp = 1;
        s("[pc], #"); s(neg); udec(off);
        return;
    }
    c('['); R((w >> 16) & 15);
    if (P) {
        if (I) { if (W || !U || off) { s(", #"); s(neg); udec(off); } setvic(U ? (int32_t)off : -(int32_t)off); }
        else { s(", "); s(neg); R(w & 15); if (W && (w & 15) == ((w >> 12) & 15)) unp = 1; }
        c(']'); if (W) c('!');
        return;
    }
    if (I) { s("], #"); s(neg); udec(off); setvic(U ? (int32_t)off : -(int32_t)off); }
    else { s("], "); s(neg); R(w & 15); if ((w & 15) == ((w >> 12) & 15)) unp = 1; }
    if (W || (!I && (w & 15) == 15)) unp = 1;
}

static void reglist(uint32_t l)
{
    int first = 1;
    c('{');
    for (int i = 0; i < 16; i++) if (l & (1u << i)) { if (!first) s(", "); R(i); first = 0; }
    c('}');
}

/* coprocessor addressing */
static void addrcp(uint32_t w, uint32_t pc)
{
    int cp = (w >> 8) & 15, rn = (w >> 16) & 15;
    uint32_t P = w & 0x01000000, U = w & 0x00800000, W = w & 0x00200000;
    int32_t off = w & 0xff;
    c('['); R(rn);
    if (P || W) { off *= cp == 9 ? 2 : 4; if (!U) off = -off; if (rn != 15) setvic(off); }
    if (P) {
        if (off) { s(", #"); dec(off); c(']'); if (W) c('!'); }
        else if (!U) s(", #-0]");
        else c(']');
    } else {
        c(']');
        if (W) { if (off) { s(", #"); dec(off); } else if (!U) s(", #-0"); }
        else { s(", {"); if (!U && !off) c('-'); dec(off); c('}'); setvic(off); }
    }
    if (rn == 15 && (P || W)) { s("\t@ "); hx(off + pc + 8 - (pc & 3)); }
}

/* VFP (cp10/cp11): the VFPv2 instruction set of the VFP9-S, as emu/disasm.mjs
 * prints it (objdump's UAL text, binutils arm-dis.c table order, cut down to
 * VFPv2).  Anything else in cp10/cp11 is UNDEFINED here and prints .word.
 * Format codes: %c cond, %A coprocessor address, %yN / %zN single / double
 * register (0 Vm, 1 Vd, 2 Vn, 3 {list}, 4 "Sm, Sm+1"), %B D list, %<a-b>k field
 * with k = r core reg, R (UNPREDICTABLE if pc), U (also if equal to the previous
 * U), d decimal, D d-reg, 'X / `X X if 1 / 0, ?.. pick a char. */
struct vrow { uint32_t v, m; const char *f; };
static const struct vrow VFP[] = {
    { 0x0d2d0b00, 0x0fff0f01, "vpush%c\t%B" },
    { 0x0d200b00, 0x0ff00f01, "vstmdb%c\t%16-19r!, %B" },
    { 0x0d300b00, 0x0ff00f01, "vldmdb%c\t%16-19r!, %B" },
    { 0x0c800b00, 0x0fd00f01, "vstmia%c\t%16-19r%21'!, %B" },
    { 0x0cbd0b00, 0x0fff0f01, "vpop%c\t%B" },
    { 0x0c900b00, 0x0fd00f01, "vldmia%c\t%16-19r%21'!, %B" },
    { 0x0d000b00, 0x0f700f00, "vstr%c\t%12-15D, %A" },
    { 0x0d100b00, 0x0f700f00, "vldr%c\t%12-15D, %A" },
    { 0x0d2d0a00, 0x0fbf0f00, "vpush%c\t%y3" },
    { 0x0d200a00, 0x0fb00f00, "vstmdb%c\t%16-19r!, %y3" },
    { 0x0d300a00, 0x0fb00f00, "vldmdb%c\t%16-19r!, %y3" },
    { 0x0c800a00, 0x0f900f00, "vstmia%c\t%16-19r%21'!, %y3" },
    { 0x0cbd0a00, 0x0fbf0f00, "vpop%c\t%y3" },
    { 0x0c900a00, 0x0f900f00, "vldmia%c\t%16-19r%21'!, %y3" },
    { 0x0d000a00, 0x0f300f00, "vstr%c\t%y1, %A" },
    { 0x0d100a00, 0x0f300f00, "vldr%c\t%y1, %A" },
    { 0x0d200b01, 0x0ff00f01, "fstmdbx%c\t%16-19r!, %z3\t@ Deprecated" },
    { 0x0d300b01, 0x0ff00f01, "fldmdbx%c\t%16-19r!, %z3\t@ Deprecated" },
    { 0x0c800b01, 0x0fd00f01, "fstmiax%c\t%16-19r%21'!, %z3\t@ Deprecated" },
    { 0x0c900b01, 0x0fd00f01, "fldmiax%c\t%16-19r%21'!, %z3\t@ Deprecated" },
    { 0x0c400b10, 0x0ff00ff0, "vmov%c\t%0-3D, %12-15R, %16-19R" },
    { 0x0c500b10, 0x0ff00ff0, "vmov%c\t%12-15U, %16-19U, %0-3D" },
    { 0x0c400a10, 0x0ff00fd0, "vmov%c\t%y4, %12-15R, %16-19R" },
    { 0x0c500a10, 0x0ff00fd0, "vmov%c\t%12-15U, %16-19U, %y4" },
    { 0x0e000b10, 0x0fd00ff0, "vmov%c.32\t%16-19D[%21d], %12-15R" },
    { 0x0e100b10, 0x0fd00ff0, "vmov%c.32\t%12-15R, %16-19D[%21d]" },
    { 0x0ee00a10, 0x0fff0fff, "vmsr%c\tfpsid, %12-15R" },
    { 0x0ee10a10, 0x0fff0fff, "vmsr%c\tfpscr, %12-15R" },
    { 0x0ee80a10, 0x0fff0fff, "vmsr%c\tfpexc, %12-15R" },
    { 0x0ee90a10, 0x0fff0fff, "vmsr%c\tfpinst, %12-15R\t@ Impl def" },
    { 0x0eea0a10, 0x0fff0fff, "vmsr%c\tfpinst2, %12-15R\t@ Impl def" },
    { 0x0ef00a10, 0x0fff0fff, "vmrs%c\t%12-15R, fpsid" },
    { 0x0ef1fa10, 0x0fffffff, "vmrs%c\tAPSR_nzcv, fpscr" },
    { 0x0ef10a10, 0x0fff0fff, "vmrs%c\t%12-15r, fpscr" },
    { 0x0ef80a10, 0x0fff0fff, "vmrs%c\t%12-15R, fpexc" },
    { 0x0ef90a10, 0x0fff0fff, "vmrs%c\t%12-15R, fpinst\t@ Impl def" },
    { 0x0efa0a10, 0x0fff0fff, "vmrs%c\t%12-15R, fpinst2\t@ Impl def" },
    { 0x0e000a10, 0x0ff00f7f, "vmov%c\t%y2, %12-15R" },
    { 0x0e100a10, 0x0ff00f7f, "vmov%c\t%12-15R, %y2" },
    { 0x0eb50a40, 0x0fbf0f70, "vcmp%7'e%c.f32\t%y1, #0.0" },
    { 0x0eb50b40, 0x0fff0f70, "vcmp%7'e%c.f64\t%z1, #0.0" },
    { 0x0eb00a40, 0x0fbf0fd0, "vmov%c.f32\t%y1, %y0" },
    { 0x0eb00ac0, 0x0fbf0fd0, "vabs%c.f32\t%y1, %y0" },
    { 0x0eb00b40, 0x0fff0ff0, "vmov%c.f64\t%z1, %z0" },
    { 0x0eb00bc0, 0x0fff0ff0, "vabs%c.f64\t%z1, %z0" },
    { 0x0eb10a40, 0x0fbf0fd0, "vneg%c.f32\t%y1, %y0" },
    { 0x0eb10ac0, 0x0fbf0fd0, "vsqrt%c.f32\t%y1, %y0" },
    { 0x0eb10b40, 0x0fff0ff0, "vneg%c.f64\t%z1, %z0" },
    { 0x0eb10bc0, 0x0fff0ff0, "vsqrt%c.f64\t%z1, %z0" },
    { 0x0eb70ac0, 0x0fff0fd0, "vcvt%c.f64.f32\t%z1, %y0" },
    { 0x0eb70bc0, 0x0fbf0ff0, "vcvt%c.f32.f64\t%y1, %z0" },
    { 0x0eb80a40, 0x0fbf0f50, "vcvt%c.f32.%7?su32\t%y1, %y0" },
    { 0x0eb80b40, 0x0fff0f50, "vcvt%c.f64.%7?su32\t%z1, %y0" },
    { 0x0eb40a40, 0x0fbf0f50, "vcmp%7'e%c.f32\t%y1, %y0" },
    { 0x0eb40b40, 0x0fff0f70, "vcmp%7'e%c.f64\t%z1, %z0" },
    { 0x0ebc0a40, 0x0fbe0f50, "vcvt%7`r%c.%16?su32.f32\t%y1, %y0" },
    { 0x0ebc0b40, 0x0fbe0f70, "vcvt%7`r%c.%16?su32.f64\t%y1, %z0" },
    { 0x0e000a00, 0x0fb00f50, "vmla%c.f32\t%y1, %y2, %y0" },
    { 0x0e000a40, 0x0fb00f50, "vmls%c.f32\t%y1, %y2, %y0" },
    { 0x0e000b00, 0x0ff00ff0, "vmla%c.f64\t%z1, %z2, %z0" },
    { 0x0e000b40, 0x0ff00ff0, "vmls%c.f64\t%z1, %z2, %z0" },
    { 0x0e100a00, 0x0fb00f50, "vnmls%c.f32\t%y1, %y2, %y0" },
    { 0x0e100a40, 0x0fb00f50, "vnmla%c.f32\t%y1, %y2, %y0" },
    { 0x0e100b00, 0x0ff00ff0, "vnmls%c.f64\t%z1, %z2, %z0" },
    { 0x0e100b40, 0x0ff00ff0, "vnmla%c.f64\t%z1, %z2, %z0" },
    { 0x0e200a00, 0x0fb00f50, "vmul%c.f32\t%y1, %y2, %y0" },
    { 0x0e200a40, 0x0fb00f50, "vnmul%c.f32\t%y1, %y2, %y0" },
    { 0x0e200b00, 0x0ff00ff0, "vmul%c.f64\t%z1, %z2, %z0" },
    { 0x0e200b40, 0x0ff00ff0, "vnmul%c.f64\t%z1, %z2, %z0" },
    { 0x0e300a00, 0x0fb00f50, "vadd%c.f32\t%y1, %y2, %y0" },
    { 0x0e300a40, 0x0fb00f50, "vsub%c.f32\t%y1, %y2, %y0" },
    { 0x0e300b00, 0x0ff00ff0, "vadd%c.f64\t%z1, %z2, %z0" },
    { 0x0e300b40, 0x0ff00ff0, "vsub%c.f64\t%z1, %z2, %z0" },
    { 0x0e800a00, 0x0fb00f50, "vdiv%c.f32\t%y1, %y2, %y0" },
    { 0x0e800b00, 0x0ff00ff0, "vdiv%c.f64\t%z1, %z2, %z0" },
};

static int fnum(const char **fp)
{
    int a = 0;
    while (**fp >= '0' && **fp <= '9') a = a * 10 + *(*fp)++ - '0';
    return a;
}

static void vfp_fmt(uint32_t w, uint32_t pc, const char *f)
{
    for (; *f; f++) {
        if (*f != '%') { c(*f); continue; }
        int k = *++f;
        if (k == 'c') { s(CC[w >> 28]); continue; }
        if (k == 'A') { addrcp(w, pc); continue; }
        if (k == 'B') {
            int r = (w >> 12) & 15, n = (w & 0xff) >> 1, ov = (w >> 1) & 0x3f;
            if (!n || r + n > 16) unp = 1;
            s("{d"); dec(r);
            if (ov != 1) {
                if (r + ov > 32) { s("-<overflow reg d"); dec(r + ov - 1); c('>'); }
                else { s("-d"); dec(r + ov - 1); }
            }
            c('}');
            continue;
        }
        if (k == 'y' || k == 'z') {
            int sg = k == 'y', t = *++f, r;
            char pfx = sg ? 's' : 'd';
            if (t == '0' || t == '4') r = sg ? (int)((w & 15) << 1 | ((w >> 5) & 1)) : (int)(w & 15);
            else if (t == '2') r = sg ? (int)(((w >> 16) & 15) << 1 | ((w >> 7) & 1)) : (int)((w >> 16) & 15);
            else r = sg ? (int)(((w >> 12) & 15) << 1 | ((w >> 22) & 1)) : (int)((w >> 12) & 15);
            if (t == '3') {
                int n = w & 0xff;
                if (!sg) n >>= 1;
                if (!n || r + n > (sg ? 32 : 16)) unp = 1;
                c('{'); c(pfx); dec(r);
                if (--n) { c('-'); c(pfx); dec(r + n); }
                c('}');
            } else {
                if (t == '4' && r == 31) unp = 1;
                c(pfx); dec(r);
                if (t == '4') { s(", "); c(pfx); dec(r + 1); }
            }
            continue;
        }
        /* %<a[-b]>[,<a[-b]>...]<kind> */
        uint32_t v = 0; int width = 0;
        for (;;) {
            int a = fnum(&f), b = a;
            if (*f == '-') { f++; b = fnum(&f); }
            v |= ((w >> a) & ((1u << (b - a + 1)) - 1)) << width;
            width += b - a + 1;
            if (*f != ',') break;
            f++;
        }
        switch (*f) {
        case 'r': R(v); break;
        case 'R': reg(v, 1, 0, 0); break;
        case 'U': reg(v, 1, 1, 0); break;
        case 'd': udec(v); break;
        case 'D': c('d'); udec(v); break;
        case '\'': f++; if (v) c(*f); break;
        case '`': f++; if (!v) c(*f); break;
        case '?': c(f[1 + (1 << width) - 1 - v]); f += 1 << width; break;
        }
    }
}

/* cp10/cp11, cond != 1111: 1 = VFPv2 text written, 0 = UNDEFINED (.word) */
static int vfp(uint32_t w, uint32_t pc)
{
    for (unsigned i = 0; i < sizeof VFP / sizeof VFP[0]; i++)
        if ((w & VFP[i].m) == VFP[i].v) {
            /* vldm/vstm (not vldr/vstr, not mcrr/mrrc) writing back to pc */
            if ((w & 0x0e200000) == 0x0c200000 && ((w >> 16) & 15) == 15) unp = 1;
            vfp_fmt(w, pc, VFP[i].f);
            return 1;
        }
    return 0;
}

static int coproc(uint32_t w, uint32_t pc, const char *cc, int two)
{
    int cp = (w >> 8) & 15, n = (w >> 16) & 15, d = (w >> 12) & 15, m = w & 15;
    int hi = (w >> 20) & 0xff;
    if (cp == 10 || cp == 11) return two ? 0 : vfp(w, pc);
    if (cp == 9 && (two || (w >> 28) != 14)) unp = 1;
    if (!two && (hi & 0xfe) == 0xc4) {
        if (hi & 1) {
            s("mrrc"); s(cc); c('\t'); dec(cp); s(", "); dec((w >> 4) & 15); s(", ");
            reg(d, 1, 1, 0); s(", "); reg(n, 1, 1, 0); s(", cr"); dec(m);
        } else {
            s("mcrr"); s(cc); c('\t'); dec(cp); s(", "); dec((w >> 4) & 15); s(", ");
            reg(d, 1, 0, 0); s(", "); R(n); s(", cr"); dec(m);
        }
        return 1;
    }
    if (cp == 9 && (two ? (w & 0x0f000010) == 0x0e000000 : (w & 0x0f10f010) != 0x0e10f010)) return 0;
    const char *t = two ? "2" : "";
    if ((w & 0x0f000000) == 0x0e000000) {
        if (!(w & 0x10)) {
            s("cdp"); s(t); s(cc); c('\t'); dec(cp); s(", "); dec((w >> 20) & 15);
            s(", cr"); dec(d); s(", cr"); dec(n); s(", cr"); dec(m); s(", {"); dec((w >> 5) & 7); c('}');
            return 1;
        }
        s(w & 0x00100000 ? "mrc" : "mcr"); s(t); s(cc); c('\t'); dec(cp); s(", "); dec((w >> 21) & 7); s(", ");
        if (w & 0x00100000) { if (d == 15 && !two) s("APSR_nzcv"); else R(d); }
        else reg(d, 1, 0, 0);
        s(", cr"); dec(n); s(", cr"); dec(m); s(", {"); dec((w >> 5) & 7); c('}');
        return 1;
    }
    s(w & 0x00100000 ? "ldc" : "stc"); s(t); if (w & 0x00400000) c('l'); s(cc); c('\t');
    dec(cp); s(", cr"); dec(d); s(", ");
    addrcp(w, pc);
    return 1;
}

static void xy(uint32_t w) { c(w & 0x20 ? 't' : 'b'); c(w & 0x40 ? 't' : 'b'); }

static int bitcount(uint32_t v) { int n = 0; while (v) { n += v & 1; v >>= 1; } return n; }

static int arm_body(uint32_t w, uint32_t pc)
{
    int cond = w >> 28;
    const char *cc = CC[cond];
    int rd = (w >> 12) & 15, rn = (w >> 16) & 15, rm = w & 15, rs = (w >> 8) & 15;
    const char *S = (w & 0x00100000) ? "s" : "";
    if (cond == 15) {
        if ((w & 0xfe000000) == 0xfa000000) {
            s("blx\t"); hx(pc + 8 + (((int32_t)(w << 8)) >> 6) + ((w >> 23) & 2)); return 1;
        }
        if ((w & 0xfc70f000) == 0xf450f000) { s("pld\t"); addr2(w, pc); return 1; }
        if ((w & 0x0e000000) == 0x0c000000 || (w & 0x0f000000) == 0x0e000000) return coproc(w, pc, "", 1);
        return 0;
    }
    int top = (w >> 25) & 7;
    if (top >= 6) {
        if ((w & 0x0f000000) == 0x0f000000) { s("svc"); s(cc); c('\t'); hx8(w & 0xffffff); return 1; }
        return coproc(w, pc, cc, 0);
    }
    if (top == 5) {
        c('b'); if (w & 0x01000000) c('l'); s(cc); c('\t');
        hx(pc + 8 + (((int32_t)(w << 8)) >> 6));
        return 1;
    }
    if (top == 4) {
        uint32_t L = w & 0x00100000, list = w & 0xffff, x = w & 0x0fff0000;
        int bits = bitcount(list);
        if (x == 0x092d0000 || x == 0x08bd0000) {
            if (bits == 1) { s(L ? "ldmfd" : "stmfd"); s(cc); s("\tsp!, "); reglist(list); return 1; }
            if (!bits) unp = 1;
            s(L ? "pop" : "push"); s(cc); c('\t'); reglist(list);
            return 1;
        }
        static const char mode[4][3] = { "da", "ia", "db", "ib" };
        s(L ? "ldm" : "stm");
        if (!(!L && (w & 0x0ff00000) == 0x08800000) &&
            (L ? (w & 0x01800000) != 0x00800000 : ((w & 0x01f00000) != 0x00800000 || (w & 0x00400000))))
            s(mode[(w >> 23) & 3]);
        s(cc); c('\t'); reg(rn, 1, 0, 0);
        if (w & 0x00200000) c('!');
        s(", "); reglist(list);
        if (w & 0x00400000) c('^');
        return 1;
    }
    if (top == 2 || top == 3) {
        if (top == 3 && (w & 0x10)) {
            if ((w & 0xfff000f0) == 0xe7f000f0) {
                int32_t v = ((w >> 4) & 0xfff0) | (w & 15);
                setvic(v); s("udf\t#"); dec(v); return 1;
            }
            return 0;
        }
        uint32_t L = w & 0x00100000, B = w & 0x00400000;
        if ((w & 0x0fff0fff) == 0x052d0004) {
            s("push"); s(cc); s("\t{"); R(rd); s("}\t\t@ (str"); s(cc); c(' '); R(rd); s(", ");
            addr2(w, pc); c(')'); return 1;
        }
        if ((w & 0x0fff0fff) == 0x049d0004) {
            s("pop"); s(cc); s("\t{"); R(rd); s("}\t\t@ (ldr"); s(cc); c(' '); R(rd); s(", ");
            addr2(w, pc); c(')'); return 1;
        }
        int t = (w & 0x01200000) == 0x00200000;
        s(L ? "ldr" : "str"); if (B) c('b'); if (t) c('t'); s(cc); c('\t');
        reg(rd, B || (L && t), 0, 0); s(", "); addr2(w, pc);
        return 1;
    }
    /* top 0/1 */
    if (w == 0xe1a00000) { s("nop\t\t\t@ (mov r0, r0)"); return 1; }
    uint32_t f0 = w & 0x0ffffff0;
    if (f0 == 0x012fff10) { s("bx"); s(cc); c('\t'); R(rm); return 1; }
    uint32_t b74 = w & 0xf0;
    if ((w & 0x0e000090) == 0x00000090) {
        if ((w & 0x0fe000f0) == 0x00000090) {
            s("mul"); s(S); s(cc); c('\t'); reg(rn, 1, 0, 0); s(", "); reg(rm, 1, 0, 0); s(", "); reg(rs, 1, 0, 0); return 1;
        }
        if ((w & 0x0fe000f0) == 0x00200090) {
            s("mla"); s(S); s(cc); c('\t'); reg(rn, 1, 0, 0); s(", "); reg(rm, 1, 0, 0); s(", ");
            reg(rs, 1, 0, 0); s(", "); reg(rd, 1, 0, 0); return 1;
        }
        if ((w & 0x0fb00ff0) == 0x01000090) {
            s("swp"); if (w & 0x00400000) c('b'); s(cc); c('\t'); reg(rd, 1, 0, 1); s(", "); reg(rm, 1, 1, 0);
            s(", ["); reg(rn, 1, 1, 1); c(']'); return 1;
        }
        if ((w & 0x0f8000f0) == 0x00800090) {
            c(w & 0x00400000 ? 's' : 'u'); s(w & 0x00200000 ? "mlal" : "mull"); s(S); s(cc); c('\t');
            reg(rd, 1, 1, 0); s(", "); reg(rn, 1, 1, 0); s(", "); reg(rm, 1, 0, 0); s(", "); reg(rs, 1, 0, 0);
            return 1;
        }
        if ((w & 0x0e1000d0) == 0x000000d0) {
            s(b74 == 0xd0 ? "ldrd" : "strd"); s(cc); c('\t'); R(rd); s(", "); addr3(w, pc); return 1;
        }
        if ((w & 0x0e5000f0) == 0x004000b0 || (w & 0x0e500ff0) == 0x000000b0) {
            s("strh"); s(cc); c('\t'); reg(rd, 1, 0, 0); s(", "); addr3(w, pc); return 1;
        }
        if ((w & 0x0e5000f0) == 0x00500090 || (w & 0x0e500ff0) == 0x00100090) return 0;
        if ((w & 0x0e500090) == 0x00500090 || (w & 0x0e500f90) == 0x00100090) {
            s("ldr"); if (w & 0x40) c('s'); c(w & 0x20 ? 'h' : 'b'); s(cc); c('\t');
            reg(rd, 1, 0, 0); s(", "); addr3(w, pc); return 1;
        }
    }
    if (f0 == 0x012fff20) { s("bxj"); s(cc); c('\t'); reg(rm, 1, 0, 0); return 1; }
    if ((w & 0xfff000f0) == 0xe1200070) { s("bkpt\t0x"); hexn(((w >> 4) & 0xfff0) | (w & 15), 4); return 1; }
    if (f0 == 0x012fff30) { s("blx"); s(cc); c('\t'); reg(rm, 1, 0, 0); return 1; }
    if ((w & 0x0fff0ff0) == 0x016f0f10) { s("clz"); s(cc); c('\t'); reg(rd, 1, 0, 0); s(", "); reg(rm, 1, 0, 0); return 1; }
    if ((w & 0x0f900090) == 0x01000080) {
        int op = (w >> 21) & 3;
        int tt = (w & 0x60) == 0x60;
        if (op == 0) {
            s("smla"); xy(w); s(cc); c('\t'); reg(rn, !tt, 0, 0); s(", "); reg(rm, !tt, 0, 0); s(", ");
            reg(rs, 1, 0, 0); s(", "); reg(rd, 1, 0, 0); return 1;
        }
        if (op == 1) {
            if (!(w & 0x20)) {
                s("smlaw"); c(w & 0x40 ? 't' : 'b'); s(cc); c('\t'); reg(rn, 1, 0, 0); s(", ");
                reg(rm, !(w & 0x40), 0, 0); s(", "); reg(rs, 1, 0, 0); s(", "); reg(rd, 1, 0, 0); return 1;
            }
            if (!(w & 0xf000)) {
                s("smulw"); c(w & 0x40 ? 't' : 'b'); s(cc); c('\t'); reg(rn, 1, 0, 0); s(", ");
                reg(rm, 1, 0, 0); s(", "); reg(rs, 1, 0, 0); return 1;
            }
        }
        if (op == 2) {
            s("smlal"); xy(w); s(cc); c('\t'); reg(rd, 1, 1, 0); s(", "); reg(rn, 1, 1, 0); s(", ");
            reg(rm, 1, 0, 0); s(", "); reg(rs, 1, 0, 0); return 1;
        }
        if (op == 3 && !(w & 0xf000)) {
            s("smul"); xy(w); s(cc); c('\t'); reg(rn, 1, 0, 0); s(", "); reg(rm, 1, 0, 0); s(", ");
            reg(rs, 1, 0, 0); return 1;
        }
    }
    if ((w & 0x0f900ff0) == 0x01000050) {
        static const char q[4][6] = { "qadd", "qsub", "qdadd", "qdsub" };
        s(q[(w >> 21) & 3]); s(cc); c('\t'); reg(rd, 1, 0, 0); s(", "); reg(rm, 1, 0, 0); s(", "); reg(rn, 1, 0, 0);
        return 1;
    }

    int op = (w >> 21) & 15;
    uint32_t I = w & 0x02000000;
    int P = !I && (w & 0x10);
    if (P && (w & 0x80) && op != 9 && op != 13) return 0;
    if (op >= 8 && op <= 11) {
        if ((w & 0x0db0f000) == 0x0120f000) {
            s("msr"); s(cc); c('\t'); s(w & 0x00400000 ? "SPSR_" : "CPSR_");
            if (w & 0x80000) c('f');
            if (w & 0x40000) c('s');
            if (w & 0x20000) c('x');
            if (w & 0x10000) c('c');
            s(", "); op2(w);
            return 1;
        }
        if ((w & 0x0fbf0fff) == 0x010f0000) {
            s("mrs"); s(cc); c('\t'); reg(rd, 1, 0, 0); s(", "); s(w & 0x00400000 ? "SPSR" : "CPSR"); return 1;
        }
        if (!*S && op == 9) return 0;
        s(DP[op]);
        if (rd == 15) c('p');
        s(cc); c('\t'); reg(rn, P, 0, 0); s(", "); op2(w);
        if (rd == 15) s("\t@ p-variant is OBSOLETE");
        return 1;
    }
    if (op == 13 || op == 15) {
        if (rn && op == 13) return 0;
        if (op == 15 || I) { s(DP[op]); s(S); s(cc); c('\t'); reg(rd, P, 0, 0); s(", "); op2(w); return 1; }
        if (!(w & 0xff0)) { s("mov"); s(S); s(cc); c('\t'); R(rd); s(", "); R(rm); return 1; }
        int t = (w >> 5) & 3;
        if (t == 3 && (w & 0xff0) == 0x60) { s("rrx"); s(S); s(cc); c('\t'); R(rd); s(", "); R(rm); return 1; }
        s(SH[t]); s(S); s(cc); c('\t'); reg(rd, 1, 0, 0); s(", "); shift(w, 0);
        return 1;
    }
    s(DP[op]); s(S); s(cc); c('\t'); reg(rd, P, 0, 0); s(", "); reg(rn, P, 0, 0); s(", "); op2(w);
    return 1;
}

static void reset(char *out) { o = out; has_vic = 0; unp = 0; uReg = -1; UReg = -1; }

void disasm_arm(uint32_t w, uint32_t pc, char *out)
{
    reset(out);
    if (!arm_body(w, pc)) { o = out; s(".word\t"); hx8(w); }
    else tail();
    *o = 0;
}

/* -------------------------------------------------------------- Thumb */

#define LO(hw, b) RN[((hw) >> (b)) & 7]

static void tlist(int l, const char *extra)
{
    int first = 1;
    c('{');
    for (int i = 0; i < 8; i++) if (l & (1 << i)) { if (!first) s(", "); R(i); first = 0; }
    if (extra) { if (!first) s(", "); s(extra); }
    c('}');
}

static int thumb_body(unsigned hw, uint32_t pc)
{
    const char *d = LO(hw, 0), *sr = LO(hw, 3), *r8 = LO(hw, 8);
    int top5 = hw >> 11, imm5 = (hw >> 6) & 31, i8 = hw & 0xff;
    switch (top5) {
    case 0:
        if (imm5) { s("lsls\t"); s(d); s(", "); s(sr); s(", #"); dec(imm5); }
        else { s("movs\t"); s(d); s(", "); s(sr); }
        return 1;
    case 1: case 2:
        s(top5 == 1 ? "lsrs\t" : "asrs\t"); s(d); s(", "); s(sr); s(", #"); dec(imm5 ? imm5 : 32);
        return 1;
    case 3:
        s(hw & 0x200 ? "subs\t" : "adds\t"); s(d); s(", "); s(sr); s(", ");
        if (hw & 0x400) { c('#'); dec((hw >> 6) & 7); } else s(LO(hw, 6));
        return 1;
    case 4: case 5: case 6: case 7: {
        static const char m[4][6] = { "movs\t", "cmp\t", "adds\t", "subs\t" };
        setvic(i8); s(m[top5 - 4]); s(r8); s(", #"); dec(i8);
        return 1;
    }
    case 8:
        if (!(hw & 0x400)) {
            static const char m[16][5] = { "ands", "eors", "lsls", "lsrs", "asrs", "adcs", "sbcs", "rors",
                                           "tst", "negs", "cmp", "cmn", "orrs", "muls", "bics", "mvns" };
            s(m[(hw >> 6) & 15]); c('\t'); s(d); s(", "); s(sr);
            return 1;
        } else {
            int op = (hw >> 8) & 3;
            const char *hs = RN[(hw >> 3) & 15], *hd = RN[(hw & 7) | ((hw >> 4) & 8)];
            if (op == 3) {
                if (hw & 0x80) { if (hw & 7) return 0; s("blx\t"); s(hs); return 1; }
                s("bx\t"); s(hs); return 1;
            }
            if (hw == 0x46c0) { s("nop\t\t\t@ (mov r8, r8)"); return 1; }
            static const char m[3][4] = { "add", "cmp", "mov" };
            s(m[op]); c('\t'); s(hd); s(", "); s(hs);
            return 1;
        }
    case 9: {
        uint32_t a = ((pc + 4) & ~3u) + i8 * 4;
        s("ldr\t"); s(r8); s(", [pc, #"); dec(i8 * 4); s("]\t@ ("); hx(a); c(')');
        return 1;
    }
    case 10: case 11: {
        static const char m[8][6] = { "str", "strh", "strb", "ldrsb", "ldr", "ldrh", "ldrb", "ldrsh" };
        s(m[(hw >> 9) & 7]); c('\t'); s(d); s(", ["); s(sr); s(", "); s(LO(hw, 6)); c(']');
        return 1;
    }
    case 12: case 13: case 14: case 15: case 16: case 17: {
        int sc = top5 <= 13 ? 4 : top5 <= 15 ? 1 : 2;
        static const char m[6][5] = { "str", "ldr", "strb", "ldrb", "strh", "ldrh" };
        setvic(imm5 * sc);
        s(m[top5 - 12]); c('\t'); s(d); s(", ["); s(sr); s(", #"); dec(imm5 * sc); c(']');
        return 1;
    }
    case 18: case 19:
        setvic(i8 * 4); s(top5 & 1 ? "ldr\t" : "str\t"); s(r8); s(", [sp, #"); dec(i8 * 4); c(']');
        return 1;
    case 20:
        s("add\t"); s(r8); s(", pc, #"); dec(i8 * 4); s("\t@ (adr "); s(r8); s(", ");
        hx(((pc + 4) & ~3u) + i8 * 4); c(')');
        return 1;
    case 21:
        setvic(i8 * 4); s("add\t"); s(r8); s(", sp, #"); dec(i8 * 4);
        return 1;
    case 22: case 23: {
        int k = (hw >> 8) & 15;
        if (k == 0) { setvic((hw & 0x7f) * 4); s(hw & 0x80 ? "sub" : "add"); s("\tsp, #"); dec((hw & 0x7f) * 4); return 1; }
        if (k == 4 || k == 5) { s("push\t"); tlist(i8, k & 1 ? "lr" : 0); return 1; }
        if (k == 12 || k == 13) { s("pop\t"); tlist(i8, k & 1 ? "pc" : 0); return 1; }
        if (k == 14) { s("bkpt\t0x"); hexn(i8, 4); return 1; }
        return 0;
    }
    case 24: s("stmia\t"); s(r8); s("!, "); tlist(i8, 0); return 1;
    case 25:
        s("ldmia\t"); s(r8); if (!(i8 & (1 << ((hw >> 8) & 7)))) c('!'); s(", "); tlist(i8, 0);
        return 1;
    case 26: case 27: {
        int cond = (hw >> 8) & 15;
        if (cond == 15) { setvic(i8); s("svc\t"); dec(i8); return 1; }
        if (cond == 14) { setvic(i8); s("udf\t#"); dec(i8); return 1; }
        c('b'); s(CC[cond]); s(".n\t"); hx(pc + 4 + (((int32_t)((hw & 0xff) << 24)) >> 23));
        return 1;
    }
    case 28:
        s("b.n\t"); hx(pc + 4 + (((int32_t)((hw & 0x7ff) << 21)) >> 20));
        return 1;
    }
    return 0;
}

/* returns the size (2 or 4); next = the following halfword (or -1) */
int disasm_thumb(unsigned hw, int next, uint32_t pc, char *out)
{
    hw &= 0xffff;
    reset(out);
    if ((hw & 0xf800) == 0xf000 && next >= 0) {
        unsigned n = next & 0xffff;
        int32_t off = (((int32_t)((hw & 0x7ff) << 21)) >> 9) + ((n & 0x7ff) << 1);
        if ((n & 0xf800) == 0xf800) { s("bl\t"); hx(pc + 4 + off); *o = 0; return 4; }
        if ((n & 0xf801) == 0xe800) { s("blx\t"); hx((pc + 4 + off) & ~3u); *o = 0; return 4; }
    }
    if (hw >= 0xe800 || !thumb_body(hw, pc)) { o = out; s(".short\t0x"); hexn(hw, 4); }
    else tail();
    *o = 0;
    return 2;
}
