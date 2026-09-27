/*
 * JOYTEST.EXE - joystick test and calibration for the IBM game control adapter (201h).
 *
 * A full-screen text utility in the manner of the joystick calibration programs that
 * came with game cards and sticks around 1990: the raw axis counts of both joysticks
 * as numbers and bars, the button lamps, a crosshair in a box, what the BIOS says
 * (INT 11h bit 12, INT 15h AH=84h), and a calibration routine (centre, upper left,
 * lower right, each confirmed with button 1) that writes JOYSTICK.CFG.
 *
 *   JOYTEST          interactive
 *   JOYTEST /C       start with the calibration
 *   JOYTEST /R       report the readings once on standard output and exit
 *   JOYTEST /?       help
 *
 * The axes are read the classic way: fire the four one-shots with a write to 201h,
 * then count polls until each axis bit drops, interrupts off. A count that reaches
 * the limit means nothing is plugged into that axis.
 */
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <conio.h>
#include <dos.h>
#include <armdos.h>

#define JOYPORT   0x201
#define LIMIT     4000          /* polls (1 us each on the ISA bus): an axis past this is open */
#define VERSION   "1.00"

/* ------------------------------------------------------------------ the port */
static unsigned raw[4];          /* last counts; LIMIT = not connected */
static unsigned btn;             /* bit n = button n pressed (A1 A2 B1 B2) */
static unsigned live = 15;       /* axes answering (bit per axis) */
static unsigned frame;

static void read_stick(void)
{
    unsigned c[4] = { 0, 0, 0, 0 }, n, v = 0;
    unsigned mask = (frame % 32 == 0) ? 15 : live;   /* look for a newly plugged stick now and then */
    if (!mask) mask = 15;
    _disable();
    /* a one-shot still timing ignores a fire (the 558 is not retriggerable): let them end */
    for (n = 0; n < LIMIT && (inp(JOYPORT) & mask); n++) ;
    outp(JOYPORT, 0);
    for (n = 0; n < LIMIT; n++) {
        v = inp(JOYPORT);
        if (!(v & mask)) break;
        if (v & 1) c[0]++;
        if (v & 2) c[1]++;
        if (v & 4) c[2]++;
        if (v & 8) c[3]++;
    }
    _enable();
    btn = (~inp(JOYPORT) >> 4) & 15;
    for (int i = 0; i < 4; i++) {
        if (!(mask & (1u << i))) { raw[i] = LIMIT; continue; }
        raw[i] = c[i];
        if (c[i] >= LIMIT - 1 || (n >= LIMIT && c[i] == n)) { raw[i] = LIMIT; live &= ~(1u << i); }
        else live |= 1u << i;
    }
}
#define CONNECTED(i) (raw[i] < LIMIT)
static void guess_cal(void);

/* ------------------------------------------------------------------ the BIOS */
static int bios_game;            /* INT 11h bit 12 */
static unsigned bios_ax[4], bios_sw;
static int bios_ok;
static void read_bios(void)
{
    union REGS r;
    memset(&r, 0, sizeof r);
    r.x.ax = 0x8400; r.x.dx = 1;
    int86(0x15, &r, &r);
    bios_ok = !r.x.cflag;
    bios_ax[0] = r.x.ax & 0xFFFF; bios_ax[1] = r.x.bx & 0xFFFF; bios_ax[2] = r.x.cx & 0xFFFF; bios_ax[3] = r.x.dx & 0xFFFF;
    memset(&r, 0, sizeof r);
    r.x.ax = 0x8400; r.x.dx = 0;
    int86(0x15, &r, &r);
    bios_sw = r.h.al & 0xF0;
}

/* ------------------------------------------------------------------ calibration */
struct cal { unsigned min, ctr, max; };
static struct cal cal[4];
static int calibrated, cfg_loaded;
static char cfgmsg[80];

static int guessed;               /* default ranges derived from a first centred reading */
static void default_cal(void)
{
    for (int i = 0; i < 4; i++) { cal[i].min = 20; cal[i].ctr = 480; cal[i].max = 940; }
    calibrated = 0; guessed = 0;
}
/* Not calibrated: take the first reading as the centre (like the games, which assume the
 * stick is centred when they start) and derive the ends from the 558's timing: 24.2 us at
 * 0 ohm, 574.2 us at the centre, 1124.2 us at 100 kOhm, so min = 4.2% and max = 195.8% of
 * the centre count, whatever the CPU and the polling loop. */
