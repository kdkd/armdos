/* term.c - TERM, a communications program for ARM-DOS.
 *
 * Original program in the style of the late-1980s DOS terminal programs
 * (Procomm Plus, Qmodem, Telix): a full-screen ANSI-BBS terminal with a
 * status line at the bottom, a dialing directory with redial, ZMODEM (with
 * auto-download), YMODEM and XMODEM, capture, scrollback and a setup screen.
 * The 16550 on COM2 is driven directly with an interrupt-driven ring buffer
 * (lib/comm.c), as real DOS comm programs did.
 *
 * Keys: Alt-Z help, Alt-D dialing directory, Alt-H hang up, Alt-X exit,
 * Alt-S setup, Alt-L capture, Alt-B scrollback, Alt-C clear screen,
 * Alt-E local echo, PgUp upload, PgDn download.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dos.h>
#include <direct.h>
#include <armdos.h>
#include "lib/comm.h"
#include "lib/scr.h"
#include "lib/vt.h"
#include "lib/zmodem.h"
#include "lib/xmodem.h"
#include "term.h"

#define VERSION "1.0"
#define TROWS 24                    /* terminal rows; row 24 is the status line */

/* colours */
#define A_STATUS   0x70
#define A_STATHI   0x74
#define A_BOX      0x1F
#define A_BOXHI    0x1E
#define A_BOXDIM   0x17
#define A_SEL      0x70
#define A_TITLE    0x1B
#define A_INPUT    0x70

/* ------------------------------------------------------------ settings */
/* the port is locked at 115200 (as with any fast modem): the modem trains at whatever the
   line allows - 2400 to 56K - and the port is never the bottleneck. docs/MODEM.md */
struct config cfg = { 2, 115200, "AT&C1&D2", "ATDT", "ATH0", "", 10, 5, 1, 1, 0 };

#define MAXDIR 20
struct dentry { char name[28]; char number[20]; long baud; char last[10]; int calls; };
static struct dentry dir[MAXDIR];
static int ndir;

char homedir[80];            /* where TERM.EXE lives (TERM.CFG, TERM.DIR) */
struct vt vt;
int online;                  /* carrier up */
static uint32_t online_t0;
static char online_name[28];
static int capfd = -1;
static char capname[80];
static int zmstate;                 /* ZMODEM auto-download recogniser */
static char held[8]; static int nheld;
static uint16_t savescr[SCR_W * SCR_H];

/* scrollback: lines that scrolled off the top */
#define SB_LINES 400
static uint16_t sb[SB_LINES][SCR_W];
static int sb_head, sb_count;

int xfer_auto_pending, xfer_auto_rc;

/* ------------------------------------------------------------ helpers */
static void path_in_home(char *out, int n, const char *name) { snprintf(out, n, "%s%s", homedir, name); }

static void sb_push(void *ctx, const volatile uint16_t *line, int w)
{
    (void)ctx;
    for (int i = 0; i < w && i < SCR_W; i++) sb[sb_head][i] = line[i];
    sb_head = (sb_head + 1) % SB_LINES;
    if (sb_count < SB_LINES) sb_count++;
}
static void vt_reply(void *ctx, const char *s) { (void)ctx; com_puts(s); }
static void vt_bell(void *ctx) { (void)ctx; beep(880, 60); }

static void load_config(void)
{
    char p[96], line[128];
    path_in_home(p, sizeof p, "TERM.CFG");
    FILE *f = fopen(p, "r");
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        char *e = strchr(line, '='); if (!e) continue;
        *e++ = 0;
        char *nl = strpbrk(e, "\r\n"); if (nl) *nl = 0;
        if (!strcmp(line, "PORT")) cfg.port = atoi(e);
        else if (!strcmp(line, "BAUD")) cfg.baud = atol(e);
        else if (!strcmp(line, "INIT")) snprintf(cfg.init, sizeof cfg.init, "%s", e);
        else if (!strcmp(line, "DIAL")) snprintf(cfg.dialpfx, sizeof cfg.dialpfx, "%s", e);
        else if (!strcmp(line, "HANGUP")) snprintf(cfg.hangup, sizeof cfg.hangup, "%s", e);
        else if (!strcmp(line, "DOWNLOAD")) snprintf(cfg.dldir, sizeof cfg.dldir, "%s", e);
        else if (!strcmp(line, "REDIAL")) cfg.redial_max = atoi(e);
        else if (!strcmp(line, "PAUSE")) cfg.redial_pause = atoi(e);
        else if (!strcmp(line, "AUTOZMODEM")) cfg.autozm = atoi(e);
        else if (!strcmp(line, "RESUME")) cfg.resume = atoi(e);
        else if (!strcmp(line, "ECHO")) cfg.echo = atoi(e);
    }
    fclose(f);
}

static void save_config(void)
{
    char p[96];
    path_in_home(p, sizeof p, "TERM.CFG");
    FILE *f = fopen(p, "w");
    if (!f) return;
    fprintf(f, "PORT=%d\nBAUD=%ld\nINIT=%s\nDIAL=%s\nHANGUP=%s\nDOWNLOAD=%s\nREDIAL=%d\nPAUSE=%d\nAUTOZMODEM=%d\nRESUME=%d\nECHO=%d\n",
            cfg.port, cfg.baud, cfg.init, cfg.dialpfx, cfg.hangup, cfg.dldir, cfg.redial_max, cfg.redial_pause,
            cfg.autozm, cfg.resume, cfg.echo);
    fclose(f);
}

static void default_dir(void)
{
    static const struct dentry d[] = {
        { "The ARM Pit BBS", "555-1989", 2400, "", 0 },
        { "ARM-DOS Host Link", "555-0100", 2400, "", 0 },
        { "Jenny (don't lose it)", "867-5309", 2400, "", 0 },
        { "WOPR - Crystal Palace", "399-2364", 2400, "", 0 },
        { "Europa Micro Support", "555-0142", 2400, "", 0 },
        { "Time and Temperature", "767-2676", 2400, "", 0 },
        { "The 386 Fortress", "555-0386", 2400, "", 0 },
        { "The Floating Point", "555-7734", 2400, "", 0 },
        { "KREMVAX (Moscow?)", "011-7-095-231-1984", 2400, "", 0 },
    };
    ndir = (int)(sizeof d / sizeof d[0]);
    memcpy(dir, d, sizeof d);
}

