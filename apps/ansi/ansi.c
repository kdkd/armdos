/*
 * ansi.c - ANSI.SYS for ARM-DOS 4.00: the resident console driver.
 *
 * A re-creation in C of MS-DOS 4.00's DEV/ANSI (ANSI.ASM + IOCTL.ASM,
 * Microsoft, MIT licence), behaviour for behaviour: the same escape-sequence
 * state machine (parameters are collected in the key-reassignment buffer, as
 * the original does), the same command table (CUU CUD CUF CUB CUP/HVP ED EL
 * CPR SM RM SGR DSR key reassignment 'p', /X toggle 'q', save/restore
 * cursor), the same GRMODE attribute table and quirks (ED clears the whole
 * screen whatever its parameter, EL erases to the end of the line, an unknown
 * final character is displayed, ESC followed by anything but '[' displays
 * that character, SM/RM with 0-6/13h-19h both set the video mode, =7h/=7l
 * turn line wrap on/off, the cursor position report is queued as keyboard
 * input), output through INT 10h, input through INT 16h with the key
 * reassignment table, INT 29h fast console output, INT 1Bh Ctrl-Break, INT
 * 2Fh AX=1A00h install check / 1A01h IOCTL / 1A02h info and the generic IOCTL
 * 440Ch CON get/set display information (MODE CON LINES= COLS=).
 *
 * Deviations (ARM-PC hardware): the video mode table has only the modes the
 * ARM-PC BIOS provides (0-6, 13h); 43/50 lines are made by loading the BIOS
 * 8x8 font and programming the CRTC maximum-scan-line register directly (the
 * BIOS has no AX=1112h/1201h); CUD stops on the last line (as observed on
 * the real driver).
 */
#include "ansi.h"

#define ASNMAX  400             /* size of the key assignment buffer */
#define ESC     0x1B

enum { S1, S2, S3, S4 };

static void outc(int c);

/* ---------------------------------------------------------------- data */

__attribute__((section(".devhdr"), used))
struct devhdr ansi_header = {
    (struct devhdr *)0xFFFFFFFFu, 0xC053, 0, ansi_strategy, ansi_interrupt,
    { 'C', 'O', 'N', ' ', ' ', ' ', ' ', ' ' }
};

struct devhdr *ansi_oldcon RES;         /* the CON driver we replace */
armdos_vect_t ansi_old10 RES, ansi_old2f RES;
uint8_t ansi_switch_x RES, ansi_switch_l RES, ansi_switch_k RES, ansi_ext16 RES;

static struct reqhdr *req RES;
static uint8_t wrap RES;                /* 0 = wrap at end of line, 1 = no wrap */
static uint16_t asnptr RES = 4;         /* end of the key definitions in buf */
static uint8_t state RES;
static uint8_t vmode RES, maxcol RES, col RES, row RES;
static uint8_t savcol RES, savrow RES;
static uint8_t inq RES, prmcnt RES, keycnt RES;
static uint8_t *keyptr RES;
static uint8_t report[10] RES = { ESC, '[', '0', '0', ';', '0', '0', 'R', 13 };
static volatile uint8_t altah RES;      /* pending scan code / ^C from Ctrl-Break */
static uint8_t attr RES = 7, bpage RES;
static uint32_t screen RES;             /* address of the active text page */
static uint16_t req_rows RES = 25;      /* LINES requested through the IOCTL */
static uint8_t in_ioctl RES, crtc_moved RES;
/* the key assignment buffer: the definitions (length, key, replacement...),
   a 0 length, then the parameters of the escape sequence being parsed.
   Initially Ctrl-PrtSc (0;114) -> Ctrl-P, as in DOS 4. */
static uint8_t buf[ASNMAX + 10] RES = { 4, 0, 0x72, 16, 0 };