static void guess_cal(void)
{
    if (calibrated || guessed) return;
    for (int s = 0; s < 2; s++) {
        if (!CONNECTED(s * 2) || !CONNECTED(s * 2 + 1)) continue;
        for (int i = s * 2; i < s * 2 + 2; i++) {
            unsigned c = raw[i] ? raw[i] : 1;
            cal[i].ctr = c; cal[i].min = c * 42 / 1000; cal[i].max = c * 1958 / 1000;
        }
        if (s == 0) guessed = 1;
    }
}
static int load_cfg(void)
{
    FILE *f = fopen("JOYSTICK.CFG", "r");
    if (!f) return 0;
    char line[80], sect = 0;
    int got = 0;
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '[') { sect = (strncmp(line, "[JoystickB]", 11) == 0) ? 'B' : (strncmp(line, "[JoystickA]", 11) == 0) ? 'A' : 0; continue; }
        if (!sect || (line[0] != 'X' && line[0] != 'Y')) continue;
        int ax = (sect == 'B' ? 2 : 0) + (line[0] == 'Y');
        char *eq = strchr(line, '=');
        if (!eq) continue;
        unsigned v = (unsigned)atoi(eq + 1);
        if (!strncmp(line + 1, "Min", 3)) cal[ax].min = v, got++;
        else if (!strncmp(line + 1, "Center", 6)) cal[ax].ctr = v, got++;
        else if (!strncmp(line + 1, "Max", 3)) cal[ax].max = v, got++;
    }
    fclose(f);
    if (got >= 6) { calibrated = 1; return 1; }
    return 0;
}
static int save_cfg(void)
{
    FILE *f = fopen("JOYSTICK.CFG", "w");
    if (!f) return 0;
    fprintf(f, "; JOYSTICK.CFG - written by JOYTEST " VERSION "\n");
    fprintf(f, "; raw game port counts (201h), minimum / centre / maximum\n");
    for (int s = 0; s < 2; s++) {
        fprintf(f, "[Joystick%c]\n", 'A' + s);
        for (int a = 0; a < 2; a++) {
            struct cal *c = &cal[s * 2 + a];
            char x = a ? 'Y' : 'X';
            fprintf(f, "%cMin=%u\n%cCenter=%u\n%cMax=%u\n", x, c->min, x, c->ctr, x, c->max);
        }
    }
    int ok = !ferror(f);
    return fclose(f) == 0 && ok;
}

/* ------------------------------------------------------------------ the screen */
static volatile uint16_t *vram;
static int mono;
#define A(fg, bg) ((uint8_t)(((bg) << 4) | (fg)))
static uint8_t at_back, at_frame, at_title, at_text, at_hi, at_dim, at_bar, at_lampon, at_lampoff, at_bar2, at_key, at_box;

static void put(int r, int c, int ch, uint8_t a) { if (r >= 0 && r < 25 && c >= 0 && c < 80) vram[r * 80 + c] = (uint16_t)((a << 8) | (uint8_t)ch); }
static void puts_at(int r, int c, const char *s, uint8_t a) { while (*s) put(r, c++, (uint8_t)*s++, a); }
static void fill(int r, int c, int n, int ch, uint8_t a) { while (n-- > 0) put(r, c++, ch, a); }
static void printf_at(int r, int c, uint8_t a, const char *fmt, ...) __attribute__((format(printf, 4, 5)));
static void printf_at(int r, int c, uint8_t a, const char *fmt, ...)
{
    char b[96];
    va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    puts_at(r, c, b, a);
}
static void box(int r0, int c0, int r1, int c1, const char *title, uint8_t a, int dbl)
{
    int h = dbl ? 0xCD : 0xC4, v = dbl ? 0xBA : 0xB3;
    put(r0, c0, dbl ? 0xC9 : 0xDA, a); put(r0, c1, dbl ? 0xBB : 0xBF, a);
    put(r1, c0, dbl ? 0xC8 : 0xC0, a); put(r1, c1, dbl ? 0xBC : 0xD9, a);
    fill(r0, c0 + 1, c1 - c0 - 1, h, a); fill(r1, c0 + 1, c1 - c0 - 1, h, a);
    for (int r = r0 + 1; r < r1; r++) { put(r, c0, v, a); put(r, c1, v, a); }
    if (title) { put(r0, c0 + 2, ' ', a); puts_at(r0, c0 + 3, title, at_title); put(r0, c0 + 3 + (int)strlen(title), ' ', a); }
}