static void load_dir(void)
{
    char p[96], line[128];
    path_in_home(p, sizeof p, "TERM.DIR");
    FILE *f = fopen(p, "r");
    if (!f) { default_dir(); return; }
    ndir = 0;
    while (ndir < MAXDIR && fgets(line, sizeof line, f)) {
        if (line[0] == ';' || line[0] == '\r' || line[0] == '\n') continue;
        char *fld[5] = { 0 }; int n = 0;
        char *s = line;
        while (n < 5) { fld[n++] = s; s = strchr(s, '|'); if (!s) break; *s++ = 0; }
        for (int i = 0; i < n; i++) { char *nl = strpbrk(fld[i], "\r\n"); if (nl) *nl = 0; }
        if (n < 2) continue;
        struct dentry *e = &dir[ndir++];
        memset(e, 0, sizeof *e);
        snprintf(e->name, sizeof e->name, "%s", fld[0]);
        snprintf(e->number, sizeof e->number, "%s", fld[1]);
        e->baud = n > 2 && fld[2] ? atol(fld[2]) : 2400;
        if (!e->baud) e->baud = 2400;
        if (n > 3 && fld[3]) snprintf(e->last, sizeof e->last, "%s", fld[3]);
        if (n > 4 && fld[4]) e->calls = atoi(fld[4]);
    }
    fclose(f);
    if (!ndir) default_dir();
}

static void save_dir(void)
{
    char p[96];
    path_in_home(p, sizeof p, "TERM.DIR");
    FILE *f = fopen(p, "w");
    if (!f) return;
    fprintf(f, "; TERM dialing directory: name|number|baud|last call|total calls\n");
    for (int i = 0; i < ndir; i++)
        fprintf(f, "%s|%s|%ld|%s|%d\n", dir[i].name, dir[i].number, dir[i].baud, dir[i].last, dir[i].calls);
    fclose(f);
}

static const char *hms(uint32_t secs, char *b)
{
    sprintf(b, "%02lu:%02lu:%02lu", (unsigned long)(secs / 3600), (unsigned long)(secs / 60 % 60), (unsigned long)(secs % 60));
    return b;
}
static uint32_t secs_since(uint32_t t0) { return (uint32_t)((uint64_t)(TICKS() - t0) * 10u / 182u); }

/* ------------------------------------------------------------ status line */
void status(void)
{
    char b[96];
    const char *sep = "\xB3";
    scr_fill(0, 24, 80, 1, ' ', A_STATUS);
    char first[20];
    if (script_running) {                   /* same width as "Alt-Z for Help" */
        char nm[9]; int i = 0;
        while (script_name[i] && script_name[i] != '.' && i < 7) { nm[i] = script_name[i]; i++; }
        nm[i] = 0;
        snprintf(first, sizeof first, "Script %-7s", nm);
    }
    else snprintf(first, sizeof first, "Alt-Z for Help");
    int n = snprintf(b, sizeof b, " %s %s ANSI-BBS %s %ld N81 %s %s %s COM%d %s %s %s ",
                     first, sep, sep, com_baud, sep, cfg.echo ? "HDX" : "FDX", sep, cfg.port, sep,
                     capfd >= 0 ? "LOG ON " : "LOG OFF", sep);
    scr_puts(0, 24, A_STATUS, b);
    if (online) {
        char tt[12];
        snprintf(b, sizeof b, "Online %s", hms(secs_since(online_t0), tt));
        scr_puts(n, 24, A_STATUS, b);
    } else scr_puts(n, 24, A_STATUS, "Offline");
    if (capfd >= 0) scr_puts(n - 9, 24, A_STATHI, "LOG ON ");
}

/* ------------------------------------------------------------ popups */
static void popup(int x, int y, int w, int h, const char *title)
{
    scr_box(x, y, w, h, A_BOX, 1, title);
    scr_shadow(x, y, w, h);
}

int ask_yn(const char *q)
{
    uint16_t s[SCR_W * SCR_H];
    scr_save(s);
    int w = (int)strlen(q) + 12;
    popup((80 - w) / 2, 9, w, 5, NULL);
    scr_printf((80 - w) / 2 + 3, 11, A_BOXHI, "%s (Y/N)?", q);
    int k;
    for (;;) { k = toupper(key_get()); if (k == 'Y' || k == 'N' || k == K_ESC || k == K_ENTER) break; }
    scr_restore(s);
    return k == 'Y' || k == K_ENTER;
}

void message(const char *title, const char *msg, int ms)
{
    uint16_t s[SCR_W * SCR_H];
    scr_save(s);
    int w = (int)strlen(msg) + 8; if (w < 30) w = 30;
    popup((80 - w) / 2, 9, w, 5, title);
    scr_puts((80 - (int)strlen(msg)) / 2, 11, A_BOXHI, msg);
    if (ms > 0) delay_ms(ms); else key_get();
    scr_restore(s);
}

static int prompt(const char *title, const char *label, char *buf, int max)
{
    uint16_t s[SCR_W * SCR_H];
    scr_save(s);
    int w = 64;
    popup(8, 9, w, 6, title);
    scr_puts(11, 11, A_BOXHI, label);
    int ok = scr_input(11, 12, 56, A_INPUT, buf, max);
    scr_restore(s);
    return ok;
}

/* ------------------------------------------------------------ terminal I/O */
static void term_char(int c)
{
    vt_putc(&vt, c);
}

static void flush_held(void) { for (int i = 0; i < nheld; i++) term_char((uint8_t)held[i]); nheld = 0; }


static void rx_byte(int c)
{
    if (script_running) script_rx(c);
    if (capfd >= 0) { char ch = (char)c; write(capfd, &ch, 1); }
    if (cfg.autozm) {
        int was = zmstate;
        if (zm_autodetect(&zmstate, c)) { nheld = 0; xfer_auto_rc = do_download('Z'); xfer_auto_pending = 1; return; }
        if (zmstate > 0) {
            if (zmstate <= was) flush_held();     /* restarted: what we held was text */
            if (nheld < (int)sizeof held) held[nheld++] = (char)c;
            return;
        }
        if (nheld) flush_held();
    }
    term_char(c);
}

void pump(void)
{
    int n = 0, c;
    while (n < 512 && (c = com_getc()) >= 0) { rx_byte(c); n++; }
    if (n) scr_cursor(vt.x, vt.y);
    speech_pump();
}

void check_carrier(void)
{
    int dcd = com_carrier();
    if (online && !dcd) { online = 0; status(); }
    else if (!online && dcd) { online = 1; online_t0 = TICKS(); status(); }
}

static void send_key(int k)
{
    const char *s = NULL;
    char b[2];
    switch (k) {
    case K_UP: s = "\033[A"; break;
    case K_DOWN: s = "\033[B"; break;
    case K_RIGHT: s = "\033[C"; break;
    case K_LEFT: s = "\033[D"; break;
    case K_HOME: s = "\033[H"; break;
    case K_END: s = "\033[K"; break;
    case K_DEL: s = "\x7F"; break;
    case K_INS: s = "\033[@"; break;
    default:
        if (k & KEY_EXT) return;
        b[0] = (char)k; b[1] = 0; s = b;
        if (k == 0) { com_putc(0); return; }
    }
    com_puts(s);
    if (cfg.echo) { for (const char *p = s; *p; p++) { term_char((uint8_t)*p); if (*p == '\r') term_char('\n'); } scr_cursor(vt.x, vt.y); }
}

