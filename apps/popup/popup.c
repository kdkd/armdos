/*
 * POPUP.COM - a SideKick-style pop-up desk accessory for ARM-DOS.
 *
 *   POPUP        install (stays resident, INT 21h AH=31h)
 *   POPUP /U     remove it from memory again (if nothing hooked on top)
 *
 * Ctrl+Alt+P pops up a menu over whatever text-mode program is running:
 * Calculator (hex/dec, shows ARM immediate encodings), Notepad (saved to
 * C:\POPUP.TXT), ASCII table, About. Esc puts the screen back exactly.
 *
 * How it is resident, the way 1980s TSRs were:
 *   INT 15h AH=4Fh  keyboard intercept: sees Ctrl+Alt+P, swallows it, sets
 *                   the "hot key pressed" flag
 *   INT 09h         after the BIOS has handled a key: pop up at once if DOS
 *                   is idle (InDOS flag = 0, no critical error, BIOS video/
 *                   disk not busy)
 *   INT 08h         the timer tick: pop up as soon as that becomes true
 *   INT 28h         DOS idle (COMMAND.COM waiting at the prompt, InDOS = 1):
 *                   pop up from inside DOS where it is safe to do so
 *   INT 10h/13h     "busy" counters, so we never interrupt the BIOS
 *   INT 2Fh AH=C5h  installation check / removal
 * The InDOS flag comes from INT 21h AH=34h. The pop-up runs on its own stack.
 *
 * Freestanding: no C library, so the resident part stays small.
 *
 * Copyright (C) 1989 Europa Micro Systems (ARM-DOS project).
 */
#include <stdint.h>
#include <stddef.h>
#include "armdos.h"

#define MPLEX_ID 0xC5

/* ------------------------------------------------------------- helpers */

void *memcpy(void *d, const void *s, size_t n)
{
    uint8_t *dd = d; const uint8_t *ss = s;
    while (n--) *dd++ = *ss++;
    return d;
}
void *memset(void *d, int c, size_t n)
{
    uint8_t *dd = d;
    while (n--) *dd++ = c;
    return d;
}
static int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }

static int dos(struct armregs *r) { return _armdos_int21(r); }
static void clr(struct armregs *r) { memset(r, 0, sizeof *r); }

/* unsigned to decimal with thousands separators */
static char *udec(char *b, uint32_t v, int commas)
{
    char t[16]; int n = 0, o = 0;
    do { t[n++] = '0' + v % 10; v /= 10; } while (v);
    for (int i = n - 1; i >= 0; i--) {
        b[o++] = t[i];
        if (commas && i && i % 3 == 0) b[o++] = ',';
    }
    b[o] = 0;
    return b + o;
}
static char *uhex(char *b, uint32_t v, int digits)
{
    for (int i = digits - 1; i >= 0; i--) *b++ = "0123456789ABCDEF"[(v >> (i * 4)) & 15];
    *b = 0;
    return b;
}
static char *scat(char *b, const char *s) { while (*s) *b++ = *s++; *b = 0; return b; }

/* ----------------------------------------------------- resident state */

static armdos_vect_t old08, old09, old10, old13, old15, old28, old2f;
static volatile uint8_t *indos;         /* InDOS; indos[-1] = critical error flag */
static volatile int hot, active, busy10, busy13, beep_ticks;
static uint32_t hook_calls;
static uint32_t minsn_at_load;          /* instructions / 1M when installed */
static uint8_t *stack_top;              /* the pop-up's stack (the install-time stack) */
static uint32_t psp_addr, resident_bytes;

static uint32_t icount_m(void)
{
    uint32_t v = armdos_inb(0xF8);      /* reading F8h snapshots the counter */
    v |= armdos_inb(0xF9) << 8;
    v |= armdos_inb(0xFA) << 16;
    v |= (uint32_t)armdos_inb(0xFB) << 24;
    return v;
}

static void beep(void)
{
    armdos_outb(0x43, 0xB6);            /* PIT ch2, square wave, 880 Hz */
    armdos_outb(0x42, 1356 & 0xFF);
    armdos_outb(0x42, 1356 >> 8);
    armdos_outb(0x61, armdos_inb(0x61) | 3);
    beep_ticks = 3;
}

