/* sio.c - session I/O shared by BBS.EXE and its doors, see sio.h. */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include "../term/lib/comm.h"
#include "../term/lib/scr.h"
#include "sio.h"

int sio_remote, sio_ansi = 1, sio_lines = 24, sio_linecount;
uint32_t sio_deadline, sio_idle_ticks;
jmp_buf sio_drop;
struct vt sio_vt;
void (*sio_status_hook)(void);

static int cur_attr = -1, nonstop, esc_state;
static uint32_t last_status, last_input;

void sio_begin(int remote, int ansi, int rows)
{
    sio_remote = remote;
    sio_ansi = ansi;
    vt_init(&sio_vt, VRAM, SCR_W, rows);
    cur_attr = -1;
    nonstop = 0;
    esc_state = 0;
    sio_linecount = 0;
    last_input = TICKS();
}

void sio_touch(void) { last_input = TICKS(); }
int sio_carrier(void) { return !sio_remote || com_carrier(); }

static void check_drop(void)
{
    if (sio_remote && !com_carrier()) longjmp(sio_drop, SIO_CARRIER);
}

/* raw byte to the caller (escape sequences stripped for non-ANSI callers)
 * and to the local screen */
void sio_putc(int c)
{
    c &= 0xFF;
    vt_putc(&sio_vt, c);
    if (!sio_remote) return;
    if (!sio_ansi) {
        if (esc_state == 1) { esc_state = c == '[' ? 2 : 0; return; }
        if (esc_state == 2) { if (c >= 0x40 && c <= 0x7E) esc_state = 0; return; }
        if (c == 27) { esc_state = 1; return; }
    }
    if (com_putc(c) < 0) check_drop();
}

void sio_write(const char *s, int n) { while (n-- > 0) sio_putc((uint8_t)*s++); }

void sio_flush(void)
{
    scr_cursor(sio_vt.x, sio_vt.y);
    if (!sio_remote) return;
    uint32_t t0 = TICKS();
    while (com_txpending() && TICKS() - t0 < 30 * 18) { check_drop(); idle(); }
}

void sio_color(int attr)
{
    static const char pc2ansi[8] = { 0, 4, 2, 6, 1, 5, 3, 7 };
    char b[24];
    if (attr == cur_attr) return;
    cur_attr = attr;
    snprintf(b, sizeof b, "\033[0;%s%s3%d;4%dm", (attr & 8) ? "1;" : "", (attr & 0x80) ? "5;" : "",
             pc2ansi[attr & 7], pc2ansi[(attr >> 4) & 7]);
    sio_write(b, (int)strlen(b));
}