/* ------------------------------------------------------------ modem talk */
/* collect a result line from the modem (echo and blank lines skipped) */
static int modem_line(char *buf, int max, int timeout_ms, int (*abortchk)(void))
{
    int n = 0;
    uint32_t t0 = TICKS(), to = ms2ticks((uint32_t)timeout_ms);
    for (;;) {
        int c = com_getc();
        if (c < 0) {
            if (TICKS() - t0 > to) return -1;
            if (abortchk && abortchk()) return -2;
            idle();
            continue;
        }
        if (c == '\r' || c == '\n') {
            if (n) { buf[n] = 0; return n; }
            continue;
        }
        if (n < max - 1 && c >= 32) buf[n++] = (char)c;
    }
}

static const char *const results[] = { "OK", "CONNECT", "BUSY", "NO CARRIER", "NO ANSWER", "NO DIALTONE", "ERROR", "RING", "VOICE", NULL };
enum { RC_OK, RC_CONNECT, RC_BUSY, RC_NOCARRIER, RC_NOANSWER, RC_NODIALTONE, RC_ERROR, RC_RING, RC_VOICE };

static int result_code(const char *l)
{
    for (int i = 0; results[i]; i++) if (!strncmp(l, results[i], strlen(results[i]))) return i;
    return -1;
}

static int esc_pressed(void)
{
    if (!key_ready()) return 0;
    int k = key_get();
    return k == K_ESC;
}

/* send a command and wait for a final result; the lines go to log() */
static int modem_cmd(const char *cmd, int timeout_ms, void (*log)(const char *), char *last)
{
    char line[80];
    com_rxpurge();
    com_puts(cmd); com_puts("\r");
    for (;;) {
        int n = modem_line(line, sizeof line, timeout_ms, esc_pressed);
        if (n == -2) return -2;
        if (n < 0) return -1;
        if (!strcmp(line, cmd)) continue;                /* echo */
        if (log) log(line);
        if (last) strcpy(last, line);
        int r = result_code(line);
        if (r >= 0 && r != RC_RING) return r;
    }
}

void hangup(void)
{
    uint16_t s[SCR_W * SCR_H];
    scr_save(s);
    popup(25, 9, 30, 5, NULL);
    scr_puts(32, 11, A_BOXHI, "Hanging up...");
    com_dtr(0);
    delay_ms(600);
    com_dtr(1);
    if (com_carrier()) {                                 /* modem ignores DTR (&D0) */
        delay_ms(1100); com_puts("+++"); delay_ms(1100);
        modem_cmd(cfg.hangup, 3000, NULL, NULL);
    }
    delay_ms(200);
    pump();
    online = com_carrier();
    scr_restore(s);
    status();
}

/* ------------------------------------------------------------ dialing */
#define DLGX 10
#define DLGY 5
#define DLGW 60
#define DLGH 14
static int dlog_row;
static void dial_log(const char *s)
{
    /* scrolling modem log inside the dialing window */
    if (dlog_row >= 4) {
        for (int r = 0; r < 3; r++)
            for (int x = 0; x < DLGW - 6; x++) VRAM[(DLGY + 8 + r) * 80 + DLGX + 3 + x] = VRAM[(DLGY + 9 + r) * 80 + DLGX + 3 + x];
        dlog_row = 3;
    }
    scr_fill(DLGX + 3, DLGY + 8 + dlog_row, DLGW - 6, 1, ' ', A_BOXDIM);
    scr_printf(DLGX + 3, DLGY + 8 + dlog_row, A_BOXDIM, "%.*s", DLGW - 6, s);
    dlog_row++;
}

int dial(const char *name, const char *number, long baud)
{
    uint16_t s[SCR_W * SCR_H];
    char digits[32], cmd[48], last[64] = "", t[16];
    int n = 0;
    for (const char *p = number; *p && n < 30; p++) if (isdigit((unsigned char)*p) || *p == ',' || *p == '*' || *p == '#') digits[n++] = *p;
    digits[n] = 0;
    /* a directory entry's baud never slows a faster locked port (old entries say 2400) */
    if (baud < cfg.baud) baud = cfg.baud;
    if (baud && baud != com_baud) com_setbaud(baud);
    scr_save(s);
    popup(DLGX, DLGY, DLGW, DLGH, "DIALING");
    scr_printf(DLGX + 3, DLGY + 2, A_BOX, "Dialing:");  scr_printf(DLGX + 14, DLGY + 2, A_BOXHI, "%.40s", name);
    scr_printf(DLGX + 3, DLGY + 3, A_BOX, "Number:");   scr_printf(DLGX + 14, DLGY + 3, A_BOXHI, "%s", number);
    scr_printf(DLGX + 3, DLGY + 4, A_BOX, "Attempt:");
    scr_printf(DLGX + 30, DLGY + 4, A_BOX, "Time:");
    scr_printf(DLGX + 3, DLGY + 5, A_BOX, "Pause:");    scr_printf(DLGX + 14, DLGY + 5, A_BOXHI, "%d", cfg.redial_pause);
    scr_printf(DLGX + 30, DLGY + 5, A_BOX, "Last:");
    for (int x = DLGX + 1; x < DLGX + DLGW - 1; x++) scr_putc(x, DLGY + 7, 0xC4, A_BOX);
    scr_puts(DLGX + 3, DLGY + 7, A_BOX, " Modem ");
    scr_puts(DLGX + 12, DLGY + DLGH - 1, A_BOX, " Esc: cancel    Space: recycle now ");
    dlog_row = 0;

    int rc = -1;
    for (int attempt = 1; attempt <= cfg.redial_max; attempt++) {
        scr_printf(DLGX + 14, DLGY + 4, A_BOXHI, "%-3d", attempt);
        /* reset + init, as the setup screen says */
        if (attempt == 1) {
            dial_log("ATZ");
            int r = modem_cmd("ATZ", 3000, dial_log, NULL);
            if (r == -2) break;
            if (r != RC_OK) { dial_log("(no response from modem)"); }
            if (cfg.init[0]) {
                dial_log(cfg.init);
                r = modem_cmd(cfg.init, 3000, dial_log, NULL);
                if (r == -2) break;
            }
        }
        snprintf(cmd, sizeof cmd, "%s%s", cfg.dialpfx, digits);
        dial_log(cmd);
        com_rxpurge();
        com_puts(cmd); com_puts("\r");
        uint32_t t0 = TICKS();
        int r = -1;
        char line[80];
        for (;;) {
            scr_printf(DLGX + 36, DLGY + 4, A_BOXHI, "%s", hms(secs_since(t0), t) + 3);
            int k = key_ready() ? key_get() : 0;
            if (k == K_ESC) { r = -2; break; }
            if (k == ' ') { r = -3; break; }
            int got = 0, c;
            static char lb[80]; static int ln;
            while ((c = com_getc()) >= 0) {
                if (c == '\r' || c == '\n') { if (ln) { lb[ln] = 0; strcpy(line, lb); ln = 0; got = 1; break; } }
                else if (ln < 79 && c >= 32) lb[ln++] = (char)c;
            }
            if (got) {
                if (!strcmp(line, cmd)) continue;
                dial_log(line);
                int rr = result_code(line);
                if (rr >= 0 && rr != RC_RING && rr != RC_OK) { r = rr; strcpy(last, line); break; }
                continue;
            }
            if (secs_since(t0) > 60) { r = -1; strcpy(last, "TIMEOUT"); break; }
            idle();
        }
        if (r == -2 || r == -3) {                     /* abort the call in progress */
            com_puts("\r");
            delay_ms(300);
            if (r == -2) { dial_log("Cancelled"); break; }
            strcpy(last, "RECYCLE");
        }
        scr_printf(DLGX + 36, DLGY + 5, A_BOXHI, "%-16s", last);
        if (r == RC_CONNECT) {
            scr_puts(DLGX + 3, DLGY + 6, A_BOXHI, "CONNECTED!");
            beep(1000, 120); beep(1320, 180);
            rc = 0;
            break;
        }
        if (r == RC_NODIALTONE || r == RC_ERROR) break;
        if (attempt == cfg.redial_max) break;
        /* pause, then redial */
        uint32_t p0 = TICKS();
        int abort = 0;
        while (secs_since(p0) < (uint32_t)cfg.redial_pause) {
            scr_printf(DLGX + 14, DLGY + 6, A_BOX, "Redial in %2lu seconds", (unsigned long)(cfg.redial_pause - secs_since(p0)));
            if (key_ready()) { int k = key_get(); if (k == K_ESC) { abort = 1; break; } if (k == ' ') break; }
            idle();
        }
        scr_fill(DLGX + 14, DLGY + 6, 30, 1, ' ', A_BOX);
        if (abort) break;
    }
    delay_ms(rc == 0 ? 700 : 300);
    scr_restore(s);
    if (rc == 0) {
        online = 1; online_t0 = TICKS();
        snprintf(online_name, sizeof online_name, "%s", name);
        char ln[80];
        snprintf(ln, sizeof ln, "%s", last);
        vt_puts(&vt, "\r\n"); vt_puts(&vt, ln); vt_puts(&vt, "\r\n");
        scr_cursor(vt.x, vt.y);
    }
    status();
    return rc;
}

