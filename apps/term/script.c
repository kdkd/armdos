/* script.c - TERM's script language: a small Procomm ASPECT / Telix SALT
 * flavoured language for unattended calls (TERM /S:FILE.SCR).
 *
 *   ; comment                      label:  or  :label
 *   MESSAGE "text" / TYPE "text"   show a line locally (not sent)
 *   CLEAR                          clear the terminal screen
 *   SET BAUDRATE n | INIT "s" | DOWNLOAD "dir" | AUTOZMODEM ON|OFF
 *       | ECHO ON|OFF | DIALATTEMPTS n
 *   DIAL "number" ["name"]         dialing box; success = CONNECT
 *   WAITFOR "text" [seconds]       default 30; the screen keeps running
 *   TRANSMIT "text^M"              ^M ^J ^[ ^^ escapes, "" = a quote
 *   PAUSE n                        seconds
 *   DOWNLOAD ZMODEM / GETFILE ZMODEM
 *   UPLOAD ZMODEM "file" / SENDFILE ZMODEM "file"
 *   HANGUP   BEEP / ALARM   GOTO label
 *   IF [NOT] SUCCESS|FAILURE command
 *   EXIT [n]                       leave TERM with errorlevel n
 *   END                            stop the script (terminal stays)
 *
 * Every command sets the success flag that IF tests. Esc stops the script.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <unistd.h>
#include <direct.h>
#include "lib/comm.h"
#include "lib/scr.h"
#include "lib/vt.h"
#include "lib/zmodem.h"
#include "term.h"

int script_running, script_stopped;
char script_name[16];

#define MAXLINES 600
#define LINELEN  160
static char (*lines)[LINELEN];
static int nlines;
static int success;
static int aborted;

/* ---- WAITFOR matching: the received text, ANSI sequences removed */
#define MATCHBUF 128
static char recent[MATCHBUF];
static int recent_n;
static int esc_state;
static uint32_t rx_seq;             /* bytes seen, to know when to re-test */

void script_rx(int c)
{
    if (esc_state == 1) { esc_state = c == '[' ? 2 : 0; return; }
    if (esc_state == 2) { if (c >= 0x40 && c <= 0x7E) esc_state = 0; return; }
    if (c == 27) { esc_state = 1; return; }
    if (c == 0) return;
    if (recent_n == MATCHBUF) { memmove(recent, recent + MATCHBUF / 2, MATCHBUF / 2); recent_n = MATCHBUF / 2; }
    recent[recent_n++] = (char)c;
    rx_seq++;
}

/* found: forget everything up to the end of the match, so the next WAITFOR
 * looks at what came after it */
static int matched(const char *t)
{
    int l = (int)strlen(t);
    for (int i = 0; i + l <= recent_n; i++)
        if (!memcmp(recent + i, t, (size_t)l)) {
            memmove(recent, recent + i + l, (size_t)(recent_n - i - l));
            recent_n -= i + l;
            return 1;
        }
    return 0;
}
static void forget(void) { recent_n = 0; esc_state = 0; }

/* ---- file lookup: as given, then in TERM's directory; .SCR by default */
static int exists(const char *p) { return access(p, 0) == 0; }

int script_find(const char *arg, char *path, int max)
{
    char name[80];
    snprintf(name, sizeof name, "%s", arg);
    const char *base = strrchr(name, '\\'); base = base ? base + 1 : name;
    if (!strchr(base, '.') && strlen(name) + 4 < sizeof name) strcat(name, ".SCR");
    snprintf(path, max, "%s", name);
    if (exists(path)) return 1;
    if (!strchr(name, '\\') && !strchr(name, ':')) {
        snprintf(path, max, "%s%s", homedir, name);
        if (exists(path)) return 1;
    }
    return 0;
}

/* ---- the terminal keeps running while the script waits */
static int check_esc(void)
{
    if (!key_ready()) return 0;
    int k = key_get();
    if (k == K_ESC && ask_yn("Stop the script")) { aborted = 1; return 1; }
    return 0;
}

