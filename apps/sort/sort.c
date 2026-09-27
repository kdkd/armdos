/*
 * SORT - ARM-DOS re-creation of the MS-DOS 4.00 SORT filter.
 *
 *   SORT [/R] [/+n]          reads standard input, writes standard output
 *
 * A literal port of CMD/SORT/SORT.ASM of the MS-DOS 4.0 source (MIT licence,
 * (C) Microsoft Corp.): up to 64 KB of input is read into one DOS memory
 * block ("segment") and sorted in place there with 16-bit offsets, so every
 * quirk of the original carries over - records are CR LF separated (a lone
 * CR or LF stays in the line), lines shorter than the key column compare as
 * 256 blanks, equal keys keep their order (and come out reversed with /R),
 * the character order is the country collating table (INT 21h AX=6506h),
 * a last line without CR LF picks up the byte after the data, and a full
 * buffer is "SORT: Insufficient memory".
 */
#include "u4.h"

#define MAXREC 256
#define BUFFER MAXREC                   /* offset of the first record header */

static uint8_t *S;                      /* the 64 KB segment */
#define B(o)  S[(uint16_t)(o)]
static inline uint16_t W(uint16_t o) { return B(o) | (B(o + 1) << 8); }
static inline void setW(uint16_t o, uint16_t v) { B(o) = v; B(o + 1) = v >> 8; }

static uint16_t column;
static int reverse;
static uint8_t table[256];

static __attribute__((noreturn)) void error_exit(int rc, int parse_class)
{
    u4_puts(STDERR, "SORT: ");
    if (parse_class) u4_parse_err(STDERR, rc, 0);
    else u4_puts(STDERR, "Insufficient memory\r\n");   /* (the real one ends the line) */
    u4_exit(1);
}

static const struct u4_ctl sw1 = { 0, 0, "/R\0", 0, 0 };
static const struct u4_range colrange = { 1, 65535 };
static const struct u4_ctl sw2 = { P_NUM, P_COLON_NN, "/+\0", 1, &colrange };
static const struct u4_ctl *const sw_tab[] = { &sw1, &sw2 };
static const struct u4_parms parms = { 0, 0, 0, 2, sw_tab, 0, 0, ";", 0 };

static void parse(void)
{
    if (_armdos_psp->cmdtail[0] == 0) return;
    struct u4_pstate st = { u4_cmdline(), 0, 0, { 0 } };
    struct u4_result r;
    for (;;) {
        int rc = u4_parse(&parms, &st, &r);
        if (rc == P_RC_EOL) return;
        if (rc) error_exit(rc, 1);
        if (r.ctl == &sw1) reverse = 1;
        else if (r.ctl == &sw2) column = r.value;
        else error_exit(P_BAD_SWITCH, 1);
    }
}

/* the country collating table: INT 21h AX=6506h -> word length + table,
 * copied over the end of the built-in table as SORT does */
static void get_table(void)
{
    for (int i = 0; i < 256; i++) table[i] = i;
    uint8_t info[5];
    struct armregs r;
    u4_clr(&r);
    r.r0 = 0x6506; r.r1 = 0xFFFF; r.r2 = 5; r.r3 = 0xFFFF; r.r5 = (uint32_t)info;
    if (u4_int21(&r)) return;
    const uint8_t *t = (const uint8_t *)(info[1] | (info[2] << 8) | (info[3] << 16) | ((uint32_t)info[4] << 24));
    if (!t) return;
    unsigned n = t[0] | (t[1] << 8);
    if (n > 256) n = 256;
    memcpy(table + 256 - n, t + 2, n);
}

/* the string moves of the original, with 16-bit offset wrap-around (the
 * directions do not matter where the areas overlap as they do here:
 * memmove semantics) */
static void move(uint16_t dst, uint16_t src, uint32_t n)
{
    if ((uint32_t)dst + n <= 0x10000 && (uint32_t)src + n <= 0x10000) {
        memmove(S + dst, S + src, n);
        return;
    }
    if (dst > src) while (n--) B(dst + n) = B(src + n);
    else for (uint32_t i = 0; i < n; i++) B(dst + i) = B(src + i);
}

/* compare the records at si (candidate) and di (best so far); returns the
 * flags of the original's CMP as <0, 0, >0 (unsigned) */