static void edit_entry(struct dentry *e)
{
    uint16_t s[SCR_W * SCR_H];
    scr_save(s);
    popup(12, 8, 56, 7, "REVISE ENTRY");
    scr_puts(15, 10, A_BOX, "Name:");
    scr_puts(15, 11, A_BOX, "Number:");
    scr_puts(15, 12, A_BOX, "Baud:");
    char b[32];
    scr_puts(24, 11, A_BOXHI, e->number);
    snprintf(b, sizeof b, "%ld", e->baud); scr_puts(24, 12, A_BOXHI, b);
    if (scr_input(24, 10, 26, A_INPUT, e->name, 26)) {
        scr_puts(24, 10, A_BOXHI, e->name);
        if (scr_input(24, 11, 19, A_INPUT, e->number, 19)) {
            scr_puts(24, 11, A_BOXHI, e->number);
            if (scr_input(24, 12, 6, A_INPUT, b, 6)) { long v = atol(b); if (v >= 300) e->baud = v; }
        }
    }
    scr_restore(s);
}

static int is_command(int k);
static int dial_directory(void)
{
    uint16_t s[SCR_W * SCR_H];
    static int sel;
    scr_save(s);
    scr_cursor_on(0);
    for (;;) {
        scr_box(0, 0, 80, 24, A_BOX, 1, "DIALING DIRECTORY");
        scr_puts(9, 2, A_TITLE, "NAME");
        scr_puts(35, 2, A_TITLE, "NUMBER");
        scr_puts(54, 2, A_TITLE, "BAUD");
        scr_puts(60, 2, A_TITLE, "LAST CALL");
        scr_puts(70, 2, A_TITLE, "CALLS");
        for (int x = 1; x < 79; x++) scr_putc(x, 3, 0xC4, A_BOX);
        for (int i = 0; i < 16; i++) {
            int y = 4 + i;
            if (i < ndir) {
                char b[80];
                snprintf(b, sizeof b, " %2d %-25.25s %-17.17s %5ld  %-9s %5d ", i + 1, dir[i].name, dir[i].number,
                         dir[i].baud, dir[i].last[0] ? dir[i].last : "  --", dir[i].calls);
                scr_fill(4, y, 72, 1, ' ', i == sel ? A_SEL : A_BOX);
                scr_puts(5, y, i == sel ? A_SEL : A_BOX, b);
            } else { scr_fill(4, y, 72, 1, ' ', A_BOX); scr_printf(5, y, A_BOXDIM, " %2d", i + 1); }
        }
        for (int x = 1; x < 79; x++) scr_putc(x, 20, 0xC4, A_BOX);
        scr_puts(5, 21, A_BOXHI, "Enter");  scr_puts(11, 21, A_BOX, "Dial entry");
        scr_puts(24, 21, A_BOXHI, "R");      scr_puts(26, 21, A_BOX, "Revise");
        scr_puts(35, 21, A_BOXHI, "M");      scr_puts(37, 21, A_BOX, "Manual dial");
        scr_puts(51, 21, A_BOXHI, "E");      scr_puts(53, 21, A_BOX, "Erase");
        scr_puts(61, 21, A_BOXHI, "Esc");    scr_puts(65, 21, A_BOX, "Exit");
        scr_puts(5, 22, A_BOXHI, "\x18\x19");   scr_puts(8, 22, A_BOX, "Select entry");
        scr_puts(24, 22, A_BOXHI, "1-9");    scr_puts(28, 22, A_BOX, "Dial by number");
        int k = key_get();
        if (k == K_ESC) break;
        if (k != K_ALT(ALT_D) && is_command(k)) { scr_restore(s); scr_cursor_on(1); return k; }   /* run it (Procomm style) */
        if (k == K_UP) { if (sel > 0) sel--; continue; }
        if (k == K_DOWN) { if (sel < ndir - 1) sel++; continue; }
        if (k >= '1' && k <= '9' && k - '1' < ndir) { sel = k - '1'; k = K_ENTER; }
        if (toupper(k) == 'R') {
            if (sel >= ndir && ndir < MAXDIR) { sel = ndir++; memset(&dir[sel], 0, sizeof dir[sel]); dir[sel].baud = 2400; }
            if (sel < ndir) { edit_entry(&dir[sel]); if (!dir[sel].name[0] && !dir[sel].number[0]) ndir--; save_dir(); }
            continue;
        }
        if (toupper(k) == 'E' && sel < ndir) {
            if (ask_yn("Erase this entry")) { memmove(&dir[sel], &dir[sel + 1], (ndir - sel - 1) * sizeof dir[0]); ndir--; if (sel >= ndir && sel) sel--; save_dir(); }
            continue;
        }
        if (toupper(k) == 'M') {
            char num[24] = "";
            if (prompt("MANUAL DIAL", "Number to dial:", num, 20) && num[0]) {
                scr_restore(s);
                dial("Manual", num, com_baud);
                return 0;
            }
            continue;
        }
        if (k == K_ENTER && sel < ndir) {
            scr_restore(s);
            if (dial(dir[sel].name, dir[sel].number, dir[sel].baud) == 0) {
                time_t now = time(NULL);
                struct tm *tm = localtime(&now);
                if (tm) snprintf(dir[sel].last, sizeof dir[sel].last, "%02d/%02d/%02d", tm->tm_mon + 1, tm->tm_mday, tm->tm_year % 100);
                dir[sel].calls++;
                save_dir();
            }
            return 0;
        }
    }
    scr_restore(s);
    scr_cursor_on(1);
    return 0;
}