/* SGR parameter -> AND mask, OR mask (ANSI.ASM GRMODE) */
static const uint8_t grmode[][3] RESRO = {
    {  0, 0x00, 0x07 }, {  1, 0xFF, 0x08 }, {  4, 0xF8, 0x01 }, {  5, 0xFF, 0x80 },
    {  7, 0xF8, 0x70 }, {  8, 0x88, 0x00 }, { 30, 0xF8, 0x00 }, { 31, 0xF8, 0x04 },
    { 32, 0xF8, 0x02 }, { 33, 0xF8, 0x06 }, { 34, 0xF8, 0x01 }, { 35, 0xF8, 0x05 },
    { 36, 0xF8, 0x03 }, { 37, 0xF8, 0x07 }, { 40, 0x8F, 0x00 }, { 41, 0x8F, 0x40 },
    { 42, 0x8F, 0x20 }, { 43, 0x8F, 0x60 }, { 44, 0x8F, 0x10 }, { 45, 0x8F, 0x50 },
    { 46, 0x8F, 0x30 }, { 47, 0x8F, 0x70 }, { 0xFF, 0, 0 }
};

/* the display modes (IOCTL.ASM VIDEO_MODE_TABLE as DET_HDWR loads it for a
   VGA: COLOR_TABLE, then VGA_TABLE over it; ARM-PC has no 7, 0Dh-12h) */
struct vmode_ent { uint8_t mode, dmode; uint16_t colors, width, length, cols, rows; };
static const struct vmode_ent vtab[] RESRO = {
    { 1, 1, 16, 0xFFFF, 0xFFFF, 40, 0xFFFF },
    { 0, 1, 16, 0xFFFF, 0xFFFF, 40, 0xFFFF },
    { 3, 1, 16, 0xFFFF, 0xFFFF, 80, 0xFFFF },
    { 2, 1, 16, 0xFFFF, 0xFFFF, 80, 0xFFFF },
    { 4, 2, 4, 320, 200, 40, 25 },
    { 5, 2, 2, 320, 200, 40, 25 },
    { 6, 2, 2, 640, 200, 80, 25 },
    { 0x13, 2, 256, 320, 200, 40, 25 },
};
#define NVTAB (sizeof vtab / sizeof vtab[0])

/* -------------------------------------------------------------- helpers */

static void clr(struct armregs *r)
{
    uint32_t *p = (uint32_t *)r;
    for (unsigned i = 0; i < sizeof *r / 4; i++) p[i] = 0;
}

static void video(int ax, int bx, int cx, int dx)
{
    struct armregs r;
    clr(&r);
    r.r0 = ax; r.r1 = bx; r.r2 = cx; r.r3 = dx;
    int10(&r);
}

static int is_graphics(int m) { return m > 3 && m != 7; }

/* the number of text lines the driver works with (REQ_TXT_LENGTH) */
static int lines(void)
{
    if (vmode == 0x11 || vmode == 0x12) return 30;
    if (is_graphics(vmode)) return 25;
    return BDA8(0x84) + 1;
}

static void crtc(int reg, int v)
{
    *(volatile uint8_t *)0x100003D4 = reg;
    *(volatile uint8_t *)0x100003D5 = v;
}

/* ------------------------------------------------------------- output */

static void setit(void) { video(0x0200, bpage << 8, 0, (row << 8) | col); }

static void torom(int c) { video(0x0E00 | c, (bpage << 8) | (attr & 7), 0, 0); }

static void scroll(void)
{
    if (vmode == 2 || vmode == 3) {
        /* "myscroll": move the page up one line in memory */
        int cols = maxcol + 1;
        volatile uint16_t *s = (volatile uint16_t *)screen;
        unsigned n = (unsigned)(lines() - 1) * cols;
        for (unsigned i = 0; i < n; i++) s[i] = s[i + cols];
        uint16_t blank = (attr << 8) | ' ';
        for (int i = 0; i < cols; i++) s[n + i] = blank;
    } else {
        torom(10);              /* the teletype function scrolls */
    }
}

static void lf(void)
{
    row++;
    int lim = lines();
    if (row >= lim) {
        row = lim - 1;
        scroll();
    }
    setit();
}

static void chrout(int c)
{
    switch (c) {
    case 13:
        col = 0;
        setit();
        return;
    case 10:
        lf();
        return;
    case 7:
        torom(7);
        return;
    case 8:
        if (col == 0) return;
        col--;
        setit();
        return;
    }
    video(0x0900 | c, (bpage << 8) | attr, 1, 0);
    col++;
    if (col > maxcol) {
        if (wrap) { col--; return; }
        col = 0;
        lf();
        return;
    }
    setit();
}