void popup_main(void);
void call_on_stack(void (*fn)(void), void *sp);     /* popasm.S */

static int safe_now(int from_idle)
{
    if (indos[-1]) return 0;                /* INT 24h in progress */
    if (!from_idle && indos[0]) return 0;   /* inside DOS */
    if (busy10 || busy13) return 0;
    return 1;
}

static void try_popup(int from_idle)
{
    if (!hot || active || !safe_now(from_idle)) return;
    hot = 0;
    uint8_t m = ARMDOS_BDA[0x49];
    if (m != 2 && m != 3 && m != 7) { beep(); return; }  /* graphics or 40 columns: no */
    active = 1;
    uint32_t cpsr;
    __asm__ volatile("mrs %0, cpsr" : "=r"(cpsr));
    armdos_enable();                        /* keys must keep coming (STI) */
    call_on_stack(popup_main, stack_top);
    __asm__ volatile("msr cpsr_c, %0" :: "r"(cpsr) : "memory");
    active = 0;
}

static void int08(struct armregs *f)
{
    hook_calls++;
    old08(f);                               /* BIOS tick + EOI */
    if (beep_ticks && --beep_ticks == 0) armdos_outb(0x61, armdos_inb(0x61) & ~3);
    try_popup(0);
}

static void int09(struct armregs *f)
{
    hook_calls++;
    old09(f);                               /* BIOS reads the key, calls INT 15h 4Fh, EOI */
    try_popup(0);
}

static void int15(struct armregs *f)
{
    hook_calls++;
    if ((f->r0 & 0xFF00) == 0x4F00) {
        uint8_t sc = f->r0 & 0xFF;
        uint8_t shifts = ARMDOS_BDA[0x17];
        if (sc == 0x19 && (shifts & 0x0C) == 0x0C) {        /* P with Ctrl+Alt */
            hot = 1;
            f->cpsr &= ~ARM_CPSR_C;                         /* swallow it */
            return;
        }
    }
    old15(f);
}

static void int28(struct armregs *f)
{
    hook_calls++;
    old28(f);
    try_popup(1);
}

static void int10(struct armregs *f) { busy10++; old10(f); busy10--; }
static void int13(struct armregs *f) { busy13++; old13(f); busy13--; }

static void int2f(struct armregs *f)
{
    if (((f->r0 >> 8) & 0xFF) == MPLEX_ID) {
        if ((f->r0 & 0xFF) == 0x00) {           /* installed? AL=FFh, BX = our PSP */
            f->r0 = (f->r0 & ~0xFFu) | 0xFF;
            f->r1 = psp_addr;
            f->r4 = 0x50505550;                 /* "PUPP" signature in SI */
        }
        return;
    }
    old2f(f);
}

/* ----------------------------------------------------------- screen */

static volatile uint16_t *vram;
static uint16_t saved[80 * 25];
static uint16_t saved_cursor, saved_shape;

static void put(int x, int y, const char *s, uint8_t a)
{
    volatile uint16_t *p = vram + y * 80 + x;
    while (*s && x++ < 80) *p++ = (uint8_t)*s++ | (a << 8);
}
static void putc_at(int x, int y, uint8_t c, uint8_t a) { vram[y * 80 + x] = c | (a << 8); }
static void fill(int x, int y, int w, int h, uint8_t c, uint8_t a)
{
    for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) putc_at(x + i, y + j, c, a);
}

