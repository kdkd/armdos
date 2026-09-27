/*
 * ARMINFO.EXE - System Information for the ARM/AT, in the style of the
 * Norton Utilities' SI (full-screen, blue boxes) with a teletype report
 * (/T, the SI 4.x style, redirectable).
 *
 *   ARMINFO           full-screen, five pages (PgDn/PgUp, 1-5, Esc)
 *   ARMINFO /B        start at the benchmarks
 *   ARMINFO /T [/N]   teletype report on standard output (/N: no benchmarks)
 *
 * Every figure is measured or read from the machine: the ROM BIOS strings
 * (scanned at FFF00000h), CP15 (MRC p15), the CPSR, INT 11h/12h/13h/15h/
 * 10h/33h, INT 21h 30h/48h/52h, XMS, the MCB chain and the system board
 * ports. The benchmarks run real ARM code timed with the 8253 PIT.
 *
 * Copyright (C) 1989 Europa Micro Systems (ARM-DOS project).
 */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include "armdos.h"
#include "arminfo.h"

/* ================================================================ screen */

#define V ((volatile uint16_t *)(*(volatile uint8_t *)0x449 == 7 ? 0xB0000 : 0xB8000))   /* mode 7: Hercules/MDA */

enum {
    A_DESK = 0x17,  /* desktop */
    A_BAR = 0x30,   /* top/bottom bars: black on cyan */
    A_BARK = 0x3F,  /* key names on the bars */
    A_BOX = 0x1F,   /* frame */
    A_TTL = 0x1E,   /* box title / section heads (yellow) */
    A_LBL = 0x1B,   /* labels (light cyan) */
    A_VAL = 0x1F,   /* values (bright white) */
    A_DIM = 0x17,   /* grey remarks */
    A_OK = 0x1A,    /* green check */
    A_NO = 0x1C,    /* red dash */
    A_BAR1 = 0x1E,  /* "this computer" bar */
    A_BAR2 = 0x13,  /* reference bars */
};

static void put(int x, int y, const char *s, uint8_t a)
{
    volatile uint16_t *p = V + y * 80 + x;
    while (*s && x < 80) { *p++ = (uint8_t)*s++ | (a << 8); x++; }
}

static void putn(int x, int y, const char *s, int n, uint8_t a)
{
    volatile uint16_t *p = V + y * 80 + x;
    for (int i = 0; i < n && x + i < 80; i++) p[i] = (uint8_t)(*s ? *s++ : ' ') | (a << 8);
}

static void fill(int x, int y, int w, int h, uint8_t c, uint8_t a)
{
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) V[(y + j) * 80 + x + i] = c | (a << 8);
}

__attribute__((unused)) static void putf(int x, int y, uint8_t a, const char *fmt, ...)
{
    char b[96];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    put(x, y, b, a);
}

static void shadow(int x, int y, int w, int h)
{
    for (int j = 1; j <= h; j++)
        for (int i = 0; i < 2; i++) {
            volatile uint16_t *p = V + (y + j) * 80 + x + w + i;
            if (x + w + i < 80 && y + j < 25) *p = (*p & 0xFF) | 0x0800;
        }
    for (int i = 2; i < w + 2; i++) {
        volatile uint16_t *p = V + (y + h) * 80 + x + i;
        if (x + i < 80 && y + h < 25) *p = (*p & 0xFF) | 0x0800;
    }
}

static void box(int x, int y, int w, int h, uint8_t a, const char *title)
{
    fill(x, y, w, h, ' ', a);
    V[y * 80 + x] = 0xC9 | (a << 8);
    V[y * 80 + x + w - 1] = 0xBB | (a << 8);
    V[(y + h - 1) * 80 + x] = 0xC8 | (a << 8);
    V[(y + h - 1) * 80 + x + w - 1] = 0xBC | (a << 8);
    for (int i = 1; i < w - 1; i++) {
        V[y * 80 + x + i] = 0xCD | (a << 8);
        V[(y + h - 1) * 80 + x + i] = 0xCD | (a << 8);
    }
    for (int j = 1; j < h - 1; j++) {
        V[(y + j) * 80 + x] = 0xBA | (a << 8);
        V[(y + j) * 80 + x + w - 1] = 0xBA | (a << 8);
    }
    if (title) {
        int n = strlen(title) + 2;
        int tx = x + (w - n) / 2;
        V[y * 80 + tx] = ' ' | (A_TTL << 8);
        put(tx + 1, y, title, A_TTL);
        V[y * 80 + tx + n - 1] = ' ' | (A_TTL << 8);
    }
    shadow(x, y, w, h);
}

/* a single-line separator across a box */
static void hsep(int x, int y, int w, uint8_t a)
{
    V[y * 80 + x] = 0xC7 | (a << 8);
    for (int i = 1; i < w - 1; i++) V[y * 80 + x + i] = 0xC4 | (a << 8);
    V[y * 80 + x + w - 1] = 0xB6 | (a << 8);
}

/* "1,234,567" */
static char *commas(char *b, uint32_t v)
{
    char t[16];
    int n = snprintf(t, sizeof t, "%lu", (unsigned long)v), o = 0;
    for (int i = 0; i < n; i++) {
        b[o++] = t[i];
        if ((n - 1 - i) % 3 == 0 && i != n - 1) b[o++] = ',';
    }
    b[o] = 0;
    return b;
}

static int int10(struct armregs *r) { return _armdos_int10(r); }
static int int21(struct armregs *r) { return _armdos_int21(r); }

static uint16_t cursor_shape, cursor_pos;
static uint16_t saved_screen[80 * 25];
static uint8_t saved_mode;

static void screen_begin(void)
{
    struct armregs r = { 0 };
    r.r0 = 0x0F00; int10(&r);
    saved_mode = r.r0 & 0x7F;
    if (saved_mode != 3 && saved_mode != 2) {
        memset(&r, 0, sizeof r); r.r0 = 0x0003; int10(&r);
    }
    memset(&r, 0, sizeof r); r.r0 = 0x0300; int10(&r);
    cursor_shape = r.r2; cursor_pos = r.r3;
    for (int i = 0; i < 80 * 25; i++) saved_screen[i] = V[i];
    memset(&r, 0, sizeof r); r.r0 = 0x0100; r.r2 = 0x2000; int10(&r);    /* hide cursor */
}