static unsigned getptr(void)
{
    for (;;) {
        unsigned i = asnptr + 1u + prmcnt;
        if (i < ASNMAX + 8) return i;
        prmcnt--;
    }
}

static void zero_param(void)
{
    unsigned i = getptr();
    buf[i] = 0;
    buf[i + 1] = 0;
}

static void setcur(void)
{
    video(0x0200, 0, 0, (row << 8) | col);      /* BH = 0: page 0, as ANSI.ASM */
    state = S1;
}

static void movcur(uint8_t *v, uint8_t lim, uint8_t step, unsigned cx)
{
    while (cx--) {
        if (*v == lim) break;
        *v += step;
    }
    setcur();
}

/* key reassignment lookup: index of the definition of key ax, or -1 */
static int scan(uint16_t ax)
{
    unsigned i = 0;
    for (;;) {
        unsigned len = buf[i];
        if (len == 0) return -1;
        uint8_t al = ax & 0xFF;
        if (al == 0 || (al == 0xE0 && ansi_switch_x)) {
            if ((buf[i + 1] | (buf[i + 2] << 8)) == ax) return i;
        } else if (buf[i + 1] == al) return i;
        i += len;
        if (i >= ASNMAX + 8) return -1;
    }
}

static void keyasn(void)
{
    uint16_t dx = prmcnt + 2u;
    prmcnt = 0;
    unsigned p = getptr();
    uint16_t ax = buf[p] | (buf[p + 1] << 8);
    int j = scan(ax);
    if (j >= 0) {
        /* delete the old definition: shuffle the rest of the buffer down */
        unsigned len = buf[j];
        asnptr -= len;
        keycnt = 0;
        for (unsigned s = j + len; s < ASNMAX; s++) buf[s - len] = buf[s];
    }
    p = getptr();
    if ((dx & 0xFF) >= 3) {
        buf[p - 1] = dx;
        asnptr += dx;
        p += dx;
        if (asnptr >= ASNMAX) {
            p -= dx;
            asnptr -= dx;
        }
    }
    buf[p - 1] = 0;
    state = S1;
}

static uint16_t bin2asc(unsigned v)
{
    v = (v + 1) & 0xFF;
    unsigned q = 0;
    while (v >= 10) { v -= 10; q++; }
    return (('0' + q) & 0xFF) | ((('0' + v) & 0xFF) << 8);
}

static void command(int c)
{
    unsigned p = asnptr + 1u;
    uint8_t p0 = buf[p], p1 = buf[p + 1];
    unsigned cx = p0 ? p0 : 1;
    switch (c) {
    case 'A':                                   /* CUU */
        movcur(&row, 0, 0xFF, cx);
        return;
    case 'B':                                   /* CUD */
        movcur(&row, lines() - 1, 1, cx);
        return;
    case 'C':                                   /* CUF */
        movcur(&col, maxcol, 1, cx);
        return;
    case 'D':                                   /* CUB */
        movcur(&col, 0, 0xFF, cx);
        return;
    case 'H': case 'f': {                       /* CUP, HVP */
        if (cx > (unsigned)lines()) { setcur(); return; }
        uint8_t ch = p1;
        if (ch) ch--;
        if (!(maxcol > ch)) ch = maxcol;
        col = ch;
        row = cx - 1;
        setcur();
        return;
    }
    case 'J': case 'K': {                       /* ED (any parameter), EL */
        int top, bottom;
        if (c == 'J') {
            col = 0; row = 0;
            top = 0;
            bottom = (vmode == 0x11 || vmode == 0x12) ? 30 : lines();
        } else {
            top = row;
            bottom = row;
        }
        int a = is_graphics(vmode) ? 0 : attr;
        video(0x0600, a << 8, (top << 8) | (c == 'J' ? 0 : col), (bottom << 8) | maxcol);
        setcur();
        return;
    }
    case 'R':                                   /* CPR: ignored */
        state = S1;
        return;
    case 'h': case 'l': {                       /* SM, RM */
        if (p0 < 7 || (p0 >= 13 && p0 <= 19)) video(p0, 0, 0, 0);
        else if (p0 == 7) wrap = (c == 'l');
        state = S1;
        return;
    }
    case 'm': {                                 /* SGR */
        unsigned n = prmcnt + 1u;
        prmcnt = 0;
        unsigned i = getptr();
        while (n--) {
            uint8_t v = buf[i++];
            for (const uint8_t *g = grmode[0]; g[0] != 0xFF; g += 3)
                if (g[0] == v) { attr = (attr & g[1]) | g[2]; break; }
        }
        setcur();
        return;
    }
    case 'n': {                                 /* DSR: queue ESC[rr;ccR CR */
        struct armregs r;
        clr(&r);
        r.r0 = 0x0300;
        int10(&r);
        uint16_t a = bin2asc((r.r3 >> 8) & 0xFF), b = bin2asc(r.r3 & 0xFF);
        report[2] = a; report[3] = a >> 8;
        report[5] = b; report[6] = b >> 8;
        keycnt = 9;
        keyptr = report;
        state = S1;
        return;
    }
    case 'p':                                   /* key reassignment */
        keyasn();
        return;
    case 'q':                                   /* ESC[0q / ESC[1q: /X */
        if (p0 == 0) ansi_switch_x = 0;
        else if (p0 == 1) ansi_switch_x = 1;
        state = S1;
        return;
    case 's':
        savcol = col; savrow = row;
        setcur();
        return;
    case 'u':
        col = savcol; row = savrow;
        setcur();
        return;
    }
    /* not a command: display it */
    chrout(c);
    state = S1;
}