static void window(int x, int y, int w, int h, uint8_t a, const char *title)
{
    fill(x, y, w, h, ' ', a);
    for (int i = 1; i < w - 1; i++) { putc_at(x + i, y, 0xCD, a); putc_at(x + i, y + h - 1, 0xCD, a); }
    for (int j = 1; j < h - 1; j++) { putc_at(x, y + j, 0xBA, a); putc_at(x + w - 1, y + j, 0xBA, a); }
    putc_at(x, y, 0xC9, a); putc_at(x + w - 1, y, 0xBB, a);
    putc_at(x, y + h - 1, 0xC8, a); putc_at(x + w - 1, y + h - 1, 0xBC, a);
    if (title) {
        int n = slen(title);
        int tx = x + (w - n - 2) / 2;
        putc_at(tx, y, ' ', a);
        put(tx + 1, y, title, (a & 0xF0) | 0x0E);
        putc_at(tx + n + 1, y, ' ', a);
    }
    /* shadow: darken what is underneath, keep the characters */
    for (int j = 1; j <= h; j++) for (int i = 0; i < 2; i++) {
        int xx = x + w + i, yy = y + j;
        if (xx < 80 && yy < 25) vram[yy * 80 + xx] = (vram[yy * 80 + xx] & 0xFF) | 0x0800;
    }
    for (int i = 2; i < w + 2; i++) {
        int xx = x + i, yy = y + h;
        if (xx < 80 && yy < 25) vram[yy * 80 + xx] = (vram[yy * 80 + xx] & 0xFF) | 0x0800;
    }
}

static void cursor(int on, int x, int y)
{
    struct armregs r; clr(&r);
    r.r0 = 0x0100; r.r2 = on ? 0x0607 : 0x2000; _armdos_int10(&r);
    if (on) { clr(&r); r.r0 = 0x0200; r.r1 = ARMDOS_BDA[0x62] << 8; r.r3 = (y << 8) | x; _armdos_int10(&r); }
}

static int getkey(void)
{
    struct armregs r; clr(&r);
    r.r0 = 0x0000;
    _armdos_int16(&r);
    return r.r0 & 0xFFFF;
}

static void restore_screen(void)
{
    for (int i = 0; i < 80 * 25; i++) vram[i] = saved[i];
}

/* ------------------------------------------------------- calculator */

enum { A_WIN = 0x1F, A_TXT = 0x1B, A_HI = 0x70, A_KEY = 0x1E, A_DIM = 0x17 };

/* ARM data-processing immediate: an 8-bit value rotated right by an even amount */
static int arm_imm(uint32_t v, uint32_t *enc)
{
    for (int rot = 0; rot < 16; rot++) {
        uint32_t r = rot ? (v << (2 * rot)) | (v >> (32 - 2 * rot)) : v;   /* ROL undoes ROR */
        if (r < 256) { *enc = (rot << 8) | r; return 1; }
    }
    return 0;
}

static void calc_show(int x, int y, uint32_t acc, uint32_t entry, int entering, int hex, char op)
{
    char b[64], *p;
    uint32_t v = entering ? entry : acc;
    fill(x + 2, y + 2, 44, 1, ' ', 0x30);
    p = b;
    if (hex) { p = scat(p, "0x"); uhex(p, v, 8); }
    else if ((int32_t)v < 0 && !entering) { *p++ = '-'; udec(p, -v, 0); }
    else udec(p, v, 0);
    put(x + 45 - slen(b), y + 2, b, 0x30);
    if (op) putc_at(x + 3, y + 2, op, 0x3E);
    put(x + 2, y + 1, hex ? "HEX" : "DEC", 0x1E);
    fill(x + 2, y + 4, 44, 4, ' ', A_WIN);
    p = scat(b, "Dec  "); p = udec(p, v, 1);
    if ((int32_t)v < 0) { p = scat(p, "  (-"); p = udec(p, -v, 1); scat(p, ")"); }
    put(x + 2, y + 4, b, A_TXT);
    p = scat(b, "Hex  "); p = uhex(p, v, 8); scat(p, "h");
    put(x + 2, y + 5, b, A_TXT);
    p = scat(b, "Bin  ");
    for (int i = 31; i >= 0; i--) { *p++ = (v >> i) & 1 ? '1' : '0'; if (i && !(i & 7)) *p++ = ' '; }
    *p = 0;
    put(x + 2, y + 6, b, A_TXT);
    uint32_t e;
    p = scat(b, "ARM  ");
    if (arm_imm(v, &e)) {
        p = scat(p, "MOV r0,#"); p = scat(p, "0x"); p = uhex(p, v, v > 0xFFFF ? 8 : v > 0xFF ? 4 : 2);
        p = scat(p, " = "); p = uhex(p, 0xE3A00000 | e, 8);
        p = scat(p, " (");
        p = uhex(p, e & 0xFF, 2); p = scat(p, " ROR "); p = udec(p, (e >> 8) * 2, 0); scat(p, ")");
    } else if (arm_imm(~v, &e)) {
        p = scat(p, "MVN r0,#0x"); p = uhex(p, ~v, 8); p = scat(p, " = "); p = uhex(p, 0xE3E00000 | e, 8);
    } else {
        p = scat(p, "no immediate: LDR r0,=0x"); uhex(p, v, 8);
    }
    put(x + 2, y + 7, b, 0x1A);
}