static void screen_end(void)
{
    struct armregs r = { 0 };
    if (saved_mode != 3 && saved_mode != 2) {
        memset(&r, 0, sizeof r); r.r0 = saved_mode; int10(&r);
        return;
    }
    for (int i = 0; i < 80 * 25; i++) V[i] = saved_screen[i];
    memset(&r, 0, sizeof r); r.r0 = 0x0100; r.r2 = cursor_shape; int10(&r);
    memset(&r, 0, sizeof r); r.r0 = 0x0200; r.r3 = cursor_pos; int10(&r);
}

static int getkey(void)
{
    struct armregs r = { 0 };
    r.r0 = 0x0000;
    _armdos_int16(&r);
    return r.r0 & 0xFFFF;
}

__attribute__((unused)) static int keyready(void)
{
    struct armregs r = { 0 };
    r.r0 = 0x0100;
    _armdos_int16(&r);
    return !(r.cpsr & ARM_CPSR_Z);
}

/* ============================================================ the facts */

struct info {
    /* BIOS */
    char bios_maker[48], bios_ver[12], bios_date[12], bios_date_long[40], bios_copr[64];
    uint32_t bios_copr_addr;
    uint8_t model, submodel, feat1;
    /* CPU */
    uint32_t id, cachetype, control, cpsr, mhz, turbo, board;
    uint32_t minsn0;
    /* DOS */
    unsigned dos_major, dos_minor, dos_oem;
    /* memory */
    unsigned conv_kb, dos_free, ext_kb, ext_total, xms, xms_ver, xms_free, xms_largest;
    /* equipment */
    uint16_t equip;
    unsigned floppies, fd_type, hds, hd_c, hd_h, hd_s, hd_mb;
    char drives[32];
    int ndrives;
    unsigned vid_dcc, vid_mode, vid_cols, vid_rows;
    int mouse_drv, mouse_buttons, mouse_port;
    int ncom, nlpt, kbd101;
    uint16_t com[4], lpt[3];
};

static struct info I;

static uint8_t bda(unsigned off) { volatile uint8_t *p = (volatile uint8_t *)0x400; __asm__("" : "+r"(p)); return p[off]; }

static const char *rom_scan(const char *needle)
{
    const char *p = (const char *)0xFFF00000;
    int n = strlen(needle);
    for (int i = 0; i < 0x20000 - n; i++)
        if (p[i] == needle[0] && !memcmp(p + i, needle, n)) return p + i;
    return 0;
}

static int isdig(char c) { return c >= '0' && c <= '9'; }

/* NUL-delimited strings in the ROM matching a pattern: 9 = digit, other = itself */
static const char *rom_pattern(const char *pat)
{
    const char *p = (const char *)0xFFF00000;
    int n = strlen(pat);
    for (int i = 1; i < 0x20000 - n - 1; i++) {
        if (p[i - 1] != 0 || p[i + n] != 0) continue;
        int k;
        for (k = 0; k < n; k++) {
            char c = p[i + k], q = pat[k];
            if (q == '9' ? !isdig(c) : c != q) break;
        }
        if (k == n) return p + i;
    }
    return 0;
}

static const char *const month[] = { "January", "February", "March", "April", "May", "June", "July",
                                     "August", "September", "October", "November", "December" };
static const char *const wday[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };

static void long_date(char *out, int m, int d, int y)
{
    int yy = y, mm = m;                 /* Zeller-style day of week (Sakamoto) */
    static const int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    if (mm < 3) yy--;
    int w = (yy + yy / 4 - yy / 100 + yy / 400 + t[mm - 1] + d) % 7;
    sprintf(out, "%s, %s %d, %d", wday[w], month[(m - 1) % 12], d, y);
}