static void vt(int c)
{
    switch (state) {
    case S2:
        if (c == '[') {
            state = S3;
            inq = 0;
            prmcnt = 0;
            zero_param();
            return;
        }
        /* not '[': handled as in state S1 */
        __attribute__((fallthrough));
    case S1:
        if (c == ESC) { state = S2; return; }
        chrout(c);
        state = S1;
        return;
    case S3:
        if (c == ';') {
            prmcnt++;
            zero_param();
            return;
        }
        if (c >= '0' && c <= '9') {
            unsigned i = getptr();
            buf[i] = buf[i] * 10 + (c - '0');
            return;
        }
        if (c == '=' || c == '?') return;
        if (c == '"' || c == '\'') {
            state = S4;
            inq = c;
            return;
        }
        command(c);
        return;
    case S4:
        if (c == inq) {
            prmcnt--;
            state = S3;
            return;
        }
        buf[getptr()] = c;
        prmcnt++;
        zero_param();
        return;
    }
}

static void outc(int c)
{
    vmode = BDA8(0x49);
    maxcol = BDA8(0x4A) - 1;
    bpage = BDA8(0x62);
    uint16_t pos = BDA16(0x50 + bpage * 2);
    col = pos & 0xFF;
    row = pos >> 8;
    screen = (vmode == 7 ? 0xB0000 : 0xB8000) + BDA16(0x4E);
    vt(c & 0xFF);
}

/* -------------------------------------------------------------- input */

static uint16_t remap(uint16_t ax)
{
    if ((ax & 0xFF) == 0xE0 && (ax >> 8)) ax &= 0xFF00;
    return ax;
}

static uint16_t kbd(int fn)
{
    struct armregs r;
    clr(&r);
    r.r0 = fn << 8;
    int16(&r);
    return r.r0 & 0xFFFF;
}

static uint16_t key5(void)
{
    uint16_t ax = keyptr[0] | (keyptr[1] << 8);
    keycnt--;
    keyptr++;
    if ((ax & 0xFF) == 0) {
        keyptr++;
        keycnt--;
    }
    return ax;
}

static int chrin(void)
{
    uint16_t ax;
    if (altah) {
        int c = altah;
        altah = 0;
        return c;
    }
    for (;;) {
        if (keycnt) ax = key5();
        else {
            int j;
            if (ansi_ext16) {
                ax = kbd(0x10);
                if (!ansi_switch_x) ax = remap(ax);
                j = scan(ax);
                if (j < 0 && ansi_switch_x) ax = remap(ax);
            } else {
                ax = kbd(0x00);
                j = scan(ax);
            }
            if (j >= 0) {
                uint8_t n = buf[j] - 2;
                unsigned p = j + 2;
                if ((ax & 0xFF) == 0 || ((ax & 0xFF) == 0xE0 && ansi_switch_x)) { n--; p++; }
                keycnt = n;
                keyptr = &buf[p];
                ax = key5();
            }
        }
        if (ax == 0) continue;
        if (ax & 0xFF) return ax & 0xFF;
        altah = ax >> 8;
        return 0;
    }
}