/* ------------------------------------------------------------ transfers */
#define XX 14
#define XY 6
#define XW 52
#define XH 13
static int xfer_dir;                /* 0 = download, 1 = upload */
static const char *xfer_proto;

static void xfer_window(void)
{
    char t[40];
    snprintf(t, sizeof t, "%s %s", xfer_proto, xfer_dir ? "UPLOAD" : "DOWNLOAD");
    popup(XX, XY, XW, XH, t);
    static const char *const lab[] = { "File name:", "File size:", "Bytes:", "CPS:", "Time left:", "Errors:", "Message:" };
    for (int i = 0; i < 7; i++) scr_puts(XX + 3, XY + 2 + i, A_BOX, lab[i]);
    scr_puts(XX + 18, XY + XH - 1, A_BOX, " Esc to abort ");
}

static void commas(char *b, long v)
{
    char t[20]; int n = sprintf(t, "%ld", v), o = 0;
    for (int i = 0; i < n; i++) { if (i && (n - i) % 3 == 0 && t[0] != '-') b[o++] = ','; b[o++] = t[i]; }
    b[o] = 0;
}

static void xfer_show(const char *fname, long size, long pos, long startpos, uint32_t t0, int errors, const char *msg)
{
    char b[40], c[20];
    int x = XX + 17;
    scr_printf(x, XY + 2, A_BOXHI, "%-30.30s", fname);
    if (size >= 0) { commas(c, size); scr_printf(x, XY + 3, A_BOXHI, "%-30s", c); }
    else scr_printf(x, XY + 3, A_BOXHI, "%-30s", "unknown");
    commas(c, pos);
    if (size > 0) snprintf(b, sizeof b, "%-12s (%ld%%)", c, pos * 100 / size); else snprintf(b, sizeof b, "%s", c);
    scr_printf(x, XY + 4, A_BOXHI, "%-30s", b);
    uint32_t el = TICKS() - t0;
    long cps = el > 9 ? (long)((long long)(pos - startpos) * 182 / (long long)el / 10) : 0;   /* 64-bit: bytes*182 overflows 32 bits past 11.8 MB */
    scr_printf(x, XY + 5, A_BOXHI, "%-10ld", cps);
    if (cps > 0 && size > 0) { char h[12]; scr_printf(x, XY + 6, A_BOXHI, "%s", hms((uint32_t)((size - pos) / cps), h)); }
    else scr_printf(x, XY + 6, A_BOXHI, "--:--:--");
    scr_printf(x, XY + 7, A_BOXHI, "%-6d", errors);
    scr_printf(x, XY + 8, A_BOXHI, "%-30.30s", msg ? msg : "");
    /* bar */
    int w = XW - 8, fill = size > 0 ? (int)((long long)pos * w / size) : 0;
    for (int i = 0; i < w; i++) scr_putc(XX + 4 + i, XY + 10, i < fill ? 0xDB : 0xB0, i < fill ? A_BOXHI : A_BOXDIM);
}

static uint32_t last_draw;
static void zm_event(struct zm *z, int e)
{
    if (e == ZE_DATA && TICKS() - last_draw < 4) return;
    last_draw = TICKS();
    const char *m = z->msg[0] ? z->msg : (e == ZE_DONEFILE ? "Complete" : (xfer_dir ? "Sending" : "Receiving"));
    xfer_show(z->fname, z->fsize, z->pos, z->startpos, z->t0, z->errors, m);
    if (e == ZE_MSG) z->msg[0] = 0;
}

/* comm callbacks for the protocol modules */
static int io_rx(void *ctx, int to)
{
    (void)ctx;
    uint32_t t0 = TICKS(), lim = ms2ticks((uint32_t)to);
    for (;;) {
        int c = com_getc();
        if (c >= 0) return c;
        if (!com_carrier()) return ZM_CARRIER;
        if (TICKS() - t0 >= lim) return ZM_TIMEOUT;
        idle();
    }
}
static int io_ready(void *ctx) { (void)ctx; return com_avail(); }
static void io_tx(void *ctx, const uint8_t *b, int n) { (void)ctx; com_write(b, n); }
static void io_flush(void *ctx) { (void)ctx; com_txflush(); }
static void io_purge(void *ctx) { (void)ctx; com_txpurge(); }
static int io_abort(void *ctx)
{
    (void)ctx;
    if (key_ready() && key_get() == K_ESC) return 1;
    return 0;
}

static void xm_event(struct xm *x, int e)
{
    (void)e;
    if (e == XE_DATA && TICKS() - last_draw < 4) return;
    last_draw = TICKS();
    xfer_show(x->fname, x->fsize, x->pos, 0, x->t0, x->errors, x->msg);
}

static void xfer_done(int rc, const char *what)
{
    char b[64];
    snprintf(b, sizeof b, "%s", rc == 0 ? what : zm_errstr(rc));
    scr_fill(XX + 17, XY + 8, 30, 1, ' ', A_BOX);
    scr_printf(XX + 17, XY + 8, A_BOXHI, "%-30.30s", b);
    if (rc == 0) { beep(1200, 100); beep(1600, 150); } else beep(300, 400);
    delay_ms(1500);
}

static const char *dl_dir(void) { return cfg.dldir[0] ? cfg.dldir : homedir; }

static int pick_protocol(const char *title)
{
    uint16_t s[SCR_W * SCR_H];
    scr_save(s);
    popup(28, 7, 26, 9, title);
    scr_puts(31, 9, A_BOXHI, "Z"); scr_puts(33, 9, A_BOX, "ZMODEM");
    scr_puts(31, 10, A_BOXHI, "Y"); scr_puts(33, 10, A_BOX, "YMODEM (batch)");
    scr_puts(31, 11, A_BOXHI, "G"); scr_puts(33, 11, A_BOX, "YMODEM-G");
    scr_puts(31, 12, A_BOXHI, "X"); scr_puts(33, 12, A_BOX, "XMODEM (CRC)");
    scr_puts(31, 13, A_BOXHI, "K"); scr_puts(33, 13, A_BOX, "XMODEM-1K");
    int k;
    for (;;) {
        k = toupper(key_get());
        if (k == K_ESC) { k = 0; break; }
        if (strchr("ZYGXK", k)) break;
    }
    scr_restore(s);
    return k;
}