static int compare(uint16_t si, uint16_t di)
{
    uint16_t ax = W(si), dx = W(di);
    if (ax > column) ax -= column; else { si = 0; ax = MAXREC; }
    if (dx > column) dx -= column; else { di = 0; dx = MAXREC; }
    uint16_t cx = ax < dx ? ax : dx;
    di += column; si += column;
    do {
        uint8_t a = table[B(si)], b = table[B(di)];
        si++; di++;
        if (a != b) return a < b ? -1 : 1;
    } while (--cx);
    return ax < dx ? -1 : ax > dx ? 1 : 0;
}

int main(void)
{
    if (!u4_version_ok()) { u4_puts(STDERR, "Incorrect DOS version\r\n"); return 0; }
    parse();
    column += 2;
    if (column != 2) column--;

    /* 64K worth of paragraphs, or whatever DOS has */
    struct armregs r;
    unsigned paras = 0x1000;
    for (;;) {
        u4_clr(&r);
        r.r0 = 0x4800; r.r1 = paras;
        if (!u4_int21(&r)) break;
        paras = r.r1 & 0xFFFF;
        if (!paras) error_exit(0, 0);
    }
    S = (uint8_t *)((r.r0 & 0xFFFF) << 4);
    get_table();
    uint16_t bp = paras << 4;           /* 64 KB = 0 in 16 bits, as the 8086's */
    for (int i = 0; i < MAXREC; i++) S[i] = ' ';

    /* read standard input */
    uint16_t dx = BUFFER + 2, cx = bp - (MAXREC + 2);
    for (;;) {
        int n = u4_read(STDIN, &B(dx), cx);
        if (n < 0) n = 0;
        dx += n;
        cx -= n;
        if (!cx) error_exit(0, 0);
        if (!n) break;
    }

    /* trim the last ^Z-terminated record */
    uint16_t bx = dx;
    cx = dx - (BUFFER + 2);
    int found = 1;                      /* REPNZ SCASB with CX=0 keeps ZF=1 */
    uint16_t di = BUFFER + 2;
    while (cx) {
        cx--;
        if (B(di++) == 0x1A) { found = 1; break; }
        found = 0;
    }
    if (found) bx--;
    bx -= cx;
    bx -= 2;
    if (W(bx) != 0x0A0D) {
        bx += 2;
        if (B(bx) != 0x1A) bx++;
    }
    bp = bx;
    setW(bp, 0);

    /* CR LF -> record headers (length words, header included) */
    bx = BUFFER;
    di = BUFFER + 2;
    for (;;) {
        cx = bp - di + 1;
        for (;;) {                      /* REPNZ SCASB for CR, then LF? */
            int hit = 0;
            while (cx) { cx--; if (B(di++) == 13) { hit = 1; break; } }
            if (!hit || B(di) == 10) break;
        }
        uint16_t ax = di - 1;
        setW(bx, ax - bx);
        bx = ax;
        di++;
        if (!cx) break;
    }
    setW(bx, 0);
    bp = bx + 2;

    /* the sort: pick the best of the unsorted rest, move it to the front */
    di = BUFFER;
    while (W(di)) {
        bx = di;
        uint16_t si = bx;
        for (;;) {
            si += W(si);
            if (!W(si)) break;
            int c = compare(si, bx);
            int better = reverse ? c >= 0 : c < 0;   /* JAE, patched to JB by /R */
            if (better) bx = si;
        }
        si = bx;
        if (si != di) {
            uint16_t len = W(si);
            /* move everything from di up by len (right to left) */
            move(di + len, di, (uint16_t)(bp - di));
            /* the record (now len further up) into the hole */
            move(di, si + len, len);
            /* squeeze out its old copy (word moves, left to right) */
            uint16_t dst = si + len;
            uint32_t words = (uint16_t)(bp + len - (si + len) + 1) >> 1;
            move(dst, dst + len, words * 2);
            setW(bp - 2, 0);
        }
        di += W(di);
    }

    /* headers back to CR LF */
    di = BUFFER;
    cx = W(di);
    for (;;) {
        di += cx;
        cx = W(di);
        setW(di, 0x0A0D);
        if (!cx) break;
    }
    uint16_t len = bp - (BUFFER + 2);
    int n = u4_write(STDOUT, &B(BUFFER + 2), len);
    return (n < 0 || n != len) ? 1 : 0;
}