/* non-destructive read: the character, or -1 if none (busy) */
static int rdnd(void)
{
    if (altah) return altah;
    if (keycnt) return keyptr[0];
    for (;;) {
        struct armregs r;
        clr(&r);
        r.r0 = ansi_ext16 ? 0x1100 : 0x0100;
        int16(&r);
        if (r.cpsr & ARM_CPSR_Z) return -1;
        uint16_t ax = r.r0 & 0xFFFF;
        if (ax == 0) {
            kbd(ansi_ext16 ? 0x10 : 0x00);      /* a 0000h (Ctrl-Break) key: eat it */
            continue;
        }
        int j = scan(ax);
        if (j < 0) {
            if (ansi_ext16 && ansi_switch_x) ax = remap(ax);
            return ax & 0xFF;
        }
        return buf[j + 1] == 0 ? buf[j + 3] : buf[j + 2];
    }
}

static void flush(void)
{
    altah = 0;
    keycnt = 0;
    int fn = ansi_ext16 ? 0x10 : 0x00;
    for (;;) {
        struct armregs r;
        clr(&r);
        r.r0 = (fn + 1) << 8;
        int16(&r);
        if (r.cpsr & ARM_CPSR_Z) break;
        kbd(fn);
    }
}

/* ------------------------------------------------ display information */

static uint16_t rd16(const uint8_t *p) { return p[0] | (p[1] << 8); }
static void wr16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }

/* 43 or 50 lines: the BIOS 8x8 font in a 400-line mode, 9 or 8 lines per
   character row (the ARM-PC VGA always shows 400 lines) */
static void set_rows(int rows)
{
    if (rows == 25) {
        if (crtc_moved) { crtc(9, 15); crtc_moved = 0; }
        return;
    }
    struct armregs r;
    clr(&r);
    r.r0 = 0x1130; r.r1 = 0x0300;
    int10(&r);
    uint32_t font = r.r6;
    clr(&r);
    r.r0 = 0x1110; r.r1 = 0x0800; r.r2 = 256; r.r3 = 0; r.r6 = font;
    int10(&r);
    crtc(9, rows == 50 ? 7 : 8);
    crtc_moved = 1;
    BDA8(0x84) = rows - 1;
    BDA8(0x85) = 8;
    BDA16(0x4C) = rows == 50 ? 8192 : 7168;
    video(0x0100, 0, 0x0607, 0);
}

static int find_mode(int m)
{
    for (unsigned i = 0; i < NVTAB; i++) if (vtab[i].mode == m) return i;
    return -1;
}

/* IOCTL.ASM GET_IOCTL: 0 or an error code (1 invalid, 10 not supported) */
static int get_ioctl(uint8_t *d)
{
    if (d[0] != 0 || rd16(d + 2) < 14) return 1;
    d[1] = 0;
    struct armregs r;
    clr(&r);
    r.r0 = 0x0F00;
    int10(&r);
    int i = find_mode(r.r0 & 0x7F);
    if (i < 0) return 10;
    const struct vmode_ent *e = &vtab[i];
    wr16(d + 2, 14);
    wr16(d + 4, (BDA8(0x65) & 0x20) ? 0 : 1);   /* bit 0: intensity instead of blink */
    d[6] = e->dmode;
    d[7] = 0;
    wr16(d + 8, e->colors);
    wr16(d + 10, e->width);
    wr16(d + 12, e->length);
    wr16(d + 14, e->cols);
    wr16(d + 16, e->dmode == 1 && e->rows != 25 ? BDA8(0x84) + 1 : e->rows);
    return 0;
}