static void colours(void)
{
    union REGS r; memset(&r, 0, sizeof r);
    r.h.ah = 0x0F; int86(0x10, &r, &r);
    int mode = r.h.al & 0x7F;
    if (mode != 3 && mode != 7 && mode != 2) { memset(&r, 0, sizeof r); r.x.ax = 0x0003; int86(0x10, &r, &r); mode = 3; }
    mono = (mode == 7);
    vram = ARMDOS_TEXT_VRAM;
    if (mono) {
        at_back = 0x07; at_frame = 0x07; at_title = 0x0F; at_text = 0x07; at_hi = 0x0F; at_dim = 0x07;
        at_bar = 0x0F; at_bar2 = 0x07; at_lampon = 0x70; at_lampoff = 0x07; at_key = 0x70; at_box = 0x70;
    } else {
        at_back = A(7, 1); at_frame = A(11, 1); at_title = A(14, 1); at_text = A(7, 1); at_hi = A(15, 1); at_dim = A(8, 1);
        at_bar = A(10, 1); at_bar2 = A(8, 1); at_lampon = A(12, 1); at_lampoff = A(8, 1); at_key = A(0, 3); at_box = A(15, 4);
    }
}
static void cursor(int on)
{
    union REGS r; memset(&r, 0, sizeof r);
    r.h.ah = 1; r.x.cx = on ? 0x0607 : 0x2000;
    if (on && mono) r.x.cx = 0x0B0C;
    int86(0x10, &r, &r);
}

/* the crosshair area */
#define XB0 42
#define XB1 77
#define YB0 3
#define YB1 19
static int cross_c = -1, cross_r = -1;

static int scale(unsigned v, const struct cal *c, int lo, int hi)
{
    /* piecewise: min..centre -> lo..mid, centre..max -> mid..hi */
    int mid = (lo + hi) / 2;
    long p;
    if (v <= c->ctr) {
        if (c->ctr <= c->min) return mid;
        p = mid - (long)(c->ctr - (v < c->min ? c->min : v)) * (mid - lo) / (long)(c->ctr - c->min);
    } else {
        if (c->max <= c->ctr) return mid;
        p = mid + (long)((v > c->max ? c->max : v) - c->ctr) * (hi - mid) / (long)(c->max - c->ctr);
    }
    if (p < lo) p = lo;
    if (p > hi) p = hi;
    return (int)p;
}

static void draw_static(void)
{
    for (int i = 0; i < 80 * 25; i++) vram[i] = (uint16_t)((at_back << 8) | ' ');
    fill(0, 0, 80, ' ', at_key);
    puts_at(0, 1, "JOYTEST " VERSION "  Joystick Test and Calibration", at_key);
    puts_at(0, 58, "Europa Micro Systems", at_key);
    box(2, 1, 11, 39, "Joystick A", at_frame, 0);
    box(12, 1, 19, 39, "Joystick B", at_frame, 0);
    box(2, XB0 - 1, YB1 + 1, XB1 + 1, "Position", at_frame, 0);
    puts_at(3, 3, "X axis", at_text); puts_at(5, 3, "Y axis", at_text);
    puts_at(13, 3, "X axis", at_text); puts_at(15, 3, "Y axis", at_text);
    puts_at(8, 3, "Button 1", at_text); puts_at(8, 21, "Button 2", at_text);
    puts_at(17, 3, "Button 3", at_text); puts_at(17, 21, "Button 4", at_text);
    puts_at(9, 3, "(B1 of stick B = button 3)", at_dim);
    fill(24, 0, 80, ' ', at_key);
    puts_at(24, 1, "C", at_key); puts_at(24, 3, "Calibrate", at_key);
    puts_at(24, 15, "S", at_key); puts_at(24, 17, "Save JOYSTICK.CFG", at_key);
    puts_at(24, 37, "D", at_key); puts_at(24, 39, "Defaults", at_key);
    puts_at(24, 50, "Esc", at_key); puts_at(24, 54, "Exit", at_key);
}