static void gather(void)
{
    struct armregs r;

    /* ---- BIOS: strings in the ROM at FFF00000h */
    const char *c = rom_scan("Copyright (C) 19");
    if (c) {
        I.bios_copr_addr = (uint32_t)c;
        snprintf(I.bios_copr, sizeof I.bios_copr, "%s", c);
        const char *comma = strchr(c, ',');
        snprintf(I.bios_maker, sizeof I.bios_maker, "%s", comma ? comma + 2 : "Unknown");
        for (char *q = I.bios_maker; *q; q++) if (*q < ' ') { *q = 0; break; }
        for (char *q = I.bios_copr; *q; q++) if (*q < ' ') { *q = 0; break; }
    } else strcpy(I.bios_maker, "Unknown");
    const char *v = rom_pattern("9.99");
    snprintf(I.bios_ver, sizeof I.bios_ver, "%s", v ? v : "?");
    const char *d = rom_pattern("99/99/99");
    if (d) {
        memcpy(I.bios_date, d, 8);
        int m = atoi(d), dd = atoi(d + 3), y = 1900 + atoi(d + 6);
        long_date(I.bios_date_long, m, dd, y);
    } else strcpy(I.bios_date_long, "(no date found)");
    memset(&r, 0, sizeof r); r.r0 = 0xC000;
    if (!_armdos_intr(0x15, &r) && r.r1) {
        const uint8_t *t = (const uint8_t *)r.r1;
        I.model = t[2]; I.submodel = t[3]; I.feat1 = t[5];
    }

    /* ---- CPU */
    I.id = cp15_id();
    I.cachetype = cp15_cachetype();
    I.control = cp15_control();
    I.cpsr = read_cpsr();
    I.board = armdos_inb(0xF0);
    I.mhz = armdos_inb(0xF1);
    I.turbo = armdos_inb(0xF2);

    /* ---- DOS */
    memset(&r, 0, sizeof r); r.r0 = 0x3000; int21(&r);
    I.dos_major = r.r0 & 0xFF; I.dos_minor = (r.r0 >> 8) & 0xFF; I.dos_oem = (r.r1 >> 8) & 0xFF;

    /* ---- memory */
    memset(&r, 0, sizeof r); _armdos_intr(0x12, &r); I.conv_kb = r.r0 & 0xFFFF;
    memset(&r, 0, sizeof r); r.r0 = 0x4800; r.r1 = 0xFFFF; int21(&r); I.dos_free = (r.r1 & 0xFFFF) * 16;
    memset(&r, 0, sizeof r); r.r0 = 0x8800;
    if (!_armdos_intr(0x15, &r)) I.ext_kb = r.r0 & 0xFFFF;
    armdos_outb(0x70, 0x30); I.ext_total = armdos_inb(0x71);
    armdos_outb(0x70, 0x31); I.ext_total |= armdos_inb(0x71) << 8;   /* CMOS: KB above 1 MB */
    if (!I.ext_total) I.ext_total = I.ext_kb + 64;
    void *xe = armdos_xms_entry();
    if (xe) {
        I.xms = 1;
        memset(&r, 0, sizeof r); r.r0 = 0x0000; _armdos_farcall(xe, &r); I.xms_ver = r.r0 & 0xFFFF;
        memset(&r, 0, sizeof r); r.r0 = 0x0800; _armdos_farcall(xe, &r);
        I.xms_largest = r.r0 & 0xFFFF; I.xms_free = r.r3 & 0xFFFF;
    }

    /* ---- equipment */
    memset(&r, 0, sizeof r); _armdos_intr(0x11, &r); I.equip = r.r0;
    I.floppies = (I.equip & 1) ? ((I.equip >> 6) & 3) + 1 : 0;
    memset(&r, 0, sizeof r); r.r0 = 0x0800; r.r3 = 0x00;
    if (!_armdos_int13(&r)) I.fd_type = r.r1 & 0xFF;
    memset(&r, 0, sizeof r); r.r0 = 0x0800; r.r3 = 0x80;
    if (!_armdos_int13(&r)) {
        I.hds = r.r3 & 0xFF;
        I.hd_h = ((r.r3 >> 8) & 0xFF) + 1; I.hd_s = r.r2 & 0x3F;
        I.hd_c = (((r.r2 >> 8) & 0xFF) | ((r.r2 & 0xC0) << 2)) + 1;
        I.hd_mb = (uint32_t)I.hd_c * I.hd_h * I.hd_s / 2048;
    }
    for (int dr = 1; dr <= 26; dr++) {
        memset(&r, 0, sizeof r); r.r0 = 0x4408; r.r1 = dr;
        if (int21(&r) && (r.r0 & 0xFFFF) == 0x0F) continue;
        if (dr == 2 && I.floppies < 2) continue;     /* B: is A:'s phantom */
        I.drives[I.ndrives++] = '@' + dr;
    }
    memset(&r, 0, sizeof r); r.r0 = 0x1A00; int10(&r);
    I.vid_dcc = (r.r0 & 0xFF) == 0x1A ? (r.r1 & 0xFF) : 0;
    memset(&r, 0, sizeof r); r.r0 = 0x0F00; int10(&r);
    I.vid_mode = r.r0 & 0x7F; I.vid_cols = (r.r0 >> 8) & 0xFF;
    I.vid_rows = bda(0x84) + 1;
    if (armdos_getvect(0x33)) {
        memset(&r, 0, sizeof r);
        _armdos_int33(&r);
        if ((r.r0 & 0xFFFF) == 0xFFFF) { I.mouse_drv = 1; I.mouse_buttons = r.r1 & 0xFFFF; }
    }
    I.mouse_port = (I.equip >> 2) & 1;
    for (int i = 0; i < 4; i++) { I.com[i] = *(volatile uint16_t *)(0x400 + 2 * i); if (I.com[i]) I.ncom++; }
    for (int i = 0; i < 3; i++) { I.lpt[i] = *(volatile uint16_t *)(0x408 + 2 * i); if (I.lpt[i]) I.nlpt++; }
    I.kbd101 = (bda(0x96) & 0x10) != 0;
}

static const char *video_name(unsigned dcc)
{
    switch (dcc) {
    case 0x08: return "Video Graphics Array (VGA), Color";
    case 0x07: return "Video Graphics Array (VGA), Mono";
    case 0x04: return "Enhanced Graphics Adapter (EGA)";
    case 0x02: return "Color Graphics Adapter (CGA)";
    case 0x01: return "Monochrome Display Adapter";
    default: return "Unknown";
    }
}

static const char *mode_name(unsigned m)
{
    switch (m) {
    case 0: case 1: return "Text, 40 x 25 Color";
    case 2: case 3: return "Text, 80 x 25 Color";
    case 4: case 5: return "Graphics, 320 x 200, 4 colors";
    case 6: return "Graphics, 640 x 200, 2 colors";
    case 0x13: return "Graphics, 320 x 200, 256 colors";
    default: return "Other";
    }
}

static const char *fd_name(unsigned t)
{
    switch (t) {
    case 1: return "360K, 5\xAC\"";
    case 2: return "1.2M, 5\xAC\"";
    case 3: return "720K, 3\xAB\"";
    case 4: return "1.44M, 3\xAB\"";
    default: return "1.44M, 3\xAB\"";
    }
}

static const char *cpu_mode_name(uint32_t m)
{
    switch (m & 0x1F) {
    case 0x10: return "USR (User)";
    case 0x11: return "FIQ (Fast Interrupt)";
    case 0x12: return "IRQ (Interrupt)";
    case 0x13: return "SVC (Supervisor)";
    case 0x17: return "ABT (Abort)";
    case 0x1B: return "UND (Undefined)";
    case 0x1F: return "SYS (System)";
    default: return "?";
    }
}

static const char *arch_name(unsigned a)
{
    switch (a) {
    case 1: return "ARMv4"; case 2: return "ARMv4T"; case 3: return "ARMv5";
    case 4: return "ARMv5T"; case 5: return "ARMv5TE"; case 6: return "ARMv5TEJ";
    case 7: return "ARMv6"; default: return "?";
    }
}

/* one half of the cache type register */
static void cache_desc(char *b, unsigned f)
{
    unsigned size = (f >> 6) & 15, assoc = (f >> 3) & 7, m = (f >> 2) & 1, len = f & 3;
    unsigned kb = (m ? 768 : 512) << size >> 10;
    sprintf(b, "%uK %u-way %uB lines", kb, m ? 3u << assoc >> 1 : 1u << assoc, 8u << len);
}