static void calculator(void)
{
    const int x = 16, y = 5, w = 48, h = 12;
    window(x, y, w, h, A_WIN, "Calculator");
    put(x + 2, y + 9, "+ - * / % & | ^ < > = Enter", A_KEY);
    put(x + 2, y + 10, "Tab dec/hex  C clear  N negate  Esc done", A_DIM);
    static uint32_t acc;
    static int hex;
    uint32_t entry = 0;
    int entering = 0;
    char op = 0;
    for (;;) {
        calc_show(x, y, acc, entry, entering, hex, op);
        int k = getkey(), c = k & 0xFF;
        if (c >= 'a' && c <= 'z') c -= 32;
        int d = -1;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (hex && c >= 'A' && c <= 'F') d = c - 'A' + 10;
        if (d >= 0) {
            if (!entering) { entry = 0; entering = 1; }
            entry = hex ? (entry << 4) | d : entry * 10 + d;
            continue;
        }
        switch (c) {
        case 27: return;
        case 9: hex = !hex; break;
        case 8: if (entering) entry = hex ? entry >> 4 : entry / 10; break;
        case 'C': acc = entry = 0; entering = 0; op = 0; break;
        case 'N': if (entering) entry = -entry; else acc = -acc; break;
        case '+': case '-': case '*': case '/': case '%': case '&': case '|': case '^':
        case '<': case '>': case '=': case 13: {
            if (entering) {
                uint32_t b = entry;
                switch (op) {
                case '+': acc += b; break;
                case '-': acc -= b; break;
                case '*': acc *= b; break;
                case '/': acc = b ? acc / b : 0; break;
                case '%': acc = b ? acc % b : 0; break;
                case '&': acc &= b; break;
                case '|': acc |= b; break;
                case '^': acc ^= b; break;
                case '<': acc = b < 32 ? acc << b : 0; break;
                case '>': acc = b < 32 ? acc >> b : 0; break;
                default: acc = b; break;
                }
                entering = 0;
            }
            op = (c == '=' || c == 13) ? 0 : c;
            break;
        }
        }
    }
}

/* ---------------------------------------------------------- notepad */

#define NL 12
#define NC 56
static char note[NL][NC + 1];
static int note_loaded;
static const char note_file[] = "C:\\POPUP.TXT";

static char nbuf[NL * (NC + 2)];

static void note_load(void)
{
    struct armregs r; clr(&r);
    char *buf = nbuf;
    memset(note, 0, sizeof note);
    r.r0 = 0x3D00; r.r3 = (uint32_t)note_file;
    if (dos(&r)) return;
    int fh = r.r0 & 0xFFFF;
    clr(&r); r.r0 = 0x3F00; r.r1 = fh; r.r2 = sizeof nbuf; r.r3 = (uint32_t)buf;
    int n = dos(&r) ? 0 : (int)(r.r0 & 0xFFFF);
    clr(&r); r.r0 = 0x3E00; r.r1 = fh; dos(&r);
    int l = 0, c = 0;
    for (int i = 0; i < n && l < NL; i++) {
        char ch = buf[i];
        if (ch == 0x1A) break;
        if (ch == '\r') continue;
        if (ch == '\n') { l++; c = 0; continue; }
        if (c < NC) note[l][c++] = ch;
    }
}