static uint32_t last_stat;
static void tick(void)
{
    pump();
    if (TICKS() - last_stat >= 9) { last_stat = TICKS(); check_carrier(); status(); }
    if (!com_avail() && !key_ready()) idle();
}

/* ---- lexing */
static const char *skipws(const char *s) { while (*s == ' ' || *s == '\t') s++; return s; }

static const char *word(const char *s, char *w, int max)
{
    int n = 0;
    s = skipws(s);
    while (*s && *s != ' ' && *s != '\t' && n < max - 1) w[n++] = (char)toupper((unsigned char)*s++);
    w[n] = 0;
    return s;
}

/* a quoted string (or a bare word) with ^X escapes; returns NULL if missing */
static const char *string(const char *s, char *out, int max)
{
    int n = 0;
    s = skipws(s);
    if (!*s || *s == ';') return NULL;
    int quoted = *s == '"';
    if (quoted) s++;
    while (*s) {
        int c = (unsigned char)*s;
        if (quoted && c == '"') {
            if (s[1] == '"') { s += 2; if (n < max - 1) out[n++] = '"'; continue; }
            s++; break;
        }
        if (!quoted && (c == ' ' || c == '\t')) break;
        if (c == '^' && s[1]) {
            int d = (unsigned char)s[1];
            s += 2;
            if (d == '^') c = '^';
            else if (d == '[') c = 27;
            else if (d >= '@' && d <= '_') c = d - '@';
            else if (d >= 'a' && d <= 'z') c = d - 'a' + 1;
            else c = d;
            if (n < max - 1) out[n++] = (char)c;
            continue;
        }
        if (n < max - 1) out[n++] = (char)c;
        s++;
    }
    out[n] = 0;
    return s;
}

static long number(const char *s, long def)
{
    s = skipws(s);
    if (!isdigit((unsigned char)*s)) return def;
    return atol(s);
}

/* ---- labels */
static int is_label(const char *l, char *name, int max)
{
    l = skipws(l);
    int n = 0;
    if (*l == ':') { l++; while (*l && !isspace((unsigned char)*l) && n < max - 1) name[n++] = (char)toupper((unsigned char)*l++); name[n] = 0; return n > 0; }
    const char *p = l;
    while (*p && (isalnum((unsigned char)*p) || *p == '_')) p++;
    if (p > l && *p == ':' && !*skipws(p + 1)) {
        while (l < p && n < max - 1) name[n++] = (char)toupper((unsigned char)*l++);
        name[n] = 0;
        return 1;
    }
    return 0;
}

static int find_label(const char *want)
{
    char name[40];
    for (int i = 0; i < nlines; i++) if (is_label(lines[i], name, sizeof name) && !strcmp(name, want)) return i;
    return -1;
}

/* ---- screen output */
static void show(const char *s, const char *color)
{
    vt_puts(&vt, color);
    vt_puts(&vt, s);
    vt_puts(&vt, "\033[0m\r\n");
    scr_cursor(vt.x, vt.y);
}

static int line_no;
static int script_error(const char *what, const char *arg)
{
    char b[120];
    snprintf(b, sizeof b, "Script error line %d: %s%s%.40s", line_no, what, arg && *arg ? " " : "", arg ? arg : "");
    show(b, "\033[1;31m");
    message("SCRIPT", b, 3000);
    return -1;
}

static int on_off(const char *s)
{
    char w[8]; word(s, w, sizeof w);
    return !strcmp(w, "ON") || !strcmp(w, "YES") || !strcmp(w, "1");
}