/* IOCTL.ASM SET_IOCTL */
static int set_ioctl(uint8_t *d)
{
    if (d[0] != 0 || rd16(d + 2) != 14 || (rd16(d + 4) & ~1u)) return 1;
    int i;
    for (i = 0; i < (int)NVTAB; i++) {
        const struct vmode_ent *e = &vtab[i];
        if (e->dmode != d[6] || e->colors != rd16(d + 8) || d[7] != 0) continue;
        if (d[6] == 2) { if (e->width == rd16(d + 10) && e->length == rd16(d + 12)) break; }
        else if (e->cols == rd16(d + 14)) break;
    }
    if (i == (int)NVTAB) return 10;
    int rows = 25;
    if (vtab[i].dmode == 1) {
        rows = rd16(d + 16);
        if (rows != 25 && rows != 43 && rows != 50) return 10;
        req_rows = rows;
    }
    in_ioctl = 1;
    video(vtab[i].mode, 0, 0, 0);
    if (vtab[i].dmode == 1) {
        set_rows(rows);
        video(0x1003, (rd16(d + 4) & 1) ? 0 : 1, 0, 0);
    }
    in_ioctl = 0;
    return 0;
}

/* ------------------------------------------------------ the interface */

void ansi_strategy(struct reqhdr *r) { req = r; }

static void pass(struct reqhdr *r)
{
    ansi_oldcon->strategy(r);
    ansi_oldcon->entry();
}

void ansi_interrupt(void)
{
    struct reqhdr *r = req;
    switch (r->cmd) {
    case 0:
        ansi_init((struct req_init *)r);
        return;
    case 4: {
        struct req_rw *q = (struct req_rw *)r;
        uint8_t *p = (uint8_t *)q->addr;
        for (unsigned i = 0; i < q->count; i++) p[i] = chrin();
        break;
    }
    case 5: {
        int c = rdnd();
        if (c < 0) { r->status = RS_DONE | RS_BUSY; return; }
        ((struct req_ndread *)r)->ch = c;
        break;
    }
    case 7:
        flush();
        break;
    case 8: case 9: {
        struct req_rw *q = (struct req_rw *)r;
        const uint8_t *p = (const uint8_t *)q->addr;
        for (unsigned i = 0; i < q->count; i++) outc(p[i]);
        break;
    }
    case 19: {
        struct req_gioctl *q = (struct req_gioctl *)r;
        int e;
        if (q->minor == 0x7F) e = get_ioctl((uint8_t *)q->data);
        else if (q->minor == 0x5F) e = set_ioctl((uint8_t *)q->data);
        else { pass(r); return; }
        r->status = e ? (RS_DONE | RS_ERROR | e) : RS_DONE;
        return;
    }
    default:
        pass(r);
        return;
    }
    r->status = RS_DONE;
}

/* INT 29h: fast console output of AL */
void ansi_int29(struct armregs *f) { outc(f->r0 & 0xFF); }

/* INT 1Bh: Ctrl-Break - the next read returns ^C */
void ansi_int1b(struct armregs *f) { (void)f; altah = 3; }

/* INT 10h: after a mode set, keep the line count (/L) or put the CRTC back */
void ansi_int10(struct armregs *f)
{
    if (((f->r0 >> 8) & 0xFF) != 0 || in_ioctl) { ansi_old10(f); return; }
    ansi_old10(f);
    int m = BDA8(0x49);
    if (!ansi_switch_l) req_rows = 25;
    if (m <= 3 || m == 7) {
        in_ioctl = 1;
        set_rows(ansi_switch_l ? req_rows : 25);
        in_ioctl = 0;
    }
}

/* INT 2Fh AH=1Ah: ANSI.SYS's multiplex interface */
void ansi_int2f(struct armregs *f)
{
    int ah = (f->r0 >> 8) & 0xFF, al = f->r0 & 0xFF;
    if (ah != 0x1A || al > 2) {
        if (ansi_old2f) ansi_old2f(f); else f->cpsr |= ARM_CPSR_C;
        return;
    }
    uint8_t *d = (uint8_t *)f->r3;
    int e = 0;
    if (al == 0) {
        f->r0 = (f->r0 & ~0xFFu) | 0xFF;
    } else if (al == 1) {
        int cl = f->r2 & 0xFF;
        if (cl == 0x7F) {
            e = get_ioctl(d);
            if (!e && d[6] == 1 && ansi_switch_l) wr16(d + 16, req_rows);
        } else if (cl == 0x5F) e = set_ioctl(d);
        else e = 1;
        f->r0 = e;
    } else {
        if (d[0] == 1) d[2] = ansi_switch_l;
    }
    if (e) f->cpsr |= ARM_CPSR_C; else f->cpsr &= ~ARM_CPSR_C;
}