/* =========================================================== benchmarks */

struct results {
    int have_ci, have_arm;
    uint32_t ci, di, pi, seek_us, kbs, mhz, turbo;
    struct armbench a;
};
static struct results R;
static uint8_t *bigbuf;

struct ref { const char *name; uint16_t ci_x10; };
static const struct ref refs[] = {
    { "COMPAQ Deskpro 486/25", 552 },
    { "COMPAQ Deskpro 386/25", 274 },
    { "IBM PS/2 Model 80 (386/16)", 178 },
    { "IBM PC/AT (286, 8 MHz)", 77 },
    { "IBM PC/XT (8088, 4.77 MHz)", 10 },
};

static void run_system_benchmarks(void)
{
    timer_start();
    R.ci = bench_ci_x10();
    R.di = bench_di_x10(bigbuf, &R.seek_us, &R.kbs);
    timer_stop();
    R.pi = (R.ci * 3 + R.di) / 4;
    R.mhz = armdos_inb(0xF1);
    R.turbo = armdos_inb(0xF2);
    R.have_ci = 1;
}

static void run_arm_benchmarks(void)
{
    timer_start();
    bench_arm(&R.a, bigbuf);
    timer_stop();
    R.mhz = armdos_inb(0xF1);
    R.turbo = armdos_inb(0xF2);
    R.have_arm = 1;
}

/* ================================================================ pages */

#define WX 2
#define WY 2
#define WW 76
#define WH 21
#define NPAGES 5
static const char *const page_title[NPAGES] = {
    "System Summary", "Processor: ARM926EJ-S", "Memory Summary", "Benchmarks", "ARM Architecture Tests"
};

static void frame(int page)
{
    fill(0, 0, 80, 25, 0xB0, 0x19);             /* desktop: light dots on blue */
    fill(0, 0, 80, 1, ' ', A_BAR);
    put(1, 0, "System Information", 0x30);
    put(20, 0, "\xB3", 0x30);
    for (int i = 0, x = 22; i < NPAGES; i++) {
        char b[32];
        static const char *const shortn[NPAGES] = { "Summary", "CPU", "Memory", "Benchmarks", "ARM Tests" };
        snprintf(b, sizeof b, " %d %s ", i + 1, shortn[i]);
        put(x, 0, b, i == page ? 0x0F : 0x30);
        if (i != page) V[x + 1] = (V[x + 1] & 0xFF) | 0x3400;   /* red digit hotkey */
        x += strlen(b);
    }
    fill(0, 24, 80, 1, ' ', A_BAR);
    put(1, 24, "PgDn", 0x34); put(6, 24, "Next", 0x30);
    put(12, 24, "PgUp", 0x34); put(17, 24, "Previous", 0x30);
    put(27, 24, "1-5", 0x34); put(31, 24, "Page", 0x30);
    put(37, 24, "R", 0x34); put(39, 24, "Re-test", 0x30);
    put(48, 24, "Esc", 0x34); put(52, 24, "Quit", 0x30);
    put(66, 24, "\xB3 ARMINFO 1.00", 0x30);
    char t[48];
    snprintf(t, sizeof t, "%s", page_title[page]);
    box(WX, WY, WW, WH, A_BOX, t);
}

/* the "please wait" window */
static void busy(const char *what)
{
    int w = strlen(what) + 10;
    int x = (80 - w) / 2;
    box(x, 10, w, 5, 0x4F, "Testing");
    put(x + 5, 12, what, 0x4F);
    put(x + 5 + strlen(what), 12, "...", 0x4E);
}

static void lv(int x, int y, const char *label, const char *val)
{
    put(x, y, label, A_LBL);
    put(x + strlen(label) + 1, y, val, A_VAL);
}

/* right-aligned label, value after it (SI style) */
static void lr(int x, int y, int w, const char *label, const char *val)
{
    int n = strlen(label);
    put(x + w - n, y, label, A_LBL);
    put(x + w + 1, y, val, A_VAL);
}

static void page_summary(void)
{
    char b[100], c1[16], c2[16];
    int x = WX + 3, y = WY + 1, x2 = WX + 42;
    put(x - 1, y++, "Computer", A_TTL);
    lr(x, y, 16, "Computer Name:", "ARM/AT");
    lr(x2 - 6, y++, 13, "Made by:", I.bios_maker);
    snprintf(b, sizeof b, "Version %s, %s", I.bios_ver, I.bios_date_long);
    lr(x, y++, 16, "Built-in BIOS:", b);
    snprintf(b, sizeof b, "ARM926EJ-S (%s)", arch_name((I.id >> 16) & 15));
    lr(x, y, 16, "Main Processor:", b);
    snprintf(b, sizeof b, "%lu MHz%s", (unsigned long)I.mhz, I.turbo ? ", Turbo" : ", Turbo off");
    lr(x2 - 6, y++, 13, "Clock:", b);
    lr(x, y, 16, "Math Processor:", "None (soft-float)");
    snprintf(b, sizeof b, "ISA, model %02Xh/%02Xh", I.model, I.submodel);
    lr(x2 - 6, y++, 13, "Bus Type:", b);
    snprintf(b, sizeof b, "ARM-DOS %u.%02u", I.dos_major, I.dos_minor);
    lr(x, y++, 16, "Operating System:", b);
    y++;
    put(x - 1, y, "Disks", A_TTL);
    put(x2 - 1, y++, "Memory", A_TTL);
    if (I.hds) snprintf(b, sizeof b, "%uM (%u cyl, %u hds)", I.hd_mb, I.hd_c, I.hd_h);
    else strcpy(b, "None");
    lr(x, y, 16, "Hard Disks:", b);
    snprintf(b, sizeof b, "%uK", I.conv_kb);
    lr(x2, y++, 12, "DOS Memory:", b);
    if (I.floppies) snprintf(b, sizeof b, "%s%s", fd_name(I.fd_type), I.floppies > 1 ? ", 2 drives" : "");
    else strcpy(b, "None");
    lr(x, y, 16, "Floppy Disks:", b);
    snprintf(b, sizeof b, "%sK", commas(c1, I.ext_total));
    lr(x2, y++, 12, "Extended:", b);
    {
        char *p = b;
        p += sprintf(p, "%d, ", I.ndrives);
        for (int i = 0; i < I.ndrives; i++) p += sprintf(p, "%c:%s", I.drives[i], i + 1 < I.ndrives ? " " : "");
    }
    lr(x, y, 16, "Logical Drives:", b);
    if (I.xms) snprintf(b, sizeof b, "%u.%02x, %sK free", I.xms_ver >> 8, I.xms_ver & 0xFF, commas(c2, I.xms_free));
    else strcpy(b, "no driver");
    lr(x2, y++, 12, "XMS:", b);
    y++;
    put(x - 1, y++, "Other Info", A_TTL);
    lr(x, y++, 16, "Video Adapter:", video_name(I.vid_dcc));
    lr(x, y++, 16, "Video Mode:", mode_name(I.vid_mode));
    snprintf(b, sizeof b, "%d", I.ncom);
    lr(x, y, 16, "Serial Ports:", b);
    snprintf(b, sizeof b, "%d", I.nlpt);
    lr(x2, y++, 15, "Parallel Ports:", b);
    lr(x, y, 16, "Keyboard:", I.kbd101 ? "101-Key Enhanced" : "84-Key");
    if (I.mouse_drv) snprintf(b, sizeof b, "Driver, %d buttons", I.mouse_buttons);
    else snprintf(b, sizeof b, "%s", I.mouse_port ? "PS/2, no driver" : "None");
    lr(x2, y++, 15, "Mouse:", b);
}