static int note_save(uint32_t *bytes)
{
    char *buf = nbuf;
    int n = 0, last = 0;
    for (int l = 0; l < NL; l++) {
        int len = slen(note[l]);
        while (len && note[l][len - 1] == ' ') len--;
        memcpy(buf + n, note[l], len); n += len;
        buf[n++] = '\r'; buf[n++] = '\n';
        if (len) last = n;
    }
    n = last;
    struct armregs r; clr(&r);
    r.r0 = 0x3C00; r.r2 = 0; r.r3 = (uint32_t)note_file;
    if (dos(&r)) return -1;
    int fh = r.r0 & 0xFFFF;
    clr(&r); r.r0 = 0x4000; r.r1 = fh; r.r2 = n; r.r3 = (uint32_t)buf;
    int err = dos(&r) || (int)(r.r0 & 0xFFFF) != n;
    clr(&r); r.r0 = 0x3E00; r.r1 = fh; dos(&r);
    *bytes = n;
    return err ? -1 : 0;
}

static void notepad(void)
{
    const int x = 10, y = 4, w = NC + 4, h = NL + 4;
    if (!note_loaded) { note_load(); note_loaded = 1; }
    window(x, y, w, h, A_WIN, "Notepad - C:\\POPUP.TXT");
    int cx = 0, cy = 0;
    char msg[48] = "F2 save  Esc close (saves)";
    for (;;) {
        for (int l = 0; l < NL; l++) {
            fill(x + 2, y + 1 + l, NC, 1, ' ', 0x1F);
            put(x + 2, y + 1 + l, note[l], 0x1F);
        }
        fill(x + 2, y + h - 2, NC, 1, ' ', A_WIN);
        put(x + 2, y + h - 2, msg, A_DIM);
        char pos[16], *p = pos; *p++ = 'L'; p = udec(p, cy + 1, 0); *p++ = ' '; *p++ = 'C'; udec(p, cx + 1, 0);
        put(x + w - 2 - slen(pos), y + h - 2, pos, A_KEY);
        cursor(1, x + 2 + cx, y + 1 + cy);
        int k = getkey(), c = k & 0xFF, sc = k >> 8;
        char *ln = note[cy];
        int len = slen(ln);
        if (c == 27 || sc == 0x3C) {                    /* Esc / F2 */
            uint32_t n;
            char *q = msg;
            if (note_save(&n)) scat(msg, "Could not save C:\\POPUP.TXT");
            else { q = scat(q, "Saved "); q = udec(q, n, 1); scat(q, " bytes to C:\\POPUP.TXT"); }
            if (c == 27) break;
            continue;
        }
        msg[0] = 0;
        if (c == 0 || c == 0xE0) {
            switch (sc) {
            case 0x48: if (cy) cy--; break;
            case 0x50: if (cy < NL - 1) cy++; break;
            case 0x4B: if (cx) cx--; else if (cy) { cy--; cx = slen(note[cy]); } break;
            case 0x4D: if (cx < NC - 1) cx++; break;
            case 0x47: cx = 0; break;
            case 0x4F: cx = len; break;
            case 0x53: if (cx < len) { memcpy(ln + cx, ln + cx + 1, len - cx); } break;
            }
        } else if (c == 13) {
            if (cy < NL - 1) {
                for (int l = NL - 1; l > cy + 1; l--) memcpy(note[l], note[l - 1], NC + 1);
                memset(note[cy + 1], 0, NC + 1);
                if (cx < len) { memcpy(note[cy + 1], ln + cx, len - cx); memset(ln + cx, 0, NC + 1 - cx); }
                cy++; cx = 0;
            }
        } else if (c == 8) {
            if (cx > len) cx = len;
            if (cx) { memcpy(ln + cx - 1, ln + cx, len - cx + 1); cx--; }
        } else if (c >= ' ') {
            if (len < NC) {
                while (len < cx) ln[len++] = ' ';
                for (int i = len; i > cx; i--) ln[i] = ln[i - 1];
                ln[cx] = c; ln[len + 1] = 0;
                if (cx < NC - 1) cx++;
            }
        }
        if (cx > NC - 1) cx = NC - 1;
    }
    cursor(0, 0, 0);
}