static int hexv(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    c = toupper(c);
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

void sio_puts(const char *s)
{
    while (*s) {
        if (s[0] == '@' && (s[1] == 'X' || s[1] == 'x') && hexv(s[2]) >= 0 && hexv(s[3]) >= 0) {
            sio_color(hexv(s[2]) << 4 | hexv(s[3]));
            s += 4;
            continue;
        }
        if (*s == '\n') { sio_putc('\r'); sio_putc('\n'); s++; continue; }
        if (*s == 27) cur_attr = -1;          /* raw ANSI: our idea of the colour is gone */
        sio_putc((uint8_t)*s++);
    }
    scr_cursor(sio_vt.x, sio_vt.y);
}

void sio_printf(const char *fmt, ...)
{
    char b[512];
    va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    sio_puts(b);
}

void sio_cls(void)
{
    if (sio_ansi) sio_puts("@X07\033[2J\033[H");
    else { sio_putc(12); vt_clear(&sio_vt); }
    sio_linecount = 0;
}

uint32_t sio_minutes_left(void)
{
    if (!sio_deadline) return 999;
    uint32_t now = TICKS();
    if (now >= sio_deadline) return 0;
    return (sio_deadline - now) / 1092 + 1;
}

/* next byte from the caller or the local keyboard; -1 after ms */
static int rawkey(int ms)
{
    uint32_t t0 = TICKS(), lim = ms >= 0 ? ms2ticks((uint32_t)ms) : 0;
    for (;;) {
        if (TICKS() - last_status >= 18) {
            last_status = TICKS();
            if (sio_status_hook) sio_status_hook();
        }
        check_drop();
        if (sio_deadline && TICKS() >= sio_deadline) longjmp(sio_drop, SIO_TIMEUP);
        if (sio_idle_ticks && TICKS() - last_input > sio_idle_ticks) longjmp(sio_drop, SIO_IDLE);
        if (key_ready()) {
            int k = key_get();
            last_input = TICKS();
            if (k == K_ALT(ALT_H) && sio_remote) longjmp(sio_drop, SIO_HANGUP);
            return k;
        }
        if (sio_remote) {
            int c = com_getc();
            if (c >= 0) { last_input = TICKS(); return c; }
        }
        if (ms >= 0 && TICKS() - t0 >= lim) return -1;
        idle();
    }
}

int sio_key_timeout(int ms)
{
    int c = rawkey(ms);
    if (c != 27) return c;
    /* an ANSI cursor key from the caller's terminal? */
    int d = rawkey(150);
    if (d != '[' && d != 'O') return 27;
    int e = rawkey(150);
    switch (e) {
    case 'A': return K_UP;
    case 'B': return K_DOWN;
    case 'C': return K_RIGHT;
    case 'D': return K_LEFT;
    case 'H': return K_HOME;
    case 'K': case 'F': return K_END;
    default:
        while (e >= 0 && !(e >= 0x40 && e <= 0x7E)) e = rawkey(150);
        return -1;
    }
}

int sio_key(void)
{
    for (;;) { int c = sio_key_timeout(-1); if (c >= 0) return c; }
}

int sio_getline(char *buf, int max, int mode)
{
    int n = 0;
    buf[0] = 0;
    for (;;) {
        int c = sio_key();
        if (c == '\r' || c == '\n') break;
        if (c == 8 || c == 127) {
            if (n > 0) { n--; sio_puts("\b \b"); }
            continue;
        }
        if (c == 24 || c == 21) {                   /* ^X / ^U: erase the line */
            while (n > 0) { n--; sio_puts("\b \b"); }
            continue;
        }
        if (c < 32 || c > 255 || c == 127) continue;
        if (n >= max) continue;
        if ((mode & GL_DIGITS) && !isdigit(c) && c != '-' && c != '/') continue;
        if (mode & GL_UPPER) c = toupper(c);
        if (mode & GL_NAME) {
            if (n == 0 || buf[n - 1] == ' ' || buf[n - 1] == '-' || buf[n - 1] == '\'') c = toupper(c);
        }
        buf[n++] = (char)c;
        buf[n] = 0;
        if (mode & GL_PASSWORD) sio_putc('*'); else sio_putc(c);
        scr_cursor(sio_vt.x, sio_vt.y);
    }
    buf[n] = 0;
    /* trim */
    while (n > 0 && buf[n - 1] == ' ') buf[--n] = 0;
    char *p = buf; while (*p == ' ') p++;
    if (p != buf) memmove(buf, p, strlen(p) + 1);
    sio_puts("\n");
    sio_linecount = 0;
    return (int)strlen(buf);
}

int sio_hotkey(const char *valid)
{
    for (;;) {
        int c = sio_key();
        if (c == '\r' && strchr(valid, '\r')) { sio_puts("\n"); return '\r'; }
        if (c < 32 || c > 126) continue;
        c = toupper(c);
        if (strchr(valid, c)) {
            sio_putc(c);
            sio_puts("\n");
            sio_linecount = 0;
            return c;
        }
    }
}

int sio_yesno(const char *q, int def)
{
    sio_printf("%s @X0F[%s]@X07? ", q, def ? "Y/n" : "y/N");
    for (;;) {
        int c = toupper(sio_key());
        if (c == '\r') c = def ? 'Y' : 'N';
        if (c == 'Y' || c == 'N') {
            sio_puts(c == 'Y' ? "Yes\n" : "No\n");
            return c == 'Y';
        }
    }
}

int sio_more(void)
{
    sio_puts("@X0EMore [Y,n,=]? @X07");
    int c;
    for (;;) {
        c = toupper(sio_key());
        if (c == '\r' || c == 'Y' || c == ' ' || c == 'N' || c == '=' || c == 'Q') break;
    }
    sio_puts("\r                 \r");
    sio_linecount = 0;
    if (c == '=') nonstop = 1;
    return !(c == 'N' || c == 'Q');
}

int sio_line_done(void)
{
    if (nonstop) return 1;
    if (++sio_linecount >= sio_lines - 1) return sio_more();
    return 1;
}

void sio_pause(void)
{
    sio_puts("@X0EPress [Enter] to continue @X07");
    for (;;) { int c = sio_key(); if (c == '\r' || c == ' ' || c == 27) break; }
    sio_puts("\r                            \r");
    sio_linecount = 0;
}

int sio_showfile(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    int ans = strstr(path, ".ANS") != NULL || strstr(path, ".ans") != NULL;
    char line[260];
    nonstop = ans;
    sio_linecount = 0;
    while (fgets(line, sizeof line, f)) {
        char *z = strchr(line, 0x1A);           /* EOF mark (and SAUCE after it) */
        if (z) { *z = 0; sio_puts(line); break; }
        /* plain LF files (and CR LF): normalise */
        size_t l = strlen(line);
        int nl = l && line[l - 1] == '\n';
        if (nl) { line[--l] = 0; if (l && line[l - 1] == '\r') line[--l] = 0; }
        sio_puts(line);
        if (nl) {
            sio_puts("\n");
            if (!ans && !sio_line_done()) break;
        }
    }
    fclose(f);
    nonstop = 0;
    sio_puts("@X07");
    return 1;
}