static void axis_row(int r, int i)
{
    const int bars = 22;
    if (!CONNECTED(i)) {
        printf_at(r, 10, at_dim, " ----  ");
        fill(r, 17, bars, 0xB0, at_bar2);
        puts_at(r + 1, 10, "not connected           ", at_dim);
        return;
    }
    printf_at(r, 10, at_hi, " %4u  ", raw[i]);
    int n = scale(raw[i], &cal[i], 0, bars);
    for (int k = 0; k < bars; k++) put(r, 17 + k, k < n ? 0xDB : 0xB0, k < n ? at_bar : at_bar2);
    printf_at(r + 1, 10, at_dim, "cal %4u %4u %4u    ", cal[i].min, cal[i].ctr, cal[i].max);
}
static void lamp(int r, int c, int on)
{
    put(r, c, '[', at_text); put(r, c + 1, on ? 0xFE : ' ', on ? at_lampon : at_lampoff); put(r, c + 2, ']', at_text);
    puts_at(r, c + 4, on ? "ON " : "off", on ? at_hi : at_dim);
}

static void draw_cross(void)
{
    /* the grid */
    int mc = (XB0 + XB1) / 2, mr = (YB0 + YB1) / 2;
    for (int r = YB0; r <= YB1; r++) for (int c = XB0; c <= XB1; c++) {
        int ch = ' ';
        if (r == mr && c == mc) ch = 0xC5;
        else if (r == mr) ch = 0xFA;
        else if (c == mc) ch = 0xFA;
        put(r, c, ch, at_dim);
    }
    if (!CONNECTED(0) || !CONNECTED(1)) {
        puts_at(mr - 1, XB0 + 5, " No joystick in port A ", at_hi);
        puts_at(mr + 1, XB0 + 3, " Plug it in and move it ", at_text);
        cross_c = cross_r = -1;
        return;
    }
    int c = scale(raw[0], &cal[0], XB0, XB1), r = scale(raw[1], &cal[1], YB0, YB1);
    for (int k = XB0; k <= XB1; k++) if (k != c) put(r, k, 0xC4, at_frame);
    for (int k = YB0; k <= YB1; k++) if (k != r) put(k, c, 0xB3, at_frame);
    put(r, c, 0x0F, (uint8_t)((btn & 1) ? at_lampon : at_hi));   /* the sun symbol marks the stick */
    cross_c = c; cross_r = r;
}

static void draw_status(void)
{
    char b[80];
    fill(20, 1, 78, ' ', at_back);
    fill(21, 1, 78, ' ', at_back);
    fill(22, 1, 78, ' ', at_back);
    snprintf(b, sizeof b, "Game adapter 201h: %s", bios_game ? "installed (INT 11h bit 12)" : "not reported by INT 11h");
    puts_at(20, 2, b, at_text);
    if (bios_ok)
        snprintf(b, sizeof b, "BIOS INT 15h AH=84h:  A %3u,%3u   B %3u,%3u   switches %02Xh",
                 bios_ax[0], bios_ax[1], bios_ax[2], bios_ax[3], bios_sw);
    else snprintf(b, sizeof b, "BIOS INT 15h AH=84h:  not supported");
    puts_at(21, 2, b, at_text);
    puts_at(22, 2, cfgmsg, calibrated ? at_hi : at_dim);
}

static void draw_all(void)
{
    axis_row(3, 0); axis_row(5, 1);
    axis_row(13, 2); axis_row(15, 3);
    lamp(8, 12, btn & 1); lamp(8, 30, btn & 2);
    lamp(17, 12, btn & 4); lamp(17, 30, btn & 8);
    draw_cross();
    draw_status();
}