/* ------------------------------------------------------- ASCII table */

static void ascii_table(void)
{
    const int x = 13, y = 2, w = 54, h = 21;
    window(x, y, w, h, A_WIN, "ASCII Table");
    static int sel = 'A';
    for (;;) {
        char b[4] = "  ";
        put(x + 5, y + 1, "0 1 2 3 4 5 6 7 8 9 A B C D E F", A_KEY);
        for (int r = 0; r < 16; r++) {
            b[0] = "0123456789ABCDEF"[r]; b[1] = 'x'; b[2] = 0;
            put(x + 2, y + 2 + r, b, A_KEY);
            for (int c = 0; c < 16; c++) {
                int ch = r * 16 + c;
                putc_at(x + 5 + c * 2, y + 2 + r, ch, ch == sel ? 0x70 : 0x1F);
            }
        }
        char info[48], *p;
        fill(x + 38, y + 3, 14, 12, ' ', A_WIN);
        putc_at(x + 44, y + 3, sel, 0x70);
        putc_at(x + 43, y + 3, ' ', 0x70); putc_at(x + 45, y + 3, ' ', 0x70);
        p = scat(info, "Dec  "); udec(p, sel, 0); put(x + 39, y + 5, info, A_TXT);
        p = scat(info, "Hex  "); p = uhex(p, sel, 2); scat(p, "h"); put(x + 39, y + 6, info, A_TXT);
        p = scat(info, "Oct  "); *p++ = '0' + (sel >> 6); *p++ = '0' + ((sel >> 3) & 7); *p++ = '0' + (sel & 7); *p = 0;
        put(x + 39, y + 7, info, A_TXT);
        p = scat(info, "Bin  "); for (int i = 7; i >= 0; i--) *p++ = (sel >> i) & 1 ? '1' : '0'; *p = 0;
        put(x + 39, y + 8, info, A_TXT);
        if (sel < 32) {
            static const char ctl[] = "NULSOHSTXETXEOTENQACKBELBS HT LF VT FF CR SO SI DLEDC1DC2DC3DC4NAKSYNETBCANEM SUBESCFS GS RS US ";
            char nm[] = { 'N', 'a', 'm', 'e', ' ', ' ', ctl[sel * 3], ctl[sel * 3 + 1], ctl[sel * 3 + 2], 0 };
            put(x + 39, y + 10, nm, A_TXT);
        }
        p = scat(info, "MOV r0,#"); p = udec(p, sel, 0); put(x + 39, y + 12, info, 0x1A);
        put(x + 2, y + h - 2, "\x18\x19\x1B\x1A select, type a key to find it, Esc done", A_DIM);
        int k = getkey(), c = k & 0xFF, sc = k >> 8;
        if (c == 27) return;
        if (c == 0 || c == 0xE0) {
            if (sc == 0x48) sel = (sel + 240) & 255;
            else if (sc == 0x50) sel = (sel + 16) & 255;
            else if (sc == 0x4B) sel = (sel + 255) & 255;
            else if (sc == 0x4D) sel = (sel + 1) & 255;
        } else sel = c;
    }
}

/* ------------------------------------------------------------ about */

static void about(void)
{
    const int x = 12, y = 6, w = 56, h = 12;
    window(x, y, w, h, A_WIN, "About POPUP");
    char b[80], *p;
    put(x + 3, y + 2, "POPUP 1.00  Desk accessories for ARM-DOS", 0x1F);
    put(x + 3, y + 3, "(C) 1989 Europa Micro Systems", A_DIM);
    p = scat(b, "Resident: "); p = udec(p, resident_bytes, 1); p = scat(p, " bytes at "); p = uhex(p, psp_addr, 8); scat(p, "h");
    put(x + 3, y + 5, b, A_TXT);
    uint32_t n = icount_m() - minsn_at_load;
    p = scat(b, "While POPUP was resident, the ARM executed");
    put(x + 3, y + 6, b, A_TXT);
    if (n) { p = udec(b, n, 1); p = scat(p, " million instructions"); }
    else scat(b, "less than a million instructions");
    put(x + 5, y + 7, b, 0x1E);
    p = scat(b, "and POPUP's hooks ran "); p = udec(p, hook_calls, 1); scat(p, " times.");
    put(x + 3, y + 8, b, A_TXT);
    put(x + 3, y + 10, "Press any key", A_DIM);
    getkey();
}