int do_download(int proto)
{
    uint16_t s[SCR_W * SCR_H];
    char fname[80] = "";
    if (!proto) return ZM_ABORTED;
    if (proto == 'X' || proto == 'K') {
        if (!prompt("DOWNLOAD", "Please enter filename:", fname, 60) || !fname[0]) return ZM_ABORTED;
    }
    scr_save(s);
    scr_cursor_on(0);
    xfer_dir = 0;
    xfer_proto = proto == 'Z' ? "ZMODEM" : proto == 'Y' ? "YMODEM" : proto == 'G' ? "YMODEM-G" : proto == 'K' ? "XMODEM-1K" : "XMODEM";
    xfer_window();
    int rc;
    if (proto == 'Z') {
        static struct zm z;
        zm_init(&z);
        z.rx = io_rx; z.rxready = io_ready; z.tx = io_tx; z.txflush = io_flush; z.txpurge = io_purge;
        z.aborted = io_abort; z.event = zm_event; z.dir = dl_dir(); z.resume = cfg.resume;
        xfer_show("", -1, 0, 0, TICKS(), 0, "Waiting for sender");
        rc = zm_receive(&z);
        char b[48]; snprintf(b, sizeof b, "%d file(s) received", z.files);
        if (rc == 0 && !z.files) rc = ZM_ERROR;
        xfer_done(rc, b);
    } else {
        static struct xm x;
        xm_init(&x);
        x.rx = io_rx; x.tx = io_tx; x.txflush = io_flush; x.txpurge = io_purge; x.aborted = io_abort; x.event = xm_event;
        x.dir = dl_dir();
        x.onek = proto == 'K'; x.gmode = proto == 'G';
        char path[160];
        snprintf(path, sizeof path, "%s%s%s", dl_dir(), dl_dir()[strlen(dl_dir()) - 1] == '\\' ? "" : "\\", fname);
        rc = (proto == 'Y' || proto == 'G') ? xm_recv_batch(&x) : xm_recv(&x, strchr(fname, '\\') || strchr(fname, ':') ? fname : path);
        xfer_done(rc, "Transfer complete");
    }
    scr_restore(s);
    scr_cursor_on(1);
    zmstate = 0;
    status();
    return rc;
}

static void do_upload(int proto)
{
    char fname[80] = "";
    if (!proto) return;
    if (!prompt("UPLOAD", "Please enter filename(s) to send:", fname, 70) || !fname[0]) return;
    /* expand a wildcard / several names */
    static char names[16][80];
    char *list[16];
    int n = 0;
    char *tok = strtok(fname, " ");
    while (tok && n < 16) {
        if (strpbrk(tok, "*?")) {
            struct find_t f;
            char dirpart[80]; snprintf(dirpart, sizeof dirpart, "%s", tok);
            char *bs = strrchr(dirpart, '\\'); if (!bs) bs = strrchr(dirpart, ':');
            if (bs) bs[1] = 0; else dirpart[0] = 0;
            if (_dos_findfirst(tok, _A_NORMAL | _A_RDONLY | _A_ARCH, &f) == 0) do {
                snprintf(names[n], sizeof names[n], "%s%s", dirpart, f.name);
                list[n] = names[n]; n++;
            } while (n < 16 && _dos_findnext(&f) == 0);
        } else { snprintf(names[n], sizeof names[n], "%s", tok); list[n] = names[n]; n++; }
        tok = strtok(NULL, " ");
    }
    if (!n) { message("UPLOAD", "No files found", 1500); return; }
    for (int i = 0; i < n; i++) if (access(list[i], 0) != 0) { char b[96]; snprintf(b, sizeof b, "Can't find %.60s", list[i]); message("UPLOAD", b, 2000); return; }
    send_files(proto, list, n);
}

int send_files(int proto, char *const *list, int n)
{
    uint16_t s[SCR_W * SCR_H];
    scr_save(s);
    scr_cursor_on(0);
    xfer_dir = 1;
    xfer_proto = proto == 'Z' ? "ZMODEM" : proto == 'Y' ? "YMODEM" : proto == 'G' ? "YMODEM-G" : proto == 'K' ? "XMODEM-1K" : "XMODEM";
    xfer_window();
    int rc;
    if (proto == 'Z') {
        static struct zm z;
        zm_init(&z);
        z.rx = io_rx; z.rxready = io_ready; z.tx = io_tx; z.txflush = io_flush; z.txpurge = io_purge;
        z.aborted = io_abort; z.event = zm_event;
        xfer_show(list[0], -1, 0, 0, TICKS(), 0, "Waiting for receiver");
        rc = zm_send(&z, list, n);
        char b[48]; snprintf(b, sizeof b, "%d file(s) sent", z.files);
        xfer_done(rc, b);
    } else {
        static struct xm x;
        xm_init(&x);
        x.rx = io_rx; x.tx = io_tx; x.txflush = io_flush; x.txpurge = io_purge; x.aborted = io_abort; x.event = xm_event;
        x.onek = proto == 'K' || proto == 'Y' || proto == 'G';
        rc = (proto == 'Y' || proto == 'G') ? xm_send_batch(&x, list, n) : xm_send(&x, list[0]);
        xfer_done(rc, "Transfer complete");
    }
    scr_restore(s);
    scr_cursor_on(1);
    status();
    return rc;
}

/* ------------------------------------------------------------ screens */
static int help(void)
{
    uint16_t s[SCR_W * SCR_H];
    scr_save(s);
    scr_cursor_on(0);
    popup(4, 2, 72, 20, "TERM " VERSION " COMMAND MENU");
    static const char *const l[] = {
        "Alt-D", "Dialing directory",   "PgUp", "Send files (upload)",
        "Alt-H", "Hang up",             "PgDn", "Receive files (download)",
        "Alt-S", "Setup",               "Alt-L", "Capture file on/off",
        "Alt-B", "Scrollback buffer",   "Alt-C", "Clear screen",
        "Alt-E", "Local echo (HDX/FDX)","Alt-X", "Exit to DOS",
        "Alt-Z", "This help screen",    "Alt-V", "Voice (speech) on/off",
    };
    scr_puts(8, 4, A_TITLE, "COMMUNICATIONS                     FILE TRANSFER / OTHER");
    for (int i = 0; i < 6; i++) {
        scr_puts(8, 6 + i * 2, A_BOXHI, l[i * 4]);     scr_puts(16, 6 + i * 2, A_BOX, l[i * 4 + 1]);
        scr_puts(43, 6 + i * 2, A_BOXHI, l[i * 4 + 2]); scr_puts(51, 6 + i * 2, A_BOX, l[i * 4 + 3]);
    }
    scr_puts(8, 18, A_BOXDIM, "ZMODEM downloads start by themselves when the other end sends.");
    scr_puts(8, 19, A_BOXDIM, "Terminal: ANSI-BBS (VT100 subset), ANSI music off.");
    scr_puts(15, 21, A_BOX, " Press a command key, or any other key to continue ");
    int k = key_get();
    scr_restore(s);
    scr_cursor_on(1);
    return k;
}