static void wait_frame(void)
{
    /* one display frame: the vertical retrace (VGA 3DAh bit 3 / Hercules 3BAh bit 7 low) */
    if (mono) {
        while (!(inp(0x3BA) & 0x80)) ;
        while (inp(0x3BA) & 0x80) ;
    } else {
        while (inp(0x3DA) & 8) ;
        while (!(inp(0x3DA) & 8)) ;
    }
}

/* ------------------------------------------------------------------ calibration dialog */
static const char *dl1, *dl2, *dl3;
static void dialog(const char *l1, const char *l2, const char *l3)
{
    dl1 = l1; dl2 = l2; dl3 = l3;
    const int r0 = 7, r1 = 13, c0 = 14, c1 = 65;
    for (int r = r0; r <= r1; r++) fill(r, c0, c1 - c0 + 1, ' ', at_box);
    box(r0, c0, r1, c1, "Calibrate", at_box, 1);
    puts_at(r0 + 2, c0 + 3, l1, at_box);
    puts_at(r0 + 3, c0 + 3, l2, at_box);
    puts_at(r0 + 5, c0 + 3, l3, at_box);
}
/* wait until button 1 is pressed and released; returns 0 on Esc. The readings at the press. */
static int wait_button(unsigned out[4])
{
    int down = 0;
    for (;;) {
        frame++;
        read_stick();
        draw_all();
        dialog(dl1, dl2, dl3);                      /* stays on top of the live readings */
        if (kbhit()) { int k = getch(); if (k == 27) return 0; if (k == 0) getch(); }
        if (!down && (btn & 1) && CONNECTED(0)) { down = 1; memcpy(out, raw, sizeof raw); }
        if (down && !(btn & 1)) return 1;
        wait_frame();
    }
}
static void calibrate(void)
{
    unsigned c[4], ul[4], lr[4];
    static const char esc[] = "Button 1 = OK        Esc = cancel";
    read_stick();
    if (!CONNECTED(0) || !CONNECTED(1)) {
        snprintf(cfgmsg, sizeof cfgmsg, "Calibration: no joystick in port A");
        return;
    }
    draw_static(); dialog("Centre the joystick and press button 1.", "", esc);
    if (!wait_button(c)) goto cancel;
    draw_static(); dialog("Move the joystick to the UPPER LEFT corner", "and press button 1.", esc);
    if (!wait_button(ul)) goto cancel;
    draw_static(); dialog("Move the joystick to the LOWER RIGHT corner", "and press button 1.", esc);
    if (!wait_button(lr)) goto cancel;
    for (int i = 0; i < 4; i++) {
        unsigned lo = ul[i] < lr[i] ? ul[i] : lr[i], hi = ul[i] < lr[i] ? lr[i] : ul[i];
        if (ul[i] >= LIMIT || lr[i] >= LIMIT || c[i] >= LIMIT) continue;   /* not connected: keep what it had */
        if (i >= 2 && lo == hi) continue;                                   /* stick B not moved */
        cal[i].min = lo; cal[i].ctr = c[i]; cal[i].max = hi;
    }
    if (cal[0].min >= cal[0].ctr || cal[0].ctr >= cal[0].max || cal[1].min >= cal[1].ctr || cal[1].ctr >= cal[1].max) {
        default_cal();
        draw_static();
        snprintf(cfgmsg, sizeof cfgmsg, "Calibration failed: the corners must be on both sides of the centre");
        return;
    }
    calibrated = 1;
    draw_static();
    snprintf(cfgmsg, sizeof cfgmsg, "Calibrated: X %u-%u-%u  Y %u-%u-%u  (S saves JOYSTICK.CFG)",
             cal[0].min, cal[0].ctr, cal[0].max, cal[1].min, cal[1].ctr, cal[1].max);
    return;
cancel:
    draw_static();
    snprintf(cfgmsg, sizeof cfgmsg, "Calibration cancelled");
}