/* ------------------------------------------------------------- menu */

void popup_main(void)
{
    uint32_t base = (ARMDOS_BDA[0x49] == 7 ? 0xB0000 : 0xB8000) + *(volatile uint16_t *)0x44E;   /* mode 7: Hercules/MDA */
    vram = (volatile uint16_t *)base;
    for (int i = 0; i < 80 * 25; i++) saved[i] = vram[i];
    saved_cursor = *(volatile uint16_t *)(0x450 + 2 * ARMDOS_BDA[0x62]);
    saved_shape = *(volatile uint16_t *)0x460;
    cursor(0, 0, 0);

    static const char *const items[5] = { "Calculator", "Notepad", "ASCII table", "About", "Exit" };
    static const char hk[5] = { 'C', 'N', 'A', 'B', 'X' };
    static const uint8_t hkpos[5] = { 0, 0, 0, 1, 1 };
    static int sel;
    int redraw = 1;
    for (;;) {
        if (redraw) { window(2, 1, 17, 9, 0x1F, "POPUP"); redraw = 0; }
        for (int i = 0; i < 5; i++) {
            fill(3, 3 + i, 15, 1, ' ', i == sel ? A_HI : 0x1F);
            put(5, 3 + i, items[i], i == sel ? A_HI : 0x1F);
            putc_at(5 + hkpos[i], 3 + i, items[i][hkpos[i]], i == sel ? 0x74 : 0x1E);
        }
        int k = getkey(), c = k & 0xFF, sc = k >> 8;
        if (c >= 'a' && c <= 'z') c -= 32;
        int go = -1;
        if (c == 27) break;
        if (sc == 0x48) sel = (sel + 4) % 5;
        else if (sc == 0x50) sel = (sel + 1) % 5;
        else if (c == 13) go = sel;
        else for (int i = 0; i < 5; i++) if (c == hk[i]) go = sel = i;
        if (go < 0) continue;
        if (go == 4) break;
        for (int i = 0; i < 5; i++) {       /* show the choice before the window opens */
            fill(3, 3 + i, 15, 1, ' ', i == sel ? A_HI : 0x1F);
            put(5, 3 + i, items[i], i == sel ? A_HI : 0x1F);
            putc_at(5 + hkpos[i], 3 + i, items[i][hkpos[i]], i == sel ? 0x74 : 0x1E);
        }
        if (go == 0) calculator();
        else if (go == 1) notepad();
        else if (go == 2) ascii_table();
        else about();
        restore_screen();
        redraw = 1;
    }
    restore_screen();
    struct armregs r; clr(&r);
    r.r0 = 0x0100; r.r2 = saved_shape; _armdos_int10(&r);
    clr(&r); r.r0 = 0x0200; r.r1 = ARMDOS_BDA[0x62] << 8; r.r3 = saved_cursor; _armdos_int10(&r);
}

/* ================================================= transient: install */

static void say(const char *s)
{
    struct armregs r; clr(&r);
    r.r0 = 0x4000; r.r1 = 1; r.r2 = slen(s); r.r3 = (uint32_t)s;
    dos(&r);
}

static armdos_vect_t getvect(int n)
{
    struct armregs r; clr(&r);
    r.r0 = 0x3500 | n; dos(&r);
    return (armdos_vect_t)r.r1;
}
static void setvect(int n, armdos_vect_t h)
{
    struct armregs r; clr(&r);
    r.r0 = 0x2500 | n; r.r3 = (uint32_t)h; dos(&r);
}

static int installed(uint32_t *psp)
{
    struct armregs r; clr(&r);
    r.r0 = MPLEX_ID << 8;
    _armdos_int2f(&r);
    if ((r.r0 & 0xFF) == 0xFF && r.r4 == 0x50505550) { *psp = r.r1; return 1; }
    return 0;
}