static void setup(void)
{
    uint16_t s[SCR_W * SCR_H];
    static const long bauds[] = { 300, 1200, 2400, 9600, 19200, 38400, 57600, 115200 };
    int sel = 0;
    scr_save(s);
    scr_cursor_on(0);
    for (;;) {
        char b[80];
        popup(8, 3, 64, 18, "SETUP");
        const char *lab[] = { "Port", "Baud rate", "Modem init string", "Dial command", "Hang-up command",
                              "Download directory", "Redial attempts", "Pause between calls",
                              "ZMODEM auto-download", "ZMODEM crash recovery", "Local echo" };
        for (int i = 0; i < 11; i++) {
            int a = i == sel ? A_SEL : A_BOX;
            scr_fill(10, 5 + i, 60, 1, ' ', a);
            scr_puts(11, 5 + i, a, lab[i]);
            switch (i) {
            case 0: snprintf(b, sizeof b, "COM%d", cfg.port); break;
            case 1: snprintf(b, sizeof b, "%ld", cfg.baud); break;
            case 2: snprintf(b, sizeof b, "%s", cfg.init); break;
            case 3: snprintf(b, sizeof b, "%s", cfg.dialpfx); break;
            case 4: snprintf(b, sizeof b, "%s", cfg.hangup); break;
            case 5: snprintf(b, sizeof b, "%s", dl_dir()); break;
            case 6: snprintf(b, sizeof b, "%d", cfg.redial_max); break;
            case 7: snprintf(b, sizeof b, "%d seconds", cfg.redial_pause); break;
            case 8: snprintf(b, sizeof b, "%s", cfg.autozm ? "Yes" : "No"); break;
            case 9: snprintf(b, sizeof b, "%s", cfg.resume ? "On" : "Off"); break;
            case 10: snprintf(b, sizeof b, "%s", cfg.echo ? "On (half duplex)" : "Off (full duplex)"); break;
            }
            scr_printf(35, 5 + i, i == sel ? A_SEL : A_BOXHI, "%.34s", b);
        }
        scr_puts(11, 18, A_BOXDIM, "\x18\x19 select   Enter/Space change   Esc save and exit");
        int k = key_get();
        if (k == K_ESC) break;
        if (k == K_UP && sel > 0) sel--;
        else if (k == K_DOWN && sel < 10) sel++;
        else if (k == K_ENTER || k == ' ') {
            char e[64];
            switch (sel) {
            case 0: cfg.port = cfg.port == 1 ? 2 : 1; break;
            case 1: { int i = 0; while (i < 8 && bauds[i] != cfg.baud) i++; cfg.baud = bauds[(i + 1) % 8]; break; }
            case 2: snprintf(e, sizeof e, "%s", cfg.init); if (scr_input(35, 5 + sel, 34, A_INPUT, e, 46)) snprintf(cfg.init, sizeof cfg.init, "%s", e); break;
            case 3: snprintf(e, sizeof e, "%s", cfg.dialpfx); if (scr_input(35, 5 + sel, 34, A_INPUT, e, 14)) snprintf(cfg.dialpfx, sizeof cfg.dialpfx, "%s", e); break;
            case 4: snprintf(e, sizeof e, "%s", cfg.hangup); if (scr_input(35, 5 + sel, 34, A_INPUT, e, 14)) snprintf(cfg.hangup, sizeof cfg.hangup, "%s", e); break;
            case 5: snprintf(e, sizeof e, "%s", dl_dir()); if (scr_input(35, 5 + sel, 34, A_INPUT, e, 62)) snprintf(cfg.dldir, sizeof cfg.dldir, "%s", e); break;
            case 6: cfg.redial_max = cfg.redial_max >= 50 ? 1 : cfg.redial_max + (cfg.redial_max < 10 ? 1 : 10); break;
            case 7: cfg.redial_pause = cfg.redial_pause >= 60 ? 0 : cfg.redial_pause + 5; break;
            case 8: cfg.autozm ^= 1; break;
            case 9: cfg.resume ^= 1; break;
            case 10: cfg.echo ^= 1; break;
            }
        }
    }
    save_config();
    scr_restore(s);
    scr_cursor_on(1);
    if (cfg.port != com_portno) {
        com_close(1);
        if (com_open(cfg.port, cfg.baud) < 0) message("SETUP", "No serial port at that address", 2000);
    } else if (cfg.baud != com_baud) com_setbaud(cfg.baud);
    status();
}

static void capture_toggle(void)
{
    if (capfd >= 0) {
        close(capfd); capfd = -1;
        char b[100]; snprintf(b, sizeof b, "Capture file %.60s closed", capname);
        message("CAPTURE", b, 1200);
    } else {
        if (!capname[0]) path_in_home(capname, sizeof capname, "TERM.CAP");
        if (!prompt("CAPTURE", "Log incoming text to file:", capname, 70) || !capname[0]) return;
        capfd = open(capname, O_WRONLY | O_CREAT | O_APPEND | O_BINARY, 0644);
        if (capfd < 0) message("CAPTURE", "Can't open the capture file", 1500);
    }
    status();
}

static void scrollback(void)
{
    uint16_t s[SCR_W * SCR_H];
    scr_save(s);
    scr_cursor_on(0);
    int total = sb_count + TROWS;               /* scrollback lines + current screen */
    int top = total - TROWS;                    /* first line shown */
    for (;;) {
        for (int r = 0; r < TROWS; r++) {
            int l = top + r;
            const uint16_t *src;
            if (l < sb_count) src = sb[(sb_head - sb_count + l + SB_LINES) % SB_LINES];
            else src = s + (l - sb_count) * SCR_W;
            for (int x = 0; x < SCR_W; x++) VRAM[r * SCR_W + x] = src[x];
        }
        scr_fill(0, 24, 80, 1, ' ', 0x1F);
        scr_printf(1, 24, 0x1F, "SCROLLBACK  line %d of %d   \x18\x19 PgUp PgDn Home End move   Esc returns",
                   top + 1, total);
        int k = key_get();
        if (k == K_ESC || k == K_ALT(ALT_B)) break;
        if (k == K_UP) top--;
        else if (k == K_DOWN) top++;
        else if (k == K_PGUP) top -= TROWS - 1;
        else if (k == K_PGDN) top += TROWS - 1;
        else if (k == K_HOME) top = 0;
        else if (k == K_END) top = total - TROWS;
        if (top < 0) top = 0;
        if (top > total - TROWS) top = total - TROWS;
    }
    scr_restore(s);
    scr_cursor_on(1);
    status();
}

static void int23(struct armregs *f) { f->cpsr &= ~ARM_CPSR_C; }   /* Ctrl-C: keep going */