/* ------------------------------------------------------------------ main */
static void report(void)
{
    read_stick();
    printf("JOYTEST " VERSION " - game adapter at 201h %s\n", bios_game ? "installed" : "not reported by the BIOS");
    for (int s = 0; s < 2; s++) {
        printf("Joystick %c:", 'A' + s);
        if (!CONNECTED(s * 2) && !CONNECTED(s * 2 + 1)) { printf(" not connected\n"); continue; }
        for (int a = 0; a < 2; a++) {
            if (CONNECTED(s * 2 + a)) printf(" %c=%u", a ? 'Y' : 'X', raw[s * 2 + a]);
            else printf(" %c=----", a ? 'Y' : 'X');
        }
        printf("  button %d %s, button %d %s\n", s * 2 + 1, (btn >> (s * 2)) & 1 ? "pressed" : "up",
               s * 2 + 2, (btn >> (s * 2 + 1)) & 1 ? "pressed" : "up");
    }
    if (bios_ok) printf("BIOS INT 15h AH=84h: A(x)=%u A(y)=%u B(x)=%u B(y)=%u switches=%02Xh\n",
                        bios_ax[0], bios_ax[1], bios_ax[2], bios_ax[3], bios_sw);
    else printf("BIOS INT 15h AH=84h: not supported\n");
    if (cfg_loaded) printf("JOYSTICK.CFG: X %u-%u-%u, Y %u-%u-%u\n", cal[0].min, cal[0].ctr, cal[0].max, cal[1].min, cal[1].ctr, cal[1].max);
}

int main(int argc, char **argv)
{
    int want_cal = 0, rep = 0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if ((a[0] == '/' || a[0] == '-') && a[1]) {
            int o = toupper((unsigned char)a[1]);
            if (o == 'C') want_cal = 1;
            else if (o == 'R') rep = 1;
            else if (o == '?' || o == 'H') {
                printf("Tests and calibrates the joystick(s) on the game adapter (201h).\n\n"
                       "JOYTEST [/C] [/R]\n\n"
                       "  /C   Start with the calibration.\n"
                       "  /R   Report the readings on standard output and exit.\n\n"
                       "The calibration (centre, upper left, lower right, each with button 1)\n"
                       "is saved as JOYSTICK.CFG in the current directory.\n");
                return 0;
            } else { printf("Invalid switch - %s\n", a); return 1; }
        } else { printf("Invalid parameter - %s\n", a); return 1; }
    }
    union REGS r; memset(&r, 0, sizeof r);
    int86(0x11, &r, &r);
    bios_game = (r.x.ax & 0x1000) != 0;
    default_cal();
    cfg_loaded = load_cfg();
    if (cfg_loaded) snprintf(cfgmsg, sizeof cfgmsg, "JOYSTICK.CFG loaded: X %u-%u-%u  Y %u-%u-%u",
                             cal[0].min, cal[0].ctr, cal[0].max, cal[1].min, cal[1].ctr, cal[1].max);
    else snprintf(cfgmsg, sizeof cfgmsg, "Not calibrated: press C");
    read_bios();
    if (rep) { report(); return 0; }

    colours();
    cursor(0);
    draw_static();
    if (want_cal) calibrate();
    int quit = 0;
    while (!quit) {
        frame++;
        read_stick();
        guess_cal();
        if (frame % 8 == 1) read_bios();
        draw_all();
        while (kbhit()) {
            int k = getch();
            if (k == 0) { getch(); continue; }
            k = toupper(k);
            if (k == 27) { quit = 1; break; }
            if (k == 'C') calibrate();
            else if (k == 'D') { default_cal(); snprintf(cfgmsg, sizeof cfgmsg, "Defaults: not calibrated (centre the stick)"); }
            else if (k == 'S') {
                if (save_cfg()) snprintf(cfgmsg, sizeof cfgmsg, "Saved JOYSTICK.CFG: X %u-%u-%u  Y %u-%u-%u",
                                         cal[0].min, cal[0].ctr, cal[0].max, cal[1].min, cal[1].ctr, cal[1].max);
                else snprintf(cfgmsg, sizeof cfgmsg, "Cannot write JOYSTICK.CFG");
            }
        }
        if (!quit) wait_frame();
    }
    /* clear the screen the way a DOS utility leaves it */
    memset(&r, 0, sizeof r);
    r.h.ah = 0x0F; int86(0x10, &r, &r);
    r.h.ah = 0x00; int86(0x10, &r, &r);
    cursor(1);
    return 0;
}