static void check_line(int x, int y, int ok, const char *what, const char *detail)
{
    put(x, y, ok ? "\xFB" : "-", ok ? A_OK : A_NO);
    put(x + 2, y, what, A_VAL);
    put(x + 30, y, detail, A_DIM);
}

static void page_cpu(void)
{
    char b[100];
    int x = WX + 3, y = WY + 1;
    uint32_t id = I.id;
    put(x - 1, y++, "Identification (CP15 register c0, read with MRC p15,0,r0,c0,c0,0)", A_TTL);
    snprintf(b, sizeof b, "%08lX", (unsigned long)id);
    lv(x, y, "Main ID:", b);
    snprintf(b, sizeof b, "%02lXh '%c' = ARM Ltd.", (unsigned long)(id >> 24), (char)(id >> 24));
    lv(x + 26, y++, "Implementer:", b);
    snprintf(b, sizeof b, "%03lXh = ARM926", (unsigned long)((id >> 4) & 0xFFF));
    lv(x, y, "   Part:", b);
    snprintf(b, sizeof b, "%lu = %s", (unsigned long)((id >> 16) & 15), arch_name((id >> 16) & 15));
    lv(x + 26, y, "Architecture:", b);
    snprintf(b, sizeof b, "r%lup%lu", (unsigned long)((id >> 20) & 15), (unsigned long)(id & 15));
    lv(x + 55, y++, "Revision:", b);
    char ic[28], dc[28];
    cache_desc(ic, I.cachetype & 0xFFF);
    cache_desc(dc, (I.cachetype >> 12) & 0xFFF);
    snprintf(b, sizeof b, "%08lX  I: %s  D: %s", (unsigned long)I.cachetype, ic, dc);
    lv(x, y++, "Cache type:", b);
    snprintf(b, sizeof b, "%08lX  MMU %s, vectors at %s, %s-endian", (unsigned long)I.control,
             I.control & 1 ? "on" : "off", I.control & 0x2000 ? "FFFF0000h" : "00000000h",
             I.control & 0x80 ? "big" : "little");
    lv(x, y++, "Control c1:", b);

    put(x - 1, y++, "Program Status Register (MRS r0,CPSR)", A_TTL);
    uint32_t p = I.cpsr;
    snprintf(b, sizeof b, "%08lX", (unsigned long)p);
    lv(x, y, "CPSR =", b);
    put(x + 17, y, "N Z C V Q", A_LBL);
    put(x + 29, y, "I F T  Mode", A_LBL);
    y++;
    snprintf(b, sizeof b, "%lu %lu %lu %lu %lu", (unsigned long)(p >> 31) & 1, (unsigned long)(p >> 30) & 1,
             (unsigned long)(p >> 29) & 1, (unsigned long)(p >> 28) & 1, (unsigned long)(p >> 27) & 1);
    put(x + 17, y, b, A_VAL);
    char mb[8];
    for (int i = 0; i < 5; i++) mb[i] = (p >> (4 - i)) & 1 ? '1' : '0';
    mb[5] = 0;
    snprintf(b, sizeof b, "%lu %lu %lu  %s = %s", (unsigned long)(p >> 7) & 1, (unsigned long)(p >> 6) & 1,
             (unsigned long)(p >> 5) & 1, mb, cpu_mode_name(p));
    put(x + 29, y++, b, A_VAL);
    put(x, y++, "Programs run in SYS mode: privileged and unprotected, like real mode.", A_DIM);

    y++;
    put(x - 1, y++, "Instruction set (each one executed just now)", A_TTL);
    int yy = y;
    uint32_t q = 0, sat = probe_qadd(0x7FFFFFFF, 1, &q);
    snprintf(b, sizeof b, "3*5+1 = %d in 16-bit code", probe_thumb(5));
    check_line(x, y++, probe_thumb(5) == 16, "Thumb (BLX interworking)", b);
    snprintf(b, sizeof b, "7FFFFFFF+1 = %08lX, Q=%lu", (unsigned long)sat, (unsigned long)q);
    check_line(x, y++, sat == 0x7FFFFFFF && q, "DSP: QADD saturation", b);
    snprintf(b, sizeof b, "7FFF*7FFF = %08lX", (unsigned long)probe_smulbb(0x7FFF, 0x7FFF));
    check_line(x, y++, probe_smulbb(0x7FFF, 0x7FFF) == 0x3FFF0001, "DSP: SMULBB 16x16", b);
    snprintf(b, sizeof b, "CLZ 00010000 = %lu", (unsigned long)probe_clz(0x10000));
    check_line(x, y++, probe_clz(0x10000) == 15, "CLZ count leading zeros", b);
    y = yy;
    x += 0;
    static uint64_t dw = 0x0123456789ABCDEFull;
    uint64_t l = probe_ldrd(&dw);
    uint32_t hi = probe_umull_hi(0xFFFFFFFF, 0xFFFFFFFF);
    static uint32_t sw = 0x1988;
    uint32_t old = probe_swp(&sw, 0x0400);
    /* right half as a compact list */
    int x2 = WX + 3;
    y = yy + 4;
    snprintf(b, sizeof b, "UMULL hi(FFFFFFFF^2) = %08lX", (unsigned long)hi);
    check_line(x2, y++, hi == 0xFFFFFFFE, "Long multiply (UMULL)", b);
    snprintf(b, sizeof b, "LDRD = %08lX:%08lX", (unsigned long)(l >> 32), (unsigned long)l);
    check_line(x2, y++, l == dw, "LDRD/STRD doubleword", b);
    snprintf(b, sizeof b, "old %04lX, new %04lX", (unsigned long)old, (unsigned long)sw);
    check_line(x2, y++, old == 0x1988 && sw == 0x400, "SWP atomic swap", b);
    check_line(x2, y++, 0, "Jazelle, VFP", "not fitted (BXJ acts as BX)");
}