/* one row of the title box: the text padded to the box's inner width, so the right edge
   always lines up whatever the version string's length */
#define TITLE_W 51
static void title_row(const char *attr, const char *text)
{
    char buf[TITLE_W + 1];
    snprintf(buf, sizeof buf, "%-*.*s", TITLE_W, TITLE_W, text);
    vt_puts(&vt, "  \xBA");
    vt_puts(&vt, attr);
    vt_puts(&vt, buf);
    vt_puts(&vt, "\033[1;36m\xBA\r\n");
}

static void title_rule(char left, char right)
{
    char buf[TITLE_W + 8];
    int n = 0;
    buf[n++] = ' '; buf[n++] = ' '; buf[n++] = left;
    for (int i = 0; i < TITLE_W; i++) buf[n++] = (char)0xCD;
    buf[n++] = right; buf[n++] = '\r'; buf[n++] = '\n'; buf[n] = 0;
    vt_puts(&vt, buf);
}

static void title(void)
{
    vt_puts(&vt, "\033[2J\033[1;36m\r\n");
    title_rule((char)0xC9, (char)0xBB);
    title_row("\033[1;37m", "   TERM  Version " VERSION);
    title_row("\033[0;37m", "   Communications for ARM-DOS");
    title_row("\033[0;37m", "   (C) 1989 Europa Micro Systems");
    title_rule((char)0xC8, (char)0xBC);
    vt_puts(&vt, "\033[0m\r\n");
    vt_puts(&vt, "  Press \033[1mAlt-D\033[0m for the dialing directory, \033[1mAlt-Z\033[0m for help.\r\n\r\n");
    vt_puts(&vt, "  The speed switch on the modem (under the monitor) makes it anything from a\r\n"
                 "  2400 bps modem to a 56K one. Set it before you dial.\r\n\r\n");
}

void term_exit(int code)
{
    speech_stop();
    if (online) hangup();
    if (capfd >= 0) close(capfd);
    com_close(online);
    scr_restore(savescr);
    { union REGS r; r.x.ax = 0x0003; int86(0x10, &r, &r); }
    printf("TERM " VERSION " - thank you.\n");
    exit(code);
}

static void vt_dcs(void *ctx, const char *s) { (void)ctx; speech_dcs(s); }

/* The Alt-key commands, from the terminal and from the menus (Alt-Z's menu runs the command you
 * press, as Procomm's did). Returns 1 if k was a command; *quit is set by Alt-X. */
static int is_command(int k)
{
    switch (k) {
    case K_ALT(ALT_Z): case K_ALT(ALT_D): case K_ALT(ALT_H): case K_ALT(ALT_S): case K_ALT(ALT_L): case K_ALT(ALT_B):
    case K_ALT(ALT_C): case K_ALT(ALT_E): case K_ALT(ALT_V): case K_ALT(ALT_X): case K_PGUP: case K_PGDN:
        return 1;
    }
    return 0;
}
static int command(int k, int *quit)
{
    switch (k) {
    case K_ALT(ALT_Z): { int k2 = help(); if (k2 != K_ALT(ALT_Z) && is_command(k2)) return command(k2, quit); return 1; }
    case K_ALT(ALT_D): { int k2 = dial_directory(); if (k2 && k2 != K_ALT(ALT_D) && is_command(k2)) return command(k2, quit); return 1; }
    case K_ALT(ALT_H): hangup(); return 1;
    case K_ALT(ALT_S): setup(); return 1;
    case K_ALT(ALT_L): capture_toggle(); return 1;
    case K_ALT(ALT_B): scrollback(); return 1;
    case K_ALT(ALT_C): vt_clear(&vt); scr_cursor(0, 0); return 1;
    case K_ALT(ALT_E): cfg.echo ^= 1; status(); return 1;
    case K_ALT(ALT_V):
        speech_on ^= 1;
        if (!speech_on) speech_stop();
        message("VOICE", speech_on ? "Speech on: the other end may talk (ESC P speak)" : "Speech off", 1200);
        return 1;
    case K_PGUP: do_upload(pick_protocol("UPLOAD")); return 1;
    case K_PGDN: do_download(pick_protocol("DOWNLOAD")); return 1;
    case K_ALT(ALT_X):
        if (ask_yn("Exit to DOS")) {
            int hang = online && ask_yn("Hang up the line");
            if (hang) hangup();
            speech_stop();
            if (capfd >= 0) close(capfd);
            com_close(!hang && online);
            scr_restore(savescr);
            { union REGS r; r.x.ax = 0x0003; int86(0x10, &r, &r); }
            printf("TERM " VERSION " - thank you.\n");
            *quit = script_stopped ? 1 : 0;
        }
        return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    char script[96] = "";
    /* home directory = where TERM.EXE is */
    const char *self = _armdos_progpath && *_armdos_progpath ? _armdos_progpath : (argc > 0 ? argv[0] : "");
    snprintf(homedir, sizeof homedir, "%s", self);
    char *bs = strrchr(homedir, '\\');
    if (bs) bs[1] = 0; else homedir[0] = 0;
    load_config();
    load_dir();
    if (!cfg.dldir[0]) { snprintf(cfg.dldir, sizeof cfg.dldir, "%sDOWNLOAD", homedir); }
    mkdir(cfg.dldir);
    for (int i = 1; i < argc; i++) {
        if (!strcasecmp(argv[i], "/COM1")) cfg.port = 1;
        else if (!strcasecmp(argv[i], "/COM2")) cfg.port = 2;
        else if (!strncasecmp(argv[i], "/S:", 3) || !strncasecmp(argv[i], "/S=", 3) || !strncasecmp(argv[i], "-S:", 3)) {
            if (!script_find(argv[i] + 3, script, sizeof script)) { printf("TERM: script %s not found\n", argv[i] + 3); return 1; }
        }
    }
    _dos_setvect(0x23, int23);
    scr_init();
    scr_save(savescr);
    vt_init(&vt, VRAM, SCR_W, TROWS);
    vt.scrolled = sb_push; vt.reply = vt_reply; vt.bell = vt_bell; vt.dcs = vt_dcs;
    vt_clear(&vt);
    if (com_open(cfg.port, cfg.baud) < 0) {
        char b[64]; snprintf(b, sizeof b, "No serial port found on COM%d", cfg.port);
        message("TERM", b, 2500);
    }
    title();
    scr_cursor(vt.x, vt.y);
    online = com_carrier();
    if (online) online_t0 = TICKS();
    status();
    if (script[0]) script_run(script);
    uint32_t last_status = TICKS();
    for (;;) {
        pump();
        if (key_ready()) {
            int k = key_get(), quit = -1;
            if (!command(k, &quit)) send_key(k);
            if (quit >= 0) return quit;
        }
        if (TICKS() - last_status >= 9) { last_status = TICKS(); check_carrier(); status(); }
        if (!com_avail() && !key_ready()) idle();
    }
}