/* ---- one statement; returns the next line, -1 = stop, -2 = error */
static int exec(const char *l, int pc)
{
    char cmd[16], w[24], arg[LINELEN], arg2[LINELEN];
    const char *r = word(l, cmd, sizeof cmd);
    if (!cmd[0] || cmd[0] == ';') return pc + 1;

    if (!strcmp(cmd, "IF")) {
        int neg = 0;
        r = word(r, w, sizeof w);
        if (!strcmp(w, "NOT")) { neg = 1; r = word(r, w, sizeof w); }
        int cond;
        if (!strcmp(w, "SUCCESS")) cond = success;
        else if (!strcmp(w, "FAILURE")) cond = !success;
        else return script_error("IF needs SUCCESS or FAILURE, not", w), -2;
        if (neg) cond = !cond;
        if (!cond) return pc + 1;
        return exec(r, pc);
    }
    if (!strcmp(cmd, "GOTO")) {
        word(r, w, sizeof w);
        int t = find_label(w);
        if (t < 0) return script_error("no such label", w), -2;
        return t + 1;
    }
    if (!strcmp(cmd, "MESSAGE") || !strcmp(cmd, "TYPE")) {
        if (!string(r, arg, sizeof arg)) arg[0] = 0;
        show(arg, "\033[1;33m");
        success = 1;
        return pc + 1;
    }
    if (!strcmp(cmd, "CLEAR") || !strcmp(cmd, "CLS")) {
        vt_clear(&vt); scr_cursor(0, 0); success = 1;
        return pc + 1;
    }
    if (!strcmp(cmd, "BEEP") || !strcmp(cmd, "ALARM")) {
        beep(880, 150); if (cmd[0] == 'A') { beep(660, 150); beep(880, 150); }
        success = 1; return pc + 1;
    }
    if (!strcmp(cmd, "SET")) {
        r = word(r, w, sizeof w);
        if (!strcmp(w, "BAUDRATE") || !strcmp(w, "BAUD") || !strcmp(w, "SPEED")) {
            long b = number(r, 0);
            if (b < 300 || b > 115200) return script_error("bad baud rate", r), -2;
            cfg.baud = b; com_setbaud(b); status();
        } else if (!strcmp(w, "INIT") || !strcmp(w, "MODEMINIT")) {
            if (!string(r, arg, sizeof arg)) arg[0] = 0;
            snprintf(cfg.init, sizeof cfg.init, "%s", arg);
        } else if (!strcmp(w, "DOWNLOAD") || !strcmp(w, "DNLDPATH")) {
            if (!string(r, arg, sizeof arg)) arg[0] = 0;
            if (!arg[0] || !strcmp(arg, ".")) { if (!getcwd(arg, sizeof arg)) arg[0] = 0; }
            snprintf(cfg.dldir, sizeof cfg.dldir, "%s", arg);
        } else if (!strcmp(w, "AUTOZMODEM") || !strcmp(w, "ZMODEMAUTO")) cfg.autozm = on_off(r);
        else if (!strcmp(w, "ECHO") || !strcmp(w, "DUPLEX")) { cfg.echo = on_off(r); status(); }
        else if (!strcmp(w, "DIALATTEMPTS") || !strcmp(w, "REDIAL")) { long n = number(r, 1); cfg.redial_max = n < 1 ? 1 : (int)n; }
        else return script_error("unknown SET", w), -2;
        success = 1;
        return pc + 1;
    }
    if (!strcmp(cmd, "DIAL")) {
        const char *r2 = string(r, arg, sizeof arg);
        if (!r2 || !arg[0]) return script_error("DIAL needs a number", NULL), -2;
        if (!string(r2, arg2, sizeof arg2)) snprintf(arg2, sizeof arg2, "%s", arg);
        forget();
        success = dial(arg2, arg, com_baud) == 0;
        return pc + 1;
    }
    if (!strcmp(cmd, "WAITFOR")) {
        const char *r2 = string(r, arg, sizeof arg);
        if (!r2 || !arg[0]) return script_error("WAITFOR needs a string", NULL), -2;
        long secs = number(r2, 30);
        uint32_t t0 = TICKS(), seen = rx_seq - 1;
        success = 0;
        for (;;) {
            if (rx_seq != seen) { seen = rx_seq; if (matched(arg)) { success = 1; break; } }
            if (TICKS() - t0 > ms2ticks((uint32_t)secs * 1000u)) break;
            if (check_esc()) return -1;
            tick();
        }
        return pc + 1;
    }
    if (!strcmp(cmd, "TRANSMIT") || !strcmp(cmd, "SEND")) {
        if (!string(r, arg, sizeof arg)) return script_error("TRANSMIT needs a string", NULL), -2;
        forget();                                   /* WAITFOR looks at the answer to this */
        for (const char *p = arg; *p; p++) {
            com_putc((unsigned char)*p);
            if (cfg.echo) vt_putc(&vt, (unsigned char)*p);
            uint32_t t0 = TICKS();                  /* a little pacing, like a typist */
            while (TICKS() - t0 < 1) tick();
        }
        success = 1;
        return pc + 1;
    }
    if (!strcmp(cmd, "PAUSE") || !strcmp(cmd, "DELAY")) {
        long secs = number(r, 1);
        uint32_t t0 = TICKS();
        while (TICKS() - t0 < ms2ticks((uint32_t)secs * 1000u)) { if (check_esc()) return -1; tick(); }
        success = 1;
        return pc + 1;
    }
    if (!strcmp(cmd, "DOWNLOAD") || !strcmp(cmd, "GETFILE")) {
        word(r, w, sizeof w);
        if (w[0] && strcmp(w, "ZMODEM")) return script_error("only ZMODEM downloads, not", w), -2;
        int rc;
        if (xfer_auto_pending) rc = xfer_auto_rc;        /* it already started by itself */
        else rc = do_download('Z');
        xfer_auto_pending = 0;
        success = rc == 0;
        return pc + 1;
    }
    if (!strcmp(cmd, "UPLOAD") || !strcmp(cmd, "SENDFILE")) {
        r = word(r, w, sizeof w);
        if (strcmp(w, "ZMODEM")) return script_error("only ZMODEM uploads, not", w), -2;
        if (!string(r, arg, sizeof arg) || !arg[0]) return script_error("UPLOAD needs a file name", NULL), -2;
        if (access(arg, 0) != 0) { success = 0; show("File not found", "\033[1;31m"); return pc + 1; }
        char *list[1] = { arg };
        success = send_files('Z', list, 1) == 0;
        return pc + 1;
    }
    if (!strcmp(cmd, "HANGUP")) {
        hangup(); success = !online;
        return pc + 1;
    }
    if (!strcmp(cmd, "EXIT") || !strcmp(cmd, "QUIT")) {
        long code = number(r, 0);
        script_running = 0;
        term_exit((int)code);
        return -1;
    }
    if (!strcmp(cmd, "END")) return -1;
    return script_error("unknown command", cmd), -2;
}

void script_run(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) { message("SCRIPT", "Can't open the script file", 2000); return; }
    lines = malloc(sizeof(*lines) * MAXLINES);
    if (!lines) { fclose(f); return; }
    nlines = 0;
    while (nlines < MAXLINES && fgets(lines[nlines], LINELEN, f)) {
        char *nl = strpbrk(lines[nlines], "\r\n\x1A"); if (nl) *nl = 0;
        nlines++;
    }
    fclose(f);
    const char *base = strrchr(path, '\\'); base = base ? base + 1 : path;
    snprintf(script_name, sizeof script_name, "%s", base);
    script_running = 1; aborted = 0; success = 1;
    status();
    int pc = 0, steps = 0;
    while (pc >= 0 && pc < nlines) {
        char lab[40];
        line_no = pc + 1;
        if (is_label(lines[pc], lab, sizeof lab)) { pc++; continue; }
        pc = exec(lines[pc], pc);
        if (aborted) { show("Script stopped.", "\033[1;31m"); script_stopped = 1; break; }
        if (pc == -2) script_stopped = 1;
        if ((++steps & 15) == 0) tick();                  /* a loop of GOTOs still lets the screen live */
    }
    script_running = 0;
    free(lines); lines = NULL;
    status();
}