static void mrow(int x, int y, const char *what, unsigned kb, uint32_t from, uint32_t to, const char *desc)
{
    char b[80], c1[16];
    put(x + 12 - strlen(what), y, what, A_LBL);
    if (kb) { sprintf(b, "%sK", commas(c1, kb)); put(x + 21 - strlen(b), y, b, A_VAL); }
    sprintf(b, "%08lX-%08lX", (unsigned long)from, (unsigned long)to);
    put(x + 23, y, b, A_VAL);
    put(x + 42, y, desc, A_DIM);
}

static void page_memory(void)
{
    char b[80], c1[16];
    int x = WX + 3, y = WY + 1;
    put(x - 1, y++, "Memory map", A_TTL);
    unsigned used = I.conv_kb * 1024 - I.dos_free;
    snprintf(b, sizeof b, "%sK used by DOS & programs", commas(c1, (used + 512) / 1024));
    mrow(x, y++, "Conventional", I.conv_kb, 0x00000000, 0x0009FFFF, b);
    mrow(x, y++, "Vector table", 1, 0x00000000, 0x000003FF, "256 vectors x 4 bytes");
    mrow(x, y++, "BIOS data", 0, 0x00000400, 0x000004FF, "as on every PC");
    mrow(x, y++, "Display", 128, 0x000A0000, 0x000BFFFF, "VGA graphics and text");
    mrow(x, y++, "BIOS RAM", 64, 0x00100000, 0x0010FFFF, "BIOS data and stacks");
    mrow(x, y++, "Extended", I.ext_total - 64, 0x00110000, 0x00FFFFFF, I.xms ? "HIMEM.SYS (XMS)" : "INT 15h, AH=88h");
    mrow(x, y++, "I/O ports", 64, 0x10000000, 0x1000FFFF, "ISA port p at 10000000h+p");
    mrow(x, y++, "ROM BIOS", 1024, 0xFFF00000, 0xFFFFFFFF, "ARM vectors at FFFF0000h");
    snprintf(b, sizeof b, "%s bytes", commas(c1, I.dos_free));
    lv(x, y, "Largest free DOS block:", b);
    if (I.xms) snprintf(b, sizeof b, "%sK free", commas(c1, I.xms_free));
    else snprintf(b, sizeof b, "no driver");
    lv(x + 40, y++, "XMS:", b);
    y++;
    put(x - 1, y++, "Memory Control Blocks (INT 21h AH=52h)", A_TTL);
    put(x, y++, "Address   Bytes  Owner  Name      Hooked vectors", A_LBL);
    /* walk the MCB chain */
    struct armregs r = { 0 };
    r.r0 = 0x5200; int21(&r);
    uint8_t *lol = (uint8_t *)r.r1;
    unsigned seg = lol[-2] | (lol[-1] << 8);
    int rows = 0;
    const int maxrows = WY + WH - 2 - y;
    for (int guard = 0; guard < 200; guard++) {
        struct mcb *m = (struct mcb *)(seg << 4);
        if (m->type != 'M' && m->type != 'Z') break;
        uint32_t start = (seg + 1) << 4, bytes = m->size * 16;
        /* only show program blocks (owner == itself) and free space */
        int prog = m->owner == seg + 1;
        if ((prog || m->owner == 0 || m->owner == 8) && rows < maxrows) {
            char name[9] = "";
            if (prog) { memcpy(name, m->name, 8); name[8] = 0; for (int i = 0; i < 8; i++) if (name[i] < ' ') name[i] = 0; }
            else strcpy(name, m->owner ? "(DOS)" : "(free)");
            char hv[40] = "";
            if (prog) {
                char *p = hv;
                for (int n = 0; n < 256 && p < hv + 34; n++) {
                    uint32_t a = ((uint32_t *)0)[n];
                    if (a >= start && a < start + bytes) p += sprintf(p, "%02X ", n);
                }
            }
            snprintf(b, sizeof b, "%08lX %7s  %04X   %-8s  %s", (unsigned long)start, commas(c1, bytes), m->owner, name, hv);
            put(x, y++, b, prog ? A_VAL : A_DIM);
            rows++;
        }
        if (m->type == 'Z') break;
        seg += m->size + 1;
    }
}

static void bar(int x, int y, int maxw, uint32_t v, uint32_t scale, uint8_t a)
{
    uint32_t halves = scale ? (uint32_t)((uint64_t)v * maxw * 2 / scale) : 0;
    if (v && !halves) halves = 1;
    int over = halves > (uint32_t)maxw * 2;
    if (over) halves = maxw * 2;
    int i;
    for (i = 0; i < (int)halves / 2; i++) V[y * 80 + x + i] = 0xDB | (a << 8);
    if (halves & 1) V[y * 80 + x + i++] = 0xDD | (a << 8);
    if (over) V[y * 80 + x + i - 1] = 0x10 | (a << 8);
}

