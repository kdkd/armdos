/*
 * asm.c - DEBUG's line-by-line ARM assembler (the A command).
 *
 * One instruction per line in GNU syntax, ARMv5TE, ARM state: everything the
 * disassembler (disasm.c) prints assembles back to the same word.  Accepts
 * both unified ("ldrbeq", "movseq", "ldmiaeq") and divided ("ldreqb",
 * "moveqs", "ldmeqia") suffix orders, case-insensitively.
 *
 *   data processing   and eor sub rsb add adc sbc rsc orr bic (3 or 2 operands),
 *                     mov mvn, tst teq cmp cmn (+ "p" variants), with #imm
 *                     (any rotation, or "#imm8, rot"), rm, rm shift #n, rm shift rs,
 *                     rm rrx; lsl lsr asr ror rrx as mov aliases; nop; adr
 *   multiplies        mul mla umull umlal smull smlal, smulxy smlaxy smulwy
 *                     smlawy smlalxy, qadd qsub qdadd qdsub, clz
 *   loads/stores      ldr str ldrb strb ldrt strt ldrbt strbt (all mode 2 forms),
 *                     ldrh strh ldrsb ldrsh ldrd strd (mode 3), pc-relative
 *                     "ldr r0, <address>", pld, swp swpb
 *   multiple          ldm/stm ia ib da db fd fa ed ea, "!", "^", push pop
 *   branches          b bl (+cond) bx blx (register and immediate) bxj
 *   system            svc/swi, bkpt, udf, mrs, msr (#imm or register, field masks),
 *                     cdp mcr mrc mcrr mrrc ldc stc (+2 forms)
 *   VFPv2 (VFP9-S)    vadd vsub vmul vnmul vdiv vmla vmls vnmla vnmls vabs vneg vsqrt
 *                     .f32/.f64, vmov (.f32/.f64, s<->r, r,r<->d, s,s<->r,r, .32 d[x]),
 *                     vcmp(e) (reg or #0.0), vcvt(r) (.f64.f32 .f32.f64 .f32/.f64.s32/.u32
 *                     .s32/.u32.f32/.f64), vldr vstr ([rn, #off] or an address),
 *                     vldm/vstm ia db, vpush vpop, fldmiax fldmdbx fstmiax fstmdbx,
 *                     vmrs vmsr (fpsid fpscr fpexc fpinst fpinst2, APSR_nzcv)
 *   data              DB (hex bytes and 'strings'), DW, DD (hex, as DEBUG's),
 *                     .byte .short .word (GNU numbers)
 *
 * Numbers after "#" follow GNU as: decimal, 0x hex, 0b binary, 'c'; a trailing
 * H also means hex (41H).  Branch targets and pc-relative addresses: "0x..."
 * is an absolute address, anything else is a DEBUG address (asm_address()).
 */
#include <stdint.h>
#include <string.h>

#include "debug.h"

static const char *p, *errp;
static JMPBUF asm_jb;
static uint32_t pcaddr;

__attribute__((noreturn)) static void fail(void) { errp = p; LONGJMP(asm_jb, 1); }
__attribute__((noreturn)) static void fail_at(const char *q) { p = q; fail(); }