/* The resident copy's code and variables are at the same offsets from its PSP
 * as ours are from our PSP. */
static int uninstall(uint32_t psp)
{
    static const uint8_t nums[7] = { 0x08, 0x09, 0x10, 0x13, 0x15, 0x28, 0x2F };
    armdos_vect_t ours[7] = { int08, int09, int10, int13, int15, int28, int2f };
    armdos_vect_t *olds[7] = { &old08, &old09, &old10, &old13, &old15, &old28, &old2f };
    /* all our vectors must still be ours */
    for (int i = 0; i < 7; i++) {
        uint32_t theirs = (uint32_t)ours[i] - psp_addr + psp;
        if ((uint32_t)getvect(nums[i]) != theirs) return -1;
    }
    armdos_disable();
    for (int i = 0; i < 7; i++) {
        armdos_vect_t *o = (armdos_vect_t *)((uint8_t *)olds[i] - psp_addr + psp);
        setvect(nums[i], *o);
    }
    armdos_enable();
    struct armregs r; clr(&r);
    r.r0 = 0x4900; r.r8 = psp >> 4;
    dos(&r);
    return 0;
}

__attribute__((noreturn)) static void leave(int code)
{
    struct armregs r; clr(&r);
    r.r0 = 0x4C00 | code;
    dos(&r);
    for (;;) ;
}

void __armdos_start(struct psp *psp, uint8_t *base, uint8_t *blockend, uint8_t *stacktop)
{
    (void)base; (void)blockend;
    psp_addr = (uint32_t)psp;
    const uint8_t *tail = psp->cmdtail;
    int remove = 0;
    for (int i = 1; i <= tail[0]; i++)
        if (tail[i] == '/' && (tail[i + 1] | 0x20) == 'u') remove = 1;
        else if (tail[i] == '/' && tail[i + 1] == '?') {
            say("Installs POPUP, a pop-up calculator, notepad and ASCII table.\r\n\r\n"
                "POPUP [/U]\r\n\r\n  /U  removes POPUP from memory.\r\n\r\n"
                "Press Ctrl+Alt+P to pop it up over any text-mode program.\r\n");
            leave(0);
        }
    uint32_t other;
    if (installed(&other)) {
        if (!remove) { say("POPUP is already installed. Press Ctrl+Alt+P to activate.\r\n"); leave(1); }
        if (uninstall(other)) { say("POPUP cannot be removed: another program has hooked its interrupts.\r\n"); leave(2); }
        say("POPUP removed from memory.\r\n");
        leave(0);
    }
    if (remove) { say("POPUP is not installed.\r\n"); leave(1); }

    struct armregs r; clr(&r);
    r.r0 = 0x3400; dos(&r);
    indos = (volatile uint8_t *)r.r1;
    stack_top = (uint8_t *)((uint32_t)stacktop & ~7u);
    minsn_at_load = icount_m();

    old08 = getvect(0x08); old09 = getvect(0x09); old10 = getvect(0x10);
    old13 = getvect(0x13); old15 = getvect(0x15); old28 = getvect(0x28); old2f = getvect(0x2F);
    armdos_disable();
    setvect(0x10, int10); setvect(0x13, int13); setvect(0x15, int15);
    setvect(0x28, int28); setvect(0x2F, int2f); setvect(0x09, int09); setvect(0x08, int08);
    armdos_enable();

    say("POPUP installed. Press Ctrl+Alt+P to activate.\r\n");

    /* free our copy of the environment; keep PSP .. stack top */
    clr(&r);
    r.r0 = 0x4900; r.r8 = psp->envseg;
    if (psp->envseg && !dos(&r)) psp->envseg = 0;
    uint32_t paras = ((uint32_t)stacktop - (uint32_t)psp + 15) >> 4;
    resident_bytes = paras << 4;
    clr(&r);
    r.r0 = 0x3100; r.r3 = paras;
    dos(&r);
    for (;;) ;
}