static void fmt_x10(char *b, uint32_t v) { sprintf(b, "%lu.%lu", (unsigned long)v / 10, (unsigned long)v % 10); }

static void page_bench(void)
{
    char b[80], n[24], c1[16];
    int x = WX + 3, y = WY + 1;
    if (!R.have_ci) {
        busy("Measuring the CPU and the hard disk");
        run_system_benchmarks();
        frame(3);
    }
    put(x - 1, y++, "CPU Speed", A_TTL);
    fmt_x10(n, R.ci);
    snprintf(b, sizeof b, "Computing Index (CI), relative to IBM/XT: ");
    put(x, y, b, A_LBL);
    put(x + strlen(b), y++, n, A_VAL);
    snprintf(b, sizeof b, "(ARM926EJ-S at %lu MHz%s)", (unsigned long)R.mhz, R.turbo ? ", Turbo" : ", Turbo off");
    put(x, y++, b, A_DIM);
    uint32_t scale = R.ci > 600 ? R.ci : 600;
    const int bw = 36, bx = x + 28;
    put(x, y, "This computer", A_TTL);
    bar(bx, y, bw, R.ci, scale, A_BAR1);
    fmt_x10(n, R.ci);
    put(bx + bw + 2, y++, n, A_TTL);
    for (unsigned i = 0; i < sizeof refs / sizeof refs[0]; i++) {
        put(x, y, refs[i].name, A_LBL);
        bar(bx, y, bw, refs[i].ci_x10, scale, A_BAR2);
        fmt_x10(n, refs[i].ci_x10);
        put(bx + bw + 2, y++, n, A_VAL);
    }
    put(bx, y, "\xC0", A_DIM);
    for (int i = 1; i < bw; i++) put(bx + i, y, (i % 6) ? "\xC4" : "\xC1", A_DIM);
    y++;
    y++;
    put(x - 1, y++, "Disk Speed (INT 13h, drive 80h)", A_TTL);
    if (R.di) {
        fmt_x10(n, R.di);
        put(x, y, "Disk Index (DI), relative to IBM/XT:", A_LBL);
        put(x + 37, y++, n, A_VAL);
        snprintf(b, sizeof b, "Average seek + read:  %lu.%02lu ms     Data transfer rate:  %s KB/s",
                 (unsigned long)R.seek_us / 1000, (unsigned long)(R.seek_us % 1000) / 10, commas(c1, R.kbs));
        put(x, y++, b, A_VAL);
    } else put(x, y++, "Disk Index (DI): no hard disk to test", A_LBL);
    y++;
    fmt_x10(n, R.pi);
    put(x - 1, y, "Performance Index (PI), relative to IBM/XT:", A_TTL);
    put(x + 43, y++, n, A_VAL);
    put(x, y++, "Try the Turbo button, then press R to measure again.", A_DIM);
}

static void mbs(char *b, uint32_t kbs) { sprintf(b, "%lu.%02lu MB/s", (unsigned long)kbs / 1024, (unsigned long)(kbs % 1024) * 100 / 1024); }
static void mps(char *b, uint32_t x100) { sprintf(b, "%lu.%02lu M/s", (unsigned long)x100 / 100, (unsigned long)x100 % 100); }

static void test_row(int x, int y, const char *name, const char *val, uint32_t v, uint32_t best, uint8_t a)
{
    put(x, y, name, A_LBL);
    putn(x + 33, y, "", 13, A_VAL);
    put(x + 46 - strlen(val), y, val, A_VAL);
    bar(x + 48, y, 22, v, best, a);
}

static void page_arm(void)
{
    char b[80], c1[16];
    int x = WX + 3, y = WY + 1;
    if (!R.have_arm) {
        busy("Running the ARM architecture tests");
        run_arm_benchmarks();
        frame(4);
    }
    struct armbench *a = &R.a;
    uint32_t best;

    put(x - 1, y++, "Barrel shifter test", A_TTL);
    put(x + 21, y - 1, "y = x*5 + x/8, 1,024 words", A_DIM);
    snprintf(b, sizeof b, "%lu MHz%s", (unsigned long)R.mhz, R.turbo ? " Turbo" : "");
    put(x + 70 - strlen(b), y - 1, b, A_TTL);
    best = a->bs_bar > a->bs_c ? a->bs_bar : a->bs_c;
    if (a->bs_sep > best) best = a->bs_sep;
    mps(b, a->bs_sep); test_row(x, y++, "MOV/ADD, shifts apart (8086 way)", b, a->bs_sep, best, A_BAR2);
    mps(b, a->bs_bar); test_row(x, y++, "ADD r12,r3,r3,LSL #2 (asm)", b, a->bs_bar, best, A_BAR1);
    mps(b, a->bs_c);   test_row(x, y++, "Compiled C (gcc -O2)", b, a->bs_c, best, A_BAR2);
    y++;
    put(x - 1, y++, "Conditional execution test", A_TTL);
    put(x + 28, y - 1, "Euclid's GCD by subtraction", A_DIM);
    best = a->gcd_c > a->gcd_cc ? a->gcd_c : a->gcd_cc;
    if (a->gcd_b > best) best = a->gcd_b;
    sprintf(b, "%s/s", commas(c1, a->gcd_b));  test_row(x, y++, "CMP / BEQ / BLT / B (8086 way)", b, a->gcd_b, best, A_BAR2);
    sprintf(b, "%s/s", commas(c1, a->gcd_c));  test_row(x, y++, "CMP / SUBGT / SUBLT / BNE (asm)", b, a->gcd_c, best, A_BAR1);
    sprintf(b, "%s/s", commas(c1, a->gcd_cc)); test_row(x, y++, "Compiled C (gcc -O2)", b, a->gcd_cc, best, A_BAR2);
    y++;
    put(x - 1, y++, "LDM/STM block move test", A_TTL);
    put(x + 25, y - 1, "16 KB blocks", A_DIM);
    best = a->mv_m > a->mv_c ? a->mv_m : a->mv_c;
    if (a->mv_b > best) best = a->mv_b;
    mbs(b, a->mv_b); test_row(x, y++, "LDRB/STRB loop (REP MOVSB way)", b, a->mv_b, best, A_BAR2);
    mbs(b, a->mv_c); test_row(x, y++, "C library memcpy", b, a->mv_c, best, A_BAR2);
    mbs(b, a->mv_m); test_row(x, y++, "LDMIA/STMIA r3-r10 (asm)", b, a->mv_m, best, A_BAR1);
    y++;
    hsep(WX, y++, WW, A_BOX);
    static const char *const code[4] = {
        "loop: CMP   r0, r1", "      SUBGT r0, r0, r1   ; a>b: a-=b",
        "      SUBLT r1, r1, r0   ; a<b: b-=a", "      BNE   loop",
    };
    static const char *const why[4] = {
        "An 8086 must jump around", "every decision; an ARM just",
        "skips the instructions whose", "condition fails.",
    };
    for (int i = 0; i < 4; i++, y++) { put(x, y, code[i], 0x1A); put(x + 42, y, why[i], A_DIM); }
    if (!a->bs_ok || !a->gcd_ok) put(x, y, "WARNING: results differ between versions!", 0x1C);
}