static int up(int ch) { return ch >= 'a' && ch <= 'z' ? ch - 32 : ch; }
static void skipws(void) { while (*p == ' ' || *p == '\t') p++; }
static int at_end(void) { skipws(); return *p == 0 || *p == '\r' || *p == '@' || *p == ';'; }
static int accept(int ch) { skipws(); if (*p == ch) { p++; return 1; } return 0; }
static void expect(int ch) { if (!accept(ch)) fail(); }
static void comma(void) { expect(','); }
static int isalnum_(int ch) { ch = up(ch); return (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_'; }

/* match a keyword (case-insensitive) that is not followed by an identifier char */
static int kw(const char *k)
{
    skipws();
    const char *q = p;
    while (*k) { if (up(*q) != up(*k)) return 0; q++; k++; }
    if (isalnum_(*q)) return 0;
    p = q;
    return 1;
}

/* ------------------------------------------------------------- numbers */

static int hexval(int ch)
{
    ch = up(ch);
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

/* GNU-style number with optional sign; *neg set if a '-' was given */
static int try_number(uint32_t *v, int *neg)
{
    skipws();
    const char *q = p;
    int n = 0;
    if (*q == '-') { n = 1; q++; } else if (*q == '+') q++;
    while (*q == ' ') q++;
    uint32_t x = 0;
    if (*q == '\'') {
        if (!q[1] || q[2] != '\'') return 0;
        x = (uint8_t)q[1];
        q += 3;
    } else if (q[0] == '0' && up(q[1]) == 'X' && hexval(q[2]) >= 0) {
        q += 2;
        while (hexval(*q) >= 0) x = x * 16 + hexval(*q++);
    } else if (q[0] == '0' && up(q[1]) == 'B' && (q[2] == '0' || q[2] == '1')) {
        q += 2;
        while (*q == '0' || *q == '1') x = x * 2 + (*q++ - '0');
    } else if (*q >= '0' && *q <= '9') {
        const char *r = q;
        while (hexval(*r) >= 0) r++;
        if (up(*r) == 'H' && !isalnum_(r[1])) {
            while (q < r) x = x * 16 + hexval(*q++);
            q++;
        } else {
            while (*q >= '0' && *q <= '9') x = x * 10 + (*q++ - '0');
        }
    } else return 0;
    if (isalnum_(*q)) return 0;
    p = q;
    *v = n ? -x : x;
    if (neg) *neg = n;
    return 1;
}

static uint32_t number(void)
{
    uint32_t v;
    if (!try_number(&v, 0)) fail();
    return v;
}

/* "#n" (the # is optional where nothing else could follow) */
static uint32_t imm(int need_hash)
{
    skipws();
    if (*p == '#') p++;
    else if (need_hash) fail();
    return number();
}

/* SVC/BKPT/UDF operand: "#n" is GNU; a bare number is hex, as DEBUG's INT 21 */
static uint32_t svcnum(void)
{
    skipws();
    if (*p == '#') { p++; return number(); }
    if (p[0] == '0' && up(p[1]) == 'X') return number();
    const char *q = p;
    uint32_t v = 0;
    int d = 0;
    while (hexval(*p) >= 0) { v = v * 16 + hexval(*p++); d++; }
    if (up(*p) == 'H' && !isalnum_(p[1])) p++;
    if (!d || d > 8 || isalnum_(*p)) fail_at(q);
    return v;
}

/* an address: 0x... = absolute, "." = here, else a DEBUG address */
static uint32_t target(void)
{
    skipws();
    uint32_t v;
    if (p[0] == '0' && up(p[1]) == 'X') { if (!try_number(&v, 0)) fail(); return v; }
    if (*p == '.' && !isalnum_(p[1])) {
        p++;
        v = pcaddr;
        skipws();
        if (*p == '+' || *p == '-') { uint32_t d; int neg = 0; if (!try_number(&d, &neg)) fail(); v += d; }
        return v;
    }
    if (*p == '#') fail();
    if (!asm_address(&p, &v)) fail();
    return v;
}

/* ----------------------------------------------------------- registers */

static int try_reg(void)
{
    static const char names[6][3] = { "SL", "FP", "IP", "SP", "LR", "PC" };
    skipws();
    const char *q = p;
    int r = -1;
    if (up(q[0]) == 'R' && q[1] >= '0' && q[1] <= '9') {
        r = q[1] - '0'; q += 2;
        if (*q >= '0' && *q <= '9') { r = r * 10 + (*q - '0'); q++; }
        if (r > 15) return -1;
    } else {
        for (int i = 0; i < 6; i++)
            if (up(q[0]) == names[i][0] && up(q[1]) == names[i][1]) { r = 10 + i; q += 2; break; }
    }
    if (r < 0 || isalnum_(*q)) return -1;
    p = q;
    return r;
}

static int reg(void) { int r = try_reg(); if (r < 0) fail(); return r; }

/* coprocessor register "cr7" / "c7" */
static int creg(void)
{
    skipws();
    const char *q = p;
    if (up(*q) != 'C') fail();
    q++;
    if (up(*q) == 'R') q++;
    if (*q < '0' || *q > '9') fail();
    int n = *q++ - '0';
    if (*q >= '0' && *q <= '9') n = n * 10 + (*q++ - '0');
    if (n > 15 || isalnum_(*q)) fail();
    p = q;
    return n;
}

/* coprocessor number "p15" / "15" */
static int cpnum(void)
{
    skipws();
    if (up(*p) == 'P') p++;
    uint32_t v = number();
    if (v > 15) fail();
    return v;
}

/* --------------------------------------------------------- mnemonics */

enum {
    K_DP3, K_MOV, K_CMP, K_SHIFT, K_RRX, K_NOP, K_ADR,
    K_MUL, K_MLA, K_MULL, K_SMULXY, K_SMLAXY, K_SMULWY, K_SMLAWY, K_SMLALXY, K_QADD, K_CLZ,
    K_LDR, K_STR, K_LDM, K_STM, K_PUSH, K_POP, K_SWP, K_PLD,
    K_B, K_BL, K_BX, K_BLX, K_BXJ, K_SVC, K_BKPT, K_UDF, K_MRS, K_MSR,
    K_CDP, K_MCR, K_MRC, K_MCRR, K_MRRC, K_LDC, K_STC,
    K_DB, K_DW, K_DD, K_BYTE, K_SHORT, K_WORD
};

/* suffix tokens a mnemonic accepts */
#define S_COND  0x001
#define S_S     0x002
#define S_P     0x004           /* tst/teq/cmp/cmn with rd = pc */
#define S_B     0x008           /* ldr/str/swp byte */
#define S_T     0x010           /* ldr/str translation */
#define S_HX    0x020           /* h sb sh d (ldr/str mode 3) */
#define S_MODE  0x040           /* ldm/stm addressing mode */
#define S_L     0x080           /* ldc/stc long */
#define S_2     0x100           /* cdp2 etc. (unconditional) */

struct mn { char name[8]; uint8_t kind, arg; uint16_t sfx; };

static const struct mn mns[] = {
    { "and", K_DP3, 0, S_COND | S_S }, { "eor", K_DP3, 1, S_COND | S_S },
    { "sub", K_DP3, 2, S_COND | S_S }, { "rsb", K_DP3, 3, S_COND | S_S },
    { "add", K_DP3, 4, S_COND | S_S }, { "adc", K_DP3, 5, S_COND | S_S },
    { "sbc", K_DP3, 6, S_COND | S_S }, { "rsc", K_DP3, 7, S_COND | S_S },
    { "tst", K_CMP, 8, S_COND | S_S | S_P }, { "teq", K_CMP, 9, S_COND | S_S | S_P },
    { "cmp", K_CMP, 10, S_COND | S_S | S_P }, { "cmn", K_CMP, 11, S_COND | S_S | S_P },
    { "orr", K_DP3, 12, S_COND | S_S }, { "mov", K_MOV, 13, S_COND | S_S },
    { "bic", K_DP3, 14, S_COND | S_S }, { "mvn", K_MOV, 15, S_COND | S_S },
    { "lsl", K_SHIFT, 0, S_COND | S_S }, { "lsr", K_SHIFT, 1, S_COND | S_S },
    { "asr", K_SHIFT, 2, S_COND | S_S }, { "ror", K_SHIFT, 3, S_COND | S_S },
    { "rrx", K_RRX, 0, S_COND | S_S }, { "nop", K_NOP, 0, S_COND },
    { "adr", K_ADR, 0, S_COND },
    { "mul", K_MUL, 0, S_COND | S_S }, { "mla", K_MLA, 0, S_COND | S_S },
    { "umull", K_MULL, 4, S_COND | S_S }, { "umlal", K_MULL, 5, S_COND | S_S },
    { "smull", K_MULL, 6, S_COND | S_S }, { "smlal", K_MULL, 7, S_COND | S_S },
    { "smulbb", K_SMULXY, 0, S_COND }, { "smultb", K_SMULXY, 1, S_COND },
    { "smulbt", K_SMULXY, 2, S_COND }, { "smultt", K_SMULXY, 3, S_COND },
    { "smlabb", K_SMLAXY, 0, S_COND }, { "smlatb", K_SMLAXY, 1, S_COND },
    { "smlabt", K_SMLAXY, 2, S_COND }, { "smlatt", K_SMLAXY, 3, S_COND },
    { "smulwb", K_SMULWY, 0, S_COND }, { "smulwt", K_SMULWY, 2, S_COND },
    { "smlawb", K_SMLAWY, 0, S_COND }, { "smlawt", K_SMLAWY, 2, S_COND },
    { "smlalbb", K_SMLALXY, 0, S_COND }, { "smlaltb", K_SMLALXY, 1, S_COND },
    { "smlalbt", K_SMLALXY, 2, S_COND }, { "smlaltt", K_SMLALXY, 3, S_COND },
    { "qadd", K_QADD, 0, S_COND }, { "qsub", K_QADD, 1, S_COND },
    { "qdadd", K_QADD, 2, S_COND }, { "qdsub", K_QADD, 3, S_COND },
    { "clz", K_CLZ, 0, S_COND },
    { "ldr", K_LDR, 1, S_COND | S_B | S_T | S_HX }, { "str", K_STR, 0, S_COND | S_B | S_T | S_HX },
    { "ldm", K_LDM, 1, S_COND | S_MODE }, { "stm", K_STM, 0, S_COND | S_MODE },
    { "push", K_PUSH, 0, S_COND }, { "pop", K_POP, 1, S_COND },
    { "swp", K_SWP, 0, S_COND | S_B }, { "pld", K_PLD, 0, 0 },
    { "b", K_B, 0, S_COND }, { "bl", K_BL, 0, S_COND },
    { "bx", K_BX, 0, S_COND }, { "blx", K_BLX, 0, S_COND }, { "bxj", K_BXJ, 0, S_COND },
    { "svc", K_SVC, 0, S_COND }, { "swi", K_SVC, 0, S_COND },
    { "bkpt", K_BKPT, 0, 0 }, { "udf", K_UDF, 0, 0 },
    { "mrs", K_MRS, 0, S_COND }, { "msr", K_MSR, 0, S_COND },
    { "cdp", K_CDP, 0, S_COND | S_2 }, { "mcr", K_MCR, 0, S_COND | S_2 }, { "mrc", K_MRC, 0, S_COND | S_2 },
    { "mcrr", K_MCRR, 0, S_COND }, { "mrrc", K_MRRC, 0, S_COND },
    { "ldc", K_LDC, 1, S_COND | S_L | S_2 }, { "stc", K_STC, 0, S_COND | S_L | S_2 },
    { "db", K_DB, 0, 0 }, { "dw", K_DW, 0, 0 }, { "dd", K_DD, 0, 0 },
    { ".byte", K_BYTE, 0, 0 }, { ".short", K_SHORT, 0, 0 }, { ".hword", K_SHORT, 0, 0 },
    { ".word", K_WORD, 0, 0 }, { ".long", K_WORD, 0, 0 },
};

static const char conds[17][3] = { "eq", "ne", "cs", "cc", "mi", "pl", "vs", "vc",
                                   "hi", "ls", "ge", "lt", "gt", "le", "al", "hs", "lo" };

/* the parsed suffixes */
static int cond, sflag, pflag, bflag, tflag, hx, mode, lflag, two;
/* hx: 0 none, 1 h, 2 sb, 3 sh, 4 d.  mode: -1 none, else "ia ib da db fd fa ed ea" index */

static const char modes[8][3] = { "ia", "ib", "da", "db", "fd", "fa", "ed", "ea" };

static int lc2(const char *q, const char *t) { return (q[0] | 32) == t[0] && (q[1] | 32) == t[1]; }

/* parse the suffix letters q[0..n) with the allowed set; backtracking */
static int suffixes(const char *q, int n, unsigned allow)
{
    if (n == 0) return 1;
    /* "cmppls" is cmp+p+ls, not cmp+pl+s: try p first */
    if ((allow & S_P) && !pflag && (q[0] | 32) == 'p') { pflag = 1; if (suffixes(q + 1, n - 1, allow)) return 1; pflag = 0; }
    if ((allow & S_COND) && cond < 0 && n >= 2) {
        for (int i = 0; i < 17; i++) if (lc2(q, conds[i])) {
            cond = i == 15 ? 2 : i == 16 ? 3 : i;
            if (suffixes(q + 2, n - 2, allow)) return 1;
            cond = -1;
        }
    }
    if ((allow & S_MODE) && mode < 0 && n >= 2) {
        for (int i = 0; i < 8; i++) if (lc2(q, modes[i])) {
            mode = i;
            if (suffixes(q + 2, n - 2, allow)) return 1;
            mode = -1;
        }
    }
    if ((allow & S_HX) && !hx && !bflag) {
        static const char h[4][3] = { "h", "sb", "sh", "d" };
        for (int i = 0; i < 4; i++) {
            int l = strlen(h[i]);
            if (n >= l && (q[0] | 32) == h[i][0] && (l == 1 || (q[1] | 32) == h[i][1])) {
                hx = i + 1;
                if (suffixes(q + l, n - l, allow)) return 1;
                hx = 0;
            }
        }
    }
    int ch = q[0] | 32;
    if ((allow & S_S) && !sflag && ch == 's') { sflag = 1; if (suffixes(q + 1, n - 1, allow)) return 1; sflag = 0; }
    if ((allow & S_B) && !bflag && !hx && ch == 'b') { bflag = 1; if (suffixes(q + 1, n - 1, allow)) return 1; bflag = 0; }
    if ((allow & S_T) && !tflag && ch == 't') { tflag = 1; if (suffixes(q + 1, n - 1, allow)) return 1; tflag = 0; }
    if ((allow & S_L) && !lflag && ch == 'l') { lflag = 1; if (suffixes(q + 1, n - 1, allow)) return 1; lflag = 0; }
    if ((allow & S_2) && !two && ch == '2') { two = 1; if (suffixes(q + 1, n - 1, allow)) return 1; two = 0; }
    return 0;
}

static const struct mn *mnemonic(void)
{
    skipws();
    const char *q = p;
    int n = 0;
    while (isalnum_(q[n]) || q[n] == '.') n++;
    if (!n) fail();
    const struct mn *best = 0;
    int bestlen = 0;
    for (unsigned i = 0; i < sizeof mns / sizeof mns[0]; i++) {
        const struct mn *m = &mns[i];
        int l = strlen(m->name);
        if (l > n || l <= bestlen) continue;
        int k = 0;
        while (k < l && (q[k] | 32) == m->name[k]) k++;
        if (k < l) continue;
        cond = -1; sflag = pflag = bflag = tflag = hx = lflag = two = 0; mode = -1;
        if (!suffixes(q + l, n - l, m->sfx)) continue;
        /* combinations that make no sense */
        if (tflag && hx) continue;
        if (two && cond >= 0) continue;
        best = m;
        bestlen = l;
    }
    if (!best) fail();
    /* re-parse the winner's suffixes (the loop may have tried others after it) */
    cond = -1; sflag = pflag = bflag = tflag = hx = lflag = two = 0; mode = -1;
    suffixes(q + bestlen, n - bestlen, best->sfx);
    p = q + n;
    return best;
}

/* ------------------------------------------------------------ operands */

/* ARM immediate: returns the 12-bit field, or -1 */
static int encimm(uint32_t v)
{
    for (int r = 0; r < 32; r += 2) {
        uint32_t x = (v << r) | (r ? v >> (32 - r) : 0);
        if (x <= 0xff) return ((r / 2) << 8) | x;
    }
    return -1;
}

/* shift after "rm," : returns the bits 4-11 */
static uint32_t shiftspec(void)
{
    static const char sh[4][4] = { "LSL", "LSR", "ASR", "ROR" };
    skipws();
    const char *q = p;
    if (kw("RRX")) return 3 << 5;
    int t = -1;
    for (int i = 0; i < 4; i++) if (kw(sh[i])) { t = i; break; }
    if (t < 0 && kw("ASL")) t = 0;
    if (t < 0) fail_at(q);
    int r = try_reg();
    if (r >= 0) return (r << 8) | (t << 5) | 0x10;
    skipws();
    q = p;
    uint32_t n = imm(1);
    if (t == 0 && n > 31) fail_at(q);
    if (t == 3 && (n < 1 || n > 31)) fail_at(q);
    if ((t == 1 || t == 2) && (n < 1 || n > 32)) fail_at(q);
    return ((n & 31) << 7) | (t << 5);
}

/* flexible second operand; returns the bits (I bit included) or sets *immval */
static int op2_isimm;
static uint32_t op2_imm;
static uint32_t op2(void)
{
    skipws();
    op2_isimm = 0;
    if (*p == '#') {
        const char *q = p;
        uint32_t v = imm(1);
        if (accept(',')) {
            /* explicit "#imm8, rot" */
            const char *q2 = p;
            uint32_t rot = number();
            if (v > 255 || rot > 30 || (rot & 1)) fail_at(q2);
            return 0x02000000 | ((rot / 2) << 8) | v;
        }
        op2_isimm = 1;
        op2_imm = v;
        (void)q;
        return 0;
    }
    int rm = reg();
    if (accept(',')) return rm | shiftspec();
    return rm;
}

static uint32_t encode_dp(int op, int s, int rn, int rd, uint32_t o2, const char *immpos)
{
    if (op2_isimm) {
        int e = encimm(op2_imm);
        if (e < 0) {
            /* the complementary instruction, as GNU as does */
            static const int8_t alt[16] = { 14, -1, 4, -1, 2, 6, 5, -1, -1, -1, 11, 10, -1, 15, 0, 13 };
            int a = alt[op];
            uint32_t v2 = (op == 2 || op == 4 || op == 10 || op == 11) ? -op2_imm : ~op2_imm;
            if (a >= 0 && (e = encimm(v2)) >= 0) op = a;
            else fail_at(immpos);
        }
        o2 = 0x02000000 | e;
    }
    return ((uint32_t)cond << 28) | ((uint32_t)op << 21) | (s ? 0x00100000 : 0) | (rn << 16) | (rd << 12) | o2;
}

static uint32_t reglist(void)
{
    uint32_t l = 0;
    expect('{');
    if (accept('}')) fail();
    do {
        int a = reg();
        int b = a;
        if (accept('-')) { b = reg(); if (b < a) fail(); }
        for (int i = a; i <= b; i++) l |= 1u << i;
    } while (accept(','));
    expect('}');
    return l;
}

static int regshift_ok;             /* pld prints (and so takes) register shifts */

/* mode 2 / mode 3 address.  Returns the P U W bits + offset fields + rn. */
static uint32_t address_mode(int m3, int allow_post, int tform)
{
    skipws();
    uint32_t w = 0;
    if (*p != '[') {
        /* pc-relative: a label/address */
        if (tform) fail();
        skipws();
        const char *q = p;
        uint32_t a = target();
        int32_t d = (int32_t)(a - (pcaddr + 8));
        uint32_t u = d >= 0 ? 0x00800000 : 0, off = d >= 0 ? d : -d;
        if (off > (m3 ? 255u : 4095u)) fail_at(q);
        w = 0x01000000 | u | (15 << 16);
        if (m3) w |= 0x00400000 | ((off & 0xf0) << 4) | (off & 15);
        else w |= off;
        return w;
    }
    p++;
    int rn = reg();
    w |= rn << 16;
    if (accept(']')) {
        /* [rn] or post-indexed */
        if (accept(',')) {
            if (!allow_post) fail();
            skipws();
            if (*p == '#') {
                int neg = 0; uint32_t v;
                p++;
                const char *q = p;
                if (!try_number(&v, &neg)) fail();
                uint32_t off = neg ? -v : v;
                if (off > (m3 ? 255u : 4095u)) fail_at(q);
                if (!neg) w |= 0x00800000;
                if (m3) w |= 0x00400000 | ((off & 0xf0) << 4) | (off & 15);
                else w |= off;
            } else {
                int neg = accept('-');
                if (!neg) accept('+');
                int rm = reg();
                if (!neg) w |= 0x00800000;
                if (m3) w |= rm;
                else {
                    w |= 0x02000000 | rm;
                    if (accept(',')) { uint32_t sh = shiftspec(); if ((sh & 0x10) && !regshift_ok) fail(); w |= sh; }
                }
            }
            if (tform) w |= 0x00200000;
            return w;
        }
        /* [rn] = [rn, #0] pre-indexed (tform: post-indexed #0) */
        if (tform) return w | 0x00800000 | 0x00200000 | (m3 ? 0x00400000 : 0);
        return w | 0x01000000 | 0x00800000 | (m3 ? 0x00400000 : 0);
    }
    if (tform) fail();
    comma();
    skipws();
    w |= 0x01000000;
    if (*p == '#') {
        int neg = 0; uint32_t v;
        p++;
        const char *q = p;
        if (!try_number(&v, &neg)) fail();
        uint32_t off = neg ? -v : v;
        if (off > (m3 ? 255u : 4095u)) fail_at(q);
        if (!neg) w |= 0x00800000;
        if (m3) w |= 0x00400000 | ((off & 0xf0) << 4) | (off & 15);
        else w |= off;
    } else {
        int neg = accept('-');
        if (!neg) accept('+');
        int rm = reg();
        if (!neg) w |= 0x00800000;
        if (m3) w |= rm;
        else {
            w |= 0x02000000 | rm;
            if (accept(',')) { uint32_t sh = shiftspec(); if ((sh & 0x10) && !regshift_ok) fail(); w |= sh; }
        }
    }
    expect(']');
    if (accept('!')) w |= 0x00200000;
    return w;
}

/* coprocessor address */
static uint32_t address_cp(void)
{
    expect('[');
    int rn = reg();
    uint32_t w = rn << 16;
    if (accept(']')) {
        if (accept(',')) {
            skipws();
            if (*p == '{') {
                p++;
                int neg = 0; uint32_t v;
                if (!try_number(&v, &neg)) fail();
                if (neg && v) fail();
                if (v > 255) fail();
                expect('}');
                return w | (neg ? 0 : 0x00800000) | v;      /* unindexed */
            }
            int neg = 0; uint32_t v;
            expect('#');
            const char *q = p;
            if (!try_number(&v, &neg)) fail();
            uint32_t off = neg ? -v : v;
            if ((off & 3) || off > 1020) fail_at(q);
            return w | 0x00200000 | (neg ? 0 : 0x00800000) | (off >> 2);
        }
        return w | 0x01000000 | 0x00800000;
    }
    comma();
    int neg = 0; uint32_t v;
    expect('#');
    const char *q = p;
    if (!try_number(&v, &neg)) fail();
    uint32_t off = neg ? -v : v;
    if ((off & 3) || off > 1020) fail_at(q);
    expect(']');
    w |= 0x01000000 | (neg ? 0 : 0x00800000) | (off >> 2);
    if (accept('!')) w |= 0x00200000;
    return w;
}

/* ----------------------------------------------------------------- VFP */

/* VFPv2 (the VFP9-S, coprocessors 10 and 11) in the UAL text the disassembler
 * prints: the condition goes before the type suffix ("vaddne.f32", "vcvtrne.s32.f64",
 * "vldmiacc", "fldmiaxeq").  Registers s0-s31 and d0-d15 (VFPv2 has no d16-d31). */

enum {
    V_DP, V_UN, V_CMP, V_CMPE, V_CVT, V_CVTR, V_LDR, V_STR, V_LDMIA, V_LDMDB, V_STMIA, V_STMDB,
    V_PUSH, V_POP, V_FLDMIAX, V_FLDMDBX, V_FSTMIAX, V_FSTMDBX, V_MOV, V_MRS, V_MSR
};
struct vmn { char name[8]; uint8_t kind; uint32_t op; };
static const struct vmn vmns[] = {
    { "vmla", V_DP, 0x0e000a00 }, { "vmls", V_DP, 0x0e000a40 }, { "vnmls", V_DP, 0x0e100a00 },
    { "vnmla", V_DP, 0x0e100a40 }, { "vmul", V_DP, 0x0e200a00 }, { "vnmul", V_DP, 0x0e200a40 },
    { "vadd", V_DP, 0x0e300a00 }, { "vsub", V_DP, 0x0e300a40 }, { "vdiv", V_DP, 0x0e800a00 },
    { "vabs", V_UN, 0x0eb00ac0 }, { "vneg", V_UN, 0x0eb10a40 }, { "vsqrt", V_UN, 0x0eb10ac0 },
    { "vcmp", V_CMP, 0 }, { "vcmpe", V_CMPE, 0 }, { "vcvt", V_CVT, 0 }, { "vcvtr", V_CVTR, 0 },
    { "vldr", V_LDR, 0x0d100a00 }, { "vstr", V_STR, 0x0d000a00 },
    { "vldm", V_LDMIA, 0 }, { "vldmia", V_LDMIA, 0 }, { "vldmdb", V_LDMDB, 0 },
    { "vstm", V_STMIA, 0 }, { "vstmia", V_STMIA, 0 }, { "vstmdb", V_STMDB, 0 },
    { "vpush", V_PUSH, 0 }, { "vpop", V_POP, 0 },
    { "fldmiax", V_FLDMIAX, 0 }, { "fldmdbx", V_FLDMDBX, 0 }, { "fstmiax", V_FSTMIAX, 0 },
    { "fstmdbx", V_FSTMDBX, 0 },
    { "vmov", V_MOV, 0 }, { "vmrs", V_MRS, 0 }, { "vmsr", V_MSR, 0 },
};

/* "s5" -> 5, "d3" -> 3 (kind 's' / 'd'); -1 if not that kind of register */
static int try_vreg(int kind)
{
    skipws();
    const char *q = p;
    if ((*q | 32) != kind || q[1] < '0' || q[1] > '9') return -1;
    q++;
    int n = *q++ - '0';
    if (*q >= '0' && *q <= '9') n = n * 10 + (*q++ - '0');
    if (isalnum_(*q) || n > (kind == 's' ? 31 : 15)) return -1;
    p = q;
    return n;
}
static int vreg(int kind) { int r = try_vreg(kind); if (r < 0) fail(); return r; }

/* field bits of a single / double register as Vd (1), Vn (2) or Vm (0) */
static uint32_t vsd(int sg, int r, int pos)
{
    if (!sg) return pos == 1 ? (uint32_t)r << 12 : pos == 2 ? (uint32_t)r << 16 : (uint32_t)r;
    uint32_t hi = r >> 1, lo = r & 1;
    return pos == 1 ? hi << 12 | lo << 22 : pos == 2 ? hi << 16 | lo << 7 : hi | lo << 5;
}

/* "{s3-s7}" / "{d0}" / "{s1, s2, s3}": consecutive registers of one kind */
static void vlist(int *kind, int *first, int *count)
{
    expect('{');
    skipws();
    int k = (*p | 32) == 'd' ? 'd' : 's';
    if (*kind && *kind != k) fail();
    int a = vreg(k), last = a;
    if (accept('-')) { const char *q = p; last = vreg(k); if (last < a) fail_at(q); }
    else while (accept(',')) { const char *q = p; int r = vreg(k); if (r != last + 1) fail_at(q); last = r; }
    expect('}');
    *kind = k; *first = a; *count = last - a + 1;
}

/* the type suffixes after the mnemonic: ".f32" -> "F32", ".s32.f64" -> "S32.F64" */
static char vtype[12];
static int vt(const char *t) { return !strcmp(vtype, t); }

/* is the token at p a VFP mnemonic?  Then assemble it into *w and return 1. */
static int vfp_asm(uint32_t *w)
{
    skipws();
    const char *q = p;
    int n = 0;
    while (isalnum_(q[n])) n++;
    const struct vmn *m = 0;
    int cnd = -1;
    for (unsigned i = 0; i < sizeof vmns / sizeof vmns[0]; i++) {
        int l = strlen(vmns[i].name), k = 0;
        if (l > n) continue;
        while (k < l && (q[k] | 32) == vmns[i].name[k]) k++;
        if (k < l) continue;
        int c2 = -1;
        if (n == l) c2 = 14;
        else if (n == l + 2) for (int j = 0; j < 17; j++) if (lc2(q + l, conds[j])) c2 = j == 15 ? 2 : j == 16 ? 3 : j;
        if (c2 >= 0) { m = &vmns[i]; cnd = c2; }
    }
    if (!m) return 0;
    p = q + n;
    int tl = 0;
    while (*p == '.' && tl < 10) {
        if (tl) vtype[tl++] = '.';
        p++;
        while (isalnum_(*p) && tl < 10) vtype[tl++] = up(*p++);
    }
    vtype[tl] = 0;
    if (isalnum_(*p) || *p == '.') fail();
    const char *ops = p;
    uint32_t c = (uint32_t)cnd << 28, x = 0;
    int sg = vt("F32"), dbl = vt("F64");
    switch (m->kind) {
    case V_DP: case V_UN: {
        if (!sg && !dbl) fail_at(q);
        int k = sg ? 's' : 'd';
        int d = vreg(k); comma(); int a = vreg(k);
        if (m->kind == V_DP) {
            int b;
            if (accept(',')) b = vreg(k); else { b = a; a = d; }   /* "vadd.f32 s0, s1" = s0, s0, s1 */
            x = m->op | vsd(sg, d, 1) | vsd(sg, a, 2) | vsd(sg, b, 0);
        } else x = m->op | vsd(sg, d, 1) | vsd(sg, a, 0);
        if (dbl) x |= 0x100;
        break;
    }
    case V_CMP: case V_CMPE: {
        if (!sg && !dbl) fail_at(q);
        int k = sg ? 's' : 'd';
        int d = vreg(k); comma();
        skipws();
        uint32_t e = m->kind == V_CMPE ? 0x80 : 0;
        if (*p == '#') {
            p++;
            const char *z = p;
            if (*p != '0') fail();
            p++;
            if (*p == '.') { p++; if (*p != '0') fail_at(z); while (*p == '0') p++; }
            if (isalnum_(*p)) fail_at(z);
            x = 0x0eb50a40 | e | vsd(sg, d, 1);
        } else x = 0x0eb40a40 | e | vsd(sg, d, 1) | vsd(sg, vreg(k), 0);
        if (dbl) x |= 0x100;
        break;
    }
    case V_CVT: case V_CVTR: {
        int r = m->kind == V_CVTR;
        int to_s = vt("S32.F32") || vt("S32.F64"), to_u = vt("U32.F32") || vt("U32.F64");
        if (to_s || to_u) {                                 /* FTOSI/FTOUI (Z = not vcvtr) */
            int from_d = vt("S32.F64") || vt("U32.F64");
            int d = vreg('s'); comma(); int a = vreg(from_d ? 'd' : 's');
            x = 0x0ebc0a40 | (to_s ? 0x10000 : 0) | (r ? 0 : 0x80) | vsd(1, d, 1) | vsd(!from_d, a, 0);
            if (from_d) x |= 0x100;
            break;
        }
        if (r) fail_at(q);
        if (vt("F64.F32")) { int d = vreg('d'); comma(); x = 0x0eb70ac0 | vsd(0, d, 1) | vsd(1, vreg('s'), 0); break; }
        if (vt("F32.F64")) { int d = vreg('s'); comma(); x = 0x0eb70bc0 | vsd(1, d, 1) | vsd(0, vreg('d'), 0); break; }
        int fs = vt("F32.S32") || vt("F64.S32");
        if (fs || vt("F32.U32") || vt("F64.U32")) {         /* FSITO/FUITO */
            int to_d = vtype[1] == '6';
            int d = vreg(to_d ? 'd' : 's'); comma(); int a = vreg('s');
            x = 0x0eb80a40 | (fs ? 0x80 : 0) | vsd(!to_d, d, 1) | vsd(1, a, 0);
            if (to_d) x |= 0x100;
            break;
        }
        fail_at(q);
    }
    case V_LDR: case V_STR: {
        if (vtype[0] && !vt("32") && !vt("64") && !vt("F32") && !vt("F64")) fail_at(q);
        skipws();
        int k = (*p | 32) == 'd' ? 'd' : 's';
        int d = vreg(k); comma();
        skipws();
        uint32_t a;
        if (*p != '[') {                                    /* pc-relative: an address */
            const char *z = p;
            int32_t off = (int32_t)(target() - (pcaddr + 8));
            uint32_t u = off < 0 ? -(uint32_t)off : (uint32_t)off;
            if ((u & 3) || u > 1020) fail_at(z);
            a = 0x01000000 | (off >= 0 ? 0x00800000 : 0) | (15 << 16) | (u >> 2);
        } else {
            a = address_cp();
            if ((a & 0x01200000) != 0x01000000) fail_at(ops);   /* only [rn, #off] */
        }
        x = m->op | a | vsd(k == 's', d, 1) | (k == 'd' ? 0x100 : 0);
        break;
    }
    case V_LDMIA: case V_LDMDB: case V_STMIA: case V_STMDB:
    case V_FLDMIAX: case V_FLDMDBX: case V_FSTMIAX: case V_FSTMDBX: case V_PUSH: case V_POP: {
        static const uint32_t base[] = { 0x0c900a00, 0x0d300a00, 0x0c800a00, 0x0d200a00, 0x0d2d0a00, 0x0cbd0a00,
                                         0x0c900b01, 0x0d300b01, 0x0c800b01, 0x0d200b01 };
        int idx = m->kind - V_LDMIA, kind = 0, first, count;
        x = base[idx];
        if (m->kind == V_PUSH || m->kind == V_POP) x |= 0x00200000;
        else {
            int rn = reg();
            x |= rn << 16;
            int wb = accept('!');
            if (wb) x |= 0x00200000;
            else if (x & 0x01000000) fail();                 /* db needs "!" */
            comma();
        }
        int xform = m->kind >= V_FLDMIAX && m->kind <= V_FSTMDBX;
        if (xform) kind = 'd';
        const char *lq = p;
        vlist(&kind, &first, &count);
        if (kind == 'd' && (first + count > 16)) fail_at(lq);
        if (kind == 's' && (first + count > 32)) fail_at(lq);
        if (kind == 'd') x |= 0x100 | (first << 12) | (count * 2);
        else x |= vsd(1, first, 1) | count;
        break;
    }
    case V_MOV: {
        skipws();
        if (sg || dbl) {                                    /* vmov.f32 sd, sm / .f64 dd, dm */
            int k = sg ? 's' : 'd';
            int d = vreg(k); comma(); int a = vreg(k);
            x = 0x0eb00a40 | vsd(sg, d, 1) | vsd(sg, a, 0) | (dbl ? 0x100 : 0);
            break;
        }
        if (vt("32")) {                                     /* scalar <-> core */
            int dn = try_vreg('d');
            if (dn >= 0) {
                expect('['); const char *z = p; uint32_t i = number(); if (i > 1) fail_at(z); expect(']');
                comma();
                x = 0x0e000b10 | i << 21 | dn << 16 | reg() << 12;
            } else {
                int rt = reg(); comma(); dn = vreg('d');
                expect('['); const char *z = p; uint32_t i = number(); if (i > 1) fail_at(z); expect(']');
                x = 0x0e100b10 | i << 21 | dn << 16 | rt << 12;
            }
            break;
        }
        if (vtype[0]) fail_at(q);
        int s0 = try_vreg('s');
        if (s0 >= 0) {
            comma();
            int s1 = try_vreg('s');
            if (s1 >= 0) {                                  /* vmov sm, sm+1, rt, rt2 */
                if (s1 != s0 + 1) fail();
                comma(); int rt = reg(); comma(); int rt2 = reg();
                x = 0x0c400a10 | rt2 << 16 | rt << 12 | vsd(1, s0, 0);
            } else x = 0x0e000a10 | vsd(1, s0, 2) | reg() << 12;   /* vmov sn, rt */
            break;
        }
        int d0 = try_vreg('d');
        if (d0 >= 0) {                                      /* vmov dm, rt, rt2 */
            comma(); int rt = reg(); comma(); int rt2 = reg();
            x = 0x0c400b10 | rt2 << 16 | rt << 12 | d0;
            break;
        }
        int rt = reg(); comma();
        s0 = try_vreg('s');
        if (s0 >= 0) { x = 0x0e100a10 | vsd(1, s0, 2) | rt << 12; break; }  /* vmov rt, sn */
        int rt2 = reg(); comma();
        s0 = try_vreg('s');
        if (s0 >= 0) {                                      /* vmov rt, rt2, sm, sm+1 */
            comma(); const char *z = p; int s1 = vreg('s'); if (s1 != s0 + 1) fail_at(z);
            x = 0x0c500a10 | rt2 << 16 | rt << 12 | vsd(1, s0, 0);
        } else x = 0x0c500b10 | rt2 << 16 | rt << 12 | vreg('d');   /* vmov rt, rt2, dm */
        break;
    }
    case V_MRS: case V_MSR: {
        static const char sysn[5][8] = { "FPSID", "FPSCR", "FPEXC", "FPINST", "FPINST2" };
        static const uint8_t sysr[5] = { 0, 1, 8, 9, 10 };
        if (vtype[0]) fail_at(q);
        int rt = 0, sr = -1;
        int apsr = 0;
        if (m->kind == V_MRS) {
            if (kw("APSR_NZCV")) { rt = 15; apsr = 1; } else rt = reg();
            comma();
        }
        skipws();
        const char *z = p;
        for (int i = 4; i >= 0; i--) if (kw(sysn[i])) { sr = sysr[i]; break; }
        if (sr < 0 || (apsr && sr != 1)) fail_at(z);
        if (m->kind == V_MSR) { comma(); rt = reg(); }
        x = (m->kind == V_MRS ? 0x0ef00a10 : 0x0ee00a10) | sr << 16 | rt << 12;
        break;
    }
    }
    *w = c | x;
    return 1;
}

/* --------------------------------------------------------------- data */

static int data(int kind, uint8_t *out, int max)
{
    int n = 0;
    int size = kind == K_DB || kind == K_BYTE ? 1 : kind == K_DW || kind == K_SHORT ? 2 : 4;
    int debugnum = kind == K_DB || kind == K_DW || kind == K_DD;
    do {
        skipws();
        if ((*p == '\'' || *p == '"') && size == 1) {
            /* a quoted string (a doubled quote is one quote); 'c' is also GNU's char */
            int q = *p++;
            for (;;) {
                if (!*p || *p == '\r') fail();
                if (*p == q) { if (p[1] == q) p++; else { p++; break; } }
                if (n >= max) fail();
                out[n++] = *p++;
            }
            continue;
        }
        uint32_t v;
        const char *q = p;
        if (debugnum) {
            v = 0;
            int d = 0;
            while (hexval(*p) >= 0) { v = v * 16 + hexval(*p++); d++; }
            if (!d || d > size * 2 || isalnum_(*p)) fail_at(q);
        } else {
            if (!try_number(&v, 0)) fail();
            if (size == 1 && v > 0xff && v < 0xffffff80u) fail_at(q);
            if (size == 2 && v > 0xffff && v < 0xffff8000u) fail_at(q);
        }
        if (n + size > max) fail();
        for (int i = 0; i < size; i++) out[n++] = v >> (8 * i);
    } while (accept(','));
    return n;
}

/* ----------------------------------------------------------- assemble */

int assemble(const char *line, uint32_t addr, uint8_t *out, int max, const char **errpos)
{
    p = line;
    pcaddr = addr;
    regshift_ok = 0;
    if (SETJMP(asm_jb)) { *errpos = errp; return 0; }
    uint32_t vw;
    if (vfp_asm(&vw)) {
        if (!at_end() || max < 4) fail();
        out[0] = vw; out[1] = vw >> 8; out[2] = vw >> 16; out[3] = vw >> 24;
        return 4;
    }
    const struct mn *m = mnemonic();
    const char *opstart = p;
    uint32_t w = 0, c;
    int rd, rn, rm, rs;
    if (cond < 0) cond = 14;
    c = (uint32_t)cond << 28;
    (void)opstart;

    switch (m->kind) {
    case K_DB: case K_DW: case K_DD: case K_BYTE: case K_SHORT: case K_WORD: {
        int n = data(m->kind, out, max);
        if (!at_end()) fail();
        return n;
    }
    case K_DP3: {
        rd = reg(); comma();
        const char *q = p;
        int r2 = try_reg();
        if (r2 >= 0 && !accept(',')) {
            /* two-operand form: op rd, rm  ==  op rd, rd, rm */
            p = q;
            skipws();
            const char *ip = p;
            uint32_t o2 = op2();
            w = encode_dp(m->arg, sflag, rd, rd, o2, ip);
            break;
        }
        if (r2 >= 0) { rn = r2; }
        else {
            /* two-operand with immediate: op rd, #imm */
            p = q;
            skipws();
            const char *ip = p;
            uint32_t o2 = op2();
            w = encode_dp(m->arg, sflag, rd, rd, o2, ip);
            break;
        }
        skipws();
        const char *ip = p;
        uint32_t o2 = op2();
        w = encode_dp(m->arg, sflag, rn, rd, o2, ip);
        break;
    }
    case K_MOV: {
        rd = reg(); comma();
        skipws();
        const char *ip = p;
        uint32_t o2 = op2();
        w = encode_dp(m->arg, sflag, 0, rd, o2, ip);
        break;
    }
    case K_CMP: {
        rn = reg(); comma();
        skipws();
        const char *ip = p;
        uint32_t o2 = op2();
        w = encode_dp(m->arg, 1, rn, pflag ? 15 : 0, o2, ip);
        break;
    }
    case K_SHIFT: {
        rd = reg(); comma(); rm = reg(); comma();
        rs = try_reg();
        if (rs >= 0) w = c | 0x01a00010 | (rd << 12) | (rs << 8) | (m->arg << 5) | rm;
        else {
            skipws();
            const char *q = p;
            uint32_t n = imm(1);
            int t = m->arg;
            if (t == 0 && n > 31) fail_at(q);
            if (t == 3 && (n < 1 || n > 31)) fail_at(q);
            if ((t == 1 || t == 2) && (n < 1 || n > 32)) fail_at(q);
            w = c | 0x01a00000 | (rd << 12) | ((n & 31) << 7) | (t << 5) | rm;
        }
        if (sflag) w |= 0x00100000;
        break;
    }
    case K_RRX:
        rd = reg(); comma(); rm = reg();
        w = c | 0x01a00060 | (rd << 12) | rm | (sflag ? 0x00100000 : 0);
        break;
    case K_NOP:
        w = c | 0x01a00000;
        break;
    case K_ADR: {
        rd = reg(); comma();
        skipws();
        const char *q = p;
        uint32_t a = target();
        int32_t d = (int32_t)(a - (pcaddr + 8));
        int e = encimm(d >= 0 ? (uint32_t)d : -(uint32_t)d);
        if (e < 0) fail_at(q);
        w = c | 0x02000000 | ((d >= 0 ? 4u : 2u) << 21) | (15 << 16) | (rd << 12) | e;
        break;
    }
    case K_MUL:
        rd = reg(); comma(); rm = reg(); comma(); rs = reg();
        w = c | 0x90 | (rd << 16) | (rs << 8) | rm | (sflag ? 0x00100000 : 0);
        break;
    case K_MLA:
        rd = reg(); comma(); rm = reg(); comma(); rs = reg(); comma(); rn = reg();
        w = c | 0x00200090 | (rd << 16) | (rn << 12) | (rs << 8) | rm | (sflag ? 0x00100000 : 0);
        break;
    case K_MULL: {
        int lo = reg(); comma(); int hi = reg(); comma(); rm = reg(); comma(); rs = reg();
        w = c | 0x00800090 | ((uint32_t)(m->arg & 3) << 21) | (hi << 16) | (lo << 12) | (rs << 8) | rm |
            (sflag ? 0x00100000 : 0);
        break;
    }
    case K_SMULXY:
        rd = reg(); comma(); rm = reg(); comma(); rs = reg();
        w = c | 0x01600080 | (rd << 16) | (rs << 8) | (m->arg << 5) | rm;
        break;
    case K_SMLAXY:
        rd = reg(); comma(); rm = reg(); comma(); rs = reg(); comma(); rn = reg();
        w = c | 0x01000080 | (rd << 16) | (rn << 12) | (rs << 8) | (m->arg << 5) | rm;
        break;
    case K_SMULWY:
        rd = reg(); comma(); rm = reg(); comma(); rs = reg();
        w = c | 0x012000a0 | (rd << 16) | (rs << 8) | (m->arg << 5) | rm;
        break;
    case K_SMLAWY:
        rd = reg(); comma(); rm = reg(); comma(); rs = reg(); comma(); rn = reg();
        w = c | 0x01200080 | (rd << 16) | (rn << 12) | (rs << 8) | (m->arg << 5) | rm;
        break;
    case K_SMLALXY: {
        int lo = reg(); comma(); int hi = reg(); comma(); rm = reg(); comma(); rs = reg();
        w = c | 0x01400080 | (hi << 16) | (lo << 12) | (rs << 8) | (m->arg << 5) | rm;
        break;
    }
    case K_QADD:
        rd = reg(); comma(); rm = reg(); comma(); rn = reg();
        w = c | 0x01000050 | ((uint32_t)m->arg << 21) | (rn << 16) | (rd << 12) | rm;
        break;
    case K_CLZ:
        rd = reg(); comma(); rm = reg();
        w = c | 0x016f0f10 | (rd << 12) | rm;
        break;
    case K_LDR: case K_STR: {
        int L = m->arg;
        rd = reg(); comma();
        if (hx) {
            if (!L && (hx == 2 || hx == 3)) fail_at(line);  /* strsb/strsh */
            uint32_t a = address_mode(1, 1, 0);
            uint32_t sh;
            if (hx == 4) sh = L ? 0xd0 : 0xf0;
            else sh = hx == 1 ? 0xb0 : hx == 2 ? 0xd0 : 0xf0;
            w = c | a | sh | (rd << 12) | ((L && hx != 4) ? 0x00100000 : 0);
        } else {
            uint32_t a = address_mode(0, 1, tflag);
            w = c | 0x04000000 | a | (rd << 12) | (L ? 0x00100000 : 0) | (bflag ? 0x00400000 : 0);
        }
        break;
    }
    case K_PLD: {
        regshift_ok = 1;
        uint32_t a = address_mode(0, 1, 0);
        regshift_ok = 0;
        if (a & 0x00200000) fail();
        w = 0xf450f000 | a;
        break;
    }
    case K_LDM: case K_STM: {
        int L = m->arg;
        rn = reg();
        int wb = accept('!');
        comma();
        uint32_t list = reglist();
        int caret = accept('^');
        int md = mode < 0 ? 0 : mode;
        if (md >= 4) {
            /* stack aliases */
            static const uint8_t ldm_[4] = { 0, 2, 1, 3 }, stm_[4] = { 3, 1, 2, 0 };   /* fd fa ed ea */
            md = L ? ldm_[md - 4] : stm_[md - 4];
        }
        static const uint32_t pu[4] = { 0x00800000, 0x01800000, 0x00000000, 0x01000000 };  /* ia ib da db */
        w = c | 0x08000000 | pu[md] | (L ? 0x00100000 : 0) | (wb ? 0x00200000 : 0) |
            (caret ? 0x00400000 : 0) | (rn << 16) | list;
        break;
    }
    case K_PUSH: case K_POP: {
        uint32_t list = reglist();
        int L = m->arg;
        if ((list & (list - 1)) == 0 && !(list == (1u << 13) && !L)) {
            int r = 0;
            while (!(list & (1u << r))) r++;
            w = c | (L ? 0x049d0004 : 0x052d0004) | (r << 12);
        } else
            w = c | (L ? 0x08bd0000 : 0x092d0000) | list;
        break;
    }
    case K_SWP:
        rd = reg(); comma(); rm = reg(); comma(); expect('['); rn = reg(); expect(']');
        w = c | 0x01000090 | (bflag ? 0x00400000 : 0) | (rn << 16) | (rd << 12) | rm;
        break;
    case K_B: case K_BL: {
        skipws();
        const char *q = p;
        uint32_t a = target();
        int32_t d = (int32_t)(a - (pcaddr + 8));
        if ((d & 3) || d < -0x2000000 || d > 0x1fffffc) fail_at(q);
        w = c | (m->kind == K_BL ? 0x0b000000 : 0x0a000000) | (((uint32_t)d >> 2) & 0xffffff);
        break;
    }
    case K_BX: case K_BXJ:
        rm = reg();
        w = c | (m->kind == K_BX ? 0x012fff10 : 0x012fff20) | rm;
        break;
    case K_BLX: {
        rm = try_reg();
        if (rm >= 0) { w = c | 0x012fff30 | rm; break; }
        if (cond != 14) fail_at(line);
        skipws();
        const char *q = p;
        uint32_t a = target();
        int32_t d = (int32_t)(a - (pcaddr + 8));
        if ((d & 1) || d < -0x2000000 || d > 0x1fffffe) fail_at(q);
        w = 0xfa000000 | ((d & 2) ? 0x01000000 : 0) | (((uint32_t)d >> 2) & 0xffffff);
        break;
    }
    case K_SVC: {
        const char *q = p;
        uint32_t v = svcnum();
        if (v > 0xffffff) fail_at(q);
        w = c | 0x0f000000 | v;
        break;
    }
    case K_BKPT: case K_UDF: {
        const char *q = p;
        uint32_t v = at_end() ? 0 : svcnum();
        if (v > 0xffff) fail_at(q);
        w = (m->kind == K_BKPT ? 0xe1200070 : 0xe7f000f0) | ((v & 0xfff0) << 4) | (v & 15);
        break;
    }
    case K_MRS: {
        rd = reg(); comma();
        int r = 0;
        if (kw("CPSR") || kw("APSR")) r = 0;
        else if (kw("SPSR")) r = 1;
        else fail();
        w = c | 0x010f0000 | (r ? 0x00400000 : 0) | (rd << 12);
        break;
    }
    case K_MSR: {
        skipws();
        int r;
        if ((up(p[0]) == 'C' || up(p[0]) == 'A') && up(p[1]) == 'P' && up(p[2]) == 'S' && up(p[3]) == 'R') r = 0;
        else if (up(p[0]) == 'S' && up(p[1]) == 'P' && up(p[2]) == 'S' && up(p[3]) == 'R') r = 1;
        else fail();
        p += 4;
        uint32_t mask = 0;
        if (*p == '_') {
            p++;
            while (isalnum_(*p)) {
                int ch = up(*p);
                uint32_t b = ch == 'C' ? 1 : ch == 'X' ? 2 : ch == 'S' ? 4 : ch == 'F' ? 8 : 0;
                if (!b || (mask & b)) fail();
                mask |= b;
                p++;
            }
        } else if (isalnum_(*p)) fail();
        else mask = 9;                  /* "cpsr" = cpsr_fc */
        comma();
        skipws();
        uint32_t o2;
        if (*p == '#') {
            const char *ip = p;
            o2 = op2();
            if (op2_isimm) {
                int e = encimm(op2_imm);
                if (e < 0) fail_at(ip);
                o2 = 0x02000000 | e;
            }
        } else {
            o2 = op2();                 /* a shift here is should-be-zero, but encodable */
        }
        w = c | 0x0120f000 | (r ? 0x00400000 : 0) | (mask << 16) | o2;
        break;
    }
    case K_CDP: case K_MCR: case K_MRC: {
        int cp = cpnum(); comma();
        const char *q = p;
        uint32_t op1 = number();
        if (op1 > (m->kind == K_CDP ? 15u : 7u)) fail_at(q);
        comma();
        int d;
        if (m->kind == K_CDP) d = creg();
        else if (m->kind == K_MRC && kw("APSR_NZCV")) d = 15;
        else d = reg();
        comma(); int n = creg(); comma(); int mm = creg();
        uint32_t op2v = 0;
        if (accept(',')) {
            int br = accept('{');
            q = p;
            op2v = number();
            if (op2v > 7) fail_at(q);
            if (br) expect('}');
        }
        if (m->kind == K_CDP)
            w = 0x0e000000 | (op1 << 20) | (n << 16) | (d << 12) | (cp << 8) | (op2v << 5) | mm;
        else
            w = 0x0e000010 | (m->kind == K_MRC ? 0x00100000 : 0) | (op1 << 21) | (n << 16) | (d << 12) |
                (cp << 8) | (op2v << 5) | mm;
        w |= two ? 0xf0000000u : c;
        break;
    }
    case K_MCRR: case K_MRRC: {
        int cp = cpnum(); comma();
        const char *q = p;
        uint32_t op = number();
        if (op > 15) fail_at(q);
        comma(); rd = reg(); comma(); rn = reg(); comma(); int mm = creg();
        w = c | 0x0c400000 | (m->kind == K_MRRC ? 0x00100000 : 0) | (rn << 16) | (rd << 12) | (cp << 8) |
            (op << 4) | mm;
        break;
    }
    case K_LDC: case K_STC: {
        int cp = cpnum(); comma(); int d = creg(); comma();
        uint32_t a = address_cp();
        w = 0x0c000000 | a | (m->arg ? 0x00100000 : 0) | (lflag ? 0x00400000 : 0) | (d << 12) | (cp << 8);
        w |= two ? 0xf0000000u : c;
        break;
    }
    default:
        fail();
    }
    if (!at_end()) fail();
    if (max < 4) fail();
    out[0] = w; out[1] = w >> 8; out[2] = w >> 16; out[3] = w >> 24;
    return 4;
}