static void draw(int page)
{
    frame(page);
    switch (page) {
    case 0: page_summary(); break;
    case 1: page_cpu(); break;
    case 2: page_memory(); break;
    case 3: page_bench(); break;
    case 4: page_arm(); break;
    }
}

static void fullscreen(int page)
{
    screen_begin();
    for (;;) {
        draw(page);
        int k = getkey();
        int sc = k >> 8, ch = k & 0xFF;
        if (ch == 27 || ch == 'q' || ch == 'Q') break;
        if (sc == 0x51 || ch == 13 || ch == ' ' || sc == 0x4D) page = (page + 1) % NPAGES;       /* PgDn, Right */
        else if (sc == 0x49 || sc == 0x4B) page = (page + NPAGES - 1) % NPAGES;               /* PgUp, Left */
        else if (ch >= '1' && ch <= '0' + NPAGES) page = ch - '1';
        else if (sc == 0x47) page = 0;
        else if (sc == 0x4F) page = NPAGES - 1;
        else if (ch == 'r' || ch == 'R') {
            if (page == 3) R.have_ci = 0;
            if (page == 4) R.have_arm = 0;
        }
    }
    screen_end();
}

/* ======================================================= teletype report */

static void report(int bench)
{
    char c1[16], n[16];
    printf("ARMINFO-System Information, Version 1.00, (C) Copr 1989, Europa Micro Systems\n\n");
    printf("         Computer Name: ARM/AT (%s)\n", I.bios_maker);
    printf("   Built-in BIOS dated: %s\n", I.bios_date_long);
    printf("      Operating System: ARM-DOS %u.%02u\n", I.dos_major, I.dos_minor);
    printf("        Main Processor: ARM926EJ-S, %lu MHz      Serial Ports: %d\n", (unsigned long)I.mhz, I.ncom);
    printf("          Co-Processor: None                  Parallel Ports: %d\n", I.nlpt);
    printf("         CPU ID (CP15): %08lX, %s, SYS mode\n", (unsigned long)I.id, arch_name((I.id >> 16) & 15));
    printf(" Video Display Adapter: %s\n", video_name(I.vid_dcc));
    printf("    Current Video Mode: %s\n", mode_name(I.vid_mode));
    printf(" Available Disk Drives: %d, ", I.ndrives);
    for (int i = 0; i < I.ndrives; i++) printf("%c:%s", I.drives[i], i + 1 < I.ndrives ? " " : "\n");
    printf("\nDOS reports %u K-bytes of memory:\n", I.conv_kb);
    printf("%6u K-bytes used by DOS and resident programs\n", (I.conv_kb * 1024 - I.dos_free + 512) / 1024);
    printf("%6u K-bytes available for application programs\n", I.dos_free / 1024);
    printf("A search for active memory finds:\n");
    printf("%6u K-bytes main memory     (at hex 00000000-0009FFFF)\n", I.conv_kb);
    printf("%6u K-bytes display memory  (at hex 000A0000-000BFFFF)\n", 128);
    printf("%6s K-bytes extended memory (at hex 00100000-00FFFFFF)\n", commas(c1, I.ext_total));
    if (I.xms) printf("        XMS %u.%02x driver: %sK free\n", I.xms_ver >> 8, I.xms_ver & 0xFF, commas(c1, I.xms_free));
    printf("BIOS signature found at hex address %08lX\n", (unsigned long)I.bios_copr_addr);
    if (bench) {
        run_system_benchmarks();
        printf("\n");
        fmt_x10(n, R.ci); printf("Computing Index (CI), relative to IBM/XT: %s\n", n);
        if (R.di) { fmt_x10(n, R.di); printf("Disk Index (DI), relative to IBM/XT: %s\n", n); }
        else printf("Disk Index (DI): not computed, no hard disk\n");
        fmt_x10(n, R.pi); printf("Performance Index (PI), relative to IBM/XT: %s\n", n);
    }
}

int main(int argc, char **argv)
{
    int tty = 0, bench = 1, page = 0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (a[0] == '/' || a[0] == '-') {
            switch (a[1] | 0x20) {
            case 't': tty = 1; break;
            case 'n': bench = 0; break;
            case 'b': page = 3; break;
            case 'a': page = 4; break;
            case '?': case 'h':
                printf("ARMINFO - System Information for the ARM/AT\n\n"
                       "ARMINFO [/B | /A]      full-screen report (/B benchmarks, /A ARM tests first)\n"
                       "ARMINFO /T [/N]        teletype report to standard output (/N no benchmarks)\n");
                return 0;
            default:
                printf("Invalid switch - %s\n", a);
                return 1;
            }
        }
    }
    bigbuf = malloc(32768 + 64);
    if (!bigbuf) { printf("Insufficient memory\n"); return 8; }
    bigbuf = (uint8_t *)(((uint32_t)bigbuf + 31) & ~31u);
    gather();
    if (tty) report(bench);
    else fullscreen(page);
    return 0;
}
