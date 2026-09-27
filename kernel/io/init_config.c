/*
 * init_config.c - CONFIG.SYS (BIOS/SYSCONF.ASM, SYSINIT2.ASM) and the
 * hand-over to the shell.
 *
 * Keywords (DOS 4.00's table): BUFFERS BREAK DEVICE FILES FCBS LASTDRIVE
 * MULTITRACK DRIVPARM STACKS COUNTRY SHELL INSTALL IFS CPSW COMMENT REM
 * SWITCHES.  The file is upper-cased first; a keyword must be followed by a
 * delimiter; blanks, tabs and '=' separate it from its value.  Errors are
 * reported with DOS 4's messages and the line number.
 */
#include "init.h"

#define CFG_TEXT    0x80000u            /* CONFIG.SYS text (< 64 KB) */
#define CFG_MAX     0xFFFFu

static struct dosconfig cfg;
static uint32_t first_mcb, memptr;
static int next_drive;
static int lineno;
static char shell_path[80];
static char shell_args[128];
static int shell_given;
static uint8_t comment_char;

struct install { char path[80]; char args[128]; int line; };
#define MAXINSTALL 8
static struct install installs[MAXINSTALL];
static int ninstall;

static int dos21(struct armregs *r) { return svc21(r); }

static void err_line(void) { sys_printf("Error in CONFIG.SYS line %d\r\n", lineno); }

static void unrecognized(void)
{
    sys_puts("\r\nUnrecognized command in CONFIG.SYS\r\n");
    err_line();
}

static const char *cur_val;         /* where the keyword's value starts */

/* "Bad command or parameters - " echoes the line from where the parser
   stopped (the value, or nothing if the value itself parsed) */
static void badparm_at(const char *pos)
{
    char buf[140];
    int n = 0;
    for (const char *p = pos; *p && n < 128; p++) buf[n++] = *p;
    buf[n] = 0;
    sys_printf("\r\nBad command or parameters - %s\r\r\n", buf);
    err_line();
}
static void badparm(void) { badparm_at(cur_val); }

static int is_delim(char c) { return c == ' ' || c == '\t' || c == '=' || c == ',' || c == ';' || c == 0; }
static const char *skip_blanks(const char *p) { while (*p == ' ' || *p == '\t') p++; return p; }
static const char *skip_seps(const char *p) { while (*p == ' ' || *p == '\t' || *p == '=') p++; return p; }

/* decimal number; returns end or 0 */
static const char *number(const char *p, unsigned *v)
{
    unsigned n = 0;
    if (*p < '0' || *p > '9') return 0;
    while (*p >= '0' && *p <= '9') { n = n * 10 + (*p - '0'); if (n > 100000) return 0; p++; }
    *v = n;
    return p;
}

static int at_end(const char *p) { p = skip_blanks(p); return *p == 0; }

/* -------------------------------------------------- simple keywords */

static void do_files(const char *p)
{
    unsigned n;
    if (!(p = number(p, &n)) || !at_end(p) || n < 8 || n > 255) { badparm(); return; }
    cfg.files = n;
}

static void do_buffers(const char *p)
{
    unsigned n, m = 0;
    if (!(p = number(p, &n))) { badparm(); return; }
    p = skip_blanks(p);
    if (*p == ',') {
        p = skip_blanks(p + 1);
        if (!(p = number(p, &m)) || m > 8) { badparm(); return; }
        p = skip_blanks(p);
    }
    int x = 0;
    if (*p == '/' && p[1] == 'X') { x = 1; p += 2; }
    if (!at_end(p) || n < 1 || n > 10000) { badparm(); return; }
    if (n > 99 && !x) { badparm_at(""); return; }
    if (n > 99) n = 99;                 /* no expanded memory to put them in */
    cfg.buffers = n;
    cfg.lookahead = m;
}

static void do_fcbs(const char *p)
{
    unsigned m, n = 0;
    if (!(p = number(p, &m))) { badparm(); return; }
    p = skip_blanks(p);
    if (*p == ',') {
        p = skip_blanks(p + 1);
        if (!(p = number(p, &n))) { badparm(); return; }
    }
    if (!at_end(p) || m < 1 || m > 255 || n > m) { badparm(); return; }
    cfg.fcbs = m;
    cfg.fcbs_keep = n;
}

static void do_lastdrive(const char *p)
{
    char c = *p;
    if (c < 'A' || c > 'Z') { badparm(); return; }
    p = skip_blanks(p + 1);
    if (*p == ':') p = skip_blanks(p + 1);
    if (!at_end(p)) { badparm(); return; }
    cfg.lastdrive = c - 'A' + 1;
}

static int onoff(const char *p)
{
    if (!strncmp(p, "ON", 2) && at_end(p + 2)) return 1;
    if (!strncmp(p, "OFF", 3) && at_end(p + 3)) return 0;
    return -1;
}

static void do_break(const char *p)
{
    int v = onoff(p);
    if (v < 0) { badparm(); return; }
    cfg.brk = v;
}

static void do_stacks(const char *p)
{
    unsigned n, s;
    const char *q = number(p, &n);
    int ok = 0;
    if (q) {
        q = skip_blanks(q);
        if (*q == ',') {
            q = skip_blanks(q + 1);
            if ((q = number(q, &s)) && at_end(q))
                ok = (n == 0 && s == 0) || (n >= 8 && n <= 64 && s >= 32 && s <= 512);
        }
    }
    if (!ok) {
        sys_puts("\r\nInvalid STACK parameters\r\n");
        err_line();
    }
    /* accepted: ARM-DOS's interrupt handlers run on the SVC stack */
}

/* COUNTRY=ccc[,[cp][,path]]: the tables of COUNTRY.SYS for that country and
   code page (DOS 4.00 SYSCONF.ASM, SYSINIT2.ASM SetDOSCountryInfo). The path
   goes to DOS for NLSFUNC before the file is read, as DOS 4 does. Default
   \COUNTRY.SYS on the boot drive; ARM-DOS keeps COUNTRY.SYS in \DOS and looks
   there when the root has none. */
static uint16_t rd16(const uint8_t *p) { return p[0] | (p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

static int f_seekread(int h, uint32_t off, void *buf, unsigned n)
{
    struct armregs r = { 0 };
    r.r0 = 0x4200; r.r1 = h; r.r2 = off >> 16; r.r3 = off & 0xFFFF;
    if (dos21(&r)) return -1;
    memset(&r, 0, sizeof r);
    r.r0 = 0x3F00; r.r1 = h; r.r2 = n; r.r3 = (uint32_t)buf;
    if (dos21(&r) || (r.r0 & 0xFFFF) != n) return -1;
    return 0;
}

static int f_open(const char *path)
{
    struct armregs r = { 0 };
    r.r0 = 0x3D00; r.r3 = (uint32_t)path;
    if (dos21(&r)) return -1;
    return r.r0 & 0xFFFF;
}

/* one table of a country's data: FFh, 7-character name, length, data */
static int nls_table(int h, uint32_t off, uint8_t *dest, unsigned max)
{
    uint8_t th[10];
    if (f_seekread(h, off, th, 10) || th[0] != 0xFF) return -1;
    unsigned len = rd16(th + 8);
    if (len > max) return -1;
    return f_seekread(h, off + 10, dest, len) ? -1 : (int)len;
}

static void do_country(const char *p)
{
    unsigned c, cp = 0;
    char path[64];
    path[0] = 0;
    const char *q = number(p, &c);
    if (!q) { badparm(); return; }
    q = skip_blanks(q);
    if (*q == ',') {
        q = skip_blanks(q + 1);
        if (*q >= '0' && *q <= '9') { q = number(q, &cp); if (!q) { badparm(); return; } }
        q = skip_blanks(q);
        if (*q == ',') {
            q = skip_blanks(q + 1);
            int n = 0;
            while (*q && *q != ' ' && *q != '\t' && n < 63) path[n++] = *q++;
            path[n] = 0;
        }
    }
    if (c < 1 || c > 999 || cp > 999 || !at_end(q)) { badparm(); return; }
    struct nls_state *nls = S.api->nls;
    const char *name = path[0] ? path : "\\COUNTRY.SYS";
    int h = f_open(name);
    static char alt[] = "?:\\DOS\\COUNTRY.SYS";
    if (h < 0 && !path[0]) {
        alt[0] = 'A' + S.bootunit;
        if ((h = f_open(alt)) >= 0) name = alt;
    }
    if (h < 0) {
        sys_printf("\r\nBad or missing %s\r\n", path[0] ? path : "\\COUNTRY.SYS");
        err_line();
        return;
    }
    strcpy(nls->path, name);
    uint8_t hd[0x17], e[14];
    int ok = 0;
    if (!f_seekread(h, 0, hd, sizeof hd) && hd[0] == 0xFF && !memcmp(hd + 1, "COUNTRY", 7)) {
        uint32_t list = rd32(hd + 0x13);
        uint8_t cnt[2];
        if (!f_seekread(h, list, cnt, 2)) {
            for (unsigned i = 0; i < rd16(cnt); i++) {
                if (f_seekread(h, list + 2 + 14 * i, e, 14)) break;
                if (rd16(e + 2) == c && (!cp || rd16(e + 4) == cp)) { ok = 1; break; }
            }
        }
    }
    if (ok) {
        /* the data items: subfunction 1 country info, 2 upper case, 5 file
           characters, 6 collating sequence (4 = file upper case: the same) */
        uint32_t d = rd32(e + 10);
        uint8_t n2[2], it[8];
        static uint8_t cinfo[38];
        int got = 0;
        if (!f_seekread(h, d, n2, 2)) {
            for (unsigned i = 0; i < rd16(n2); i++) {
                if (f_seekread(h, d + 2 + 8 * i, it, 8)) break;
                uint32_t t = rd32(it + 4);
                switch (it[2]) {
                case 1: if (nls_table(h, t, cinfo, sizeof cinfo) >= 38) got |= 1; break;
                case 2: if (nls_table(h, t, nls->ucase + 2, 128) == 128) got |= 2; break;
                case 5: { int n = nls_table(h, t, nls->fchar + 2, sizeof nls->fchar - 2); if (n > 0) { nls->fchar[0] = n; nls->fchar[1] = 0; } break; }
                case 6: if (nls_table(h, t, nls->collate + 2, 256) == 256) got |= 4; break;
                }
            }
        }
        if (got & 1) {
            memcpy(nls->info, cinfo + 4, 34);
            nls->info[0x12] = nls->info[0x13] = nls->info[0x14] = nls->info[0x15] = 0;
        }
        nls->country = c;
        nls->cp = nls->syscp = rd16(e + 4);
    }
    struct armregs r = { 0 };
    r.r0 = 0x3E00; r.r1 = h;
    dos21(&r);
    if (!ok) {
        sys_puts("\r\nInvalid country code or code page\r\n");
        err_line();
    }
}

static void do_switches(const char *p)
{
    if (!strncmp(p, "/K", 2) && at_end(p + 2)) { con_init(0); return; }
    badparm();
}

/* "path args" -> path (no blanks) and args (rest, leading blank kept) */
static int split_cmd(const char *p, char *path, int pmax, char *args, int amax)
{
    int n = 0;
    while (*p && *p != ' ' && *p != '\t' && n < pmax - 1) path[n++] = *p++;
    path[n] = 0;
    n = 0;
    while (*p && n < amax - 1) args[n++] = *p++;
    args[n] = 0;
    return path[0] != 0;
}

/* ------------------------------------------------------------ DEVICE= */

static void base_name8(const char *path, char *out)
{
    const char *b = path;
    for (const char *p = path; *p; p++) if (*p == '\\' || *p == '/' || *p == ':') b = p + 1;
    int i = 0;
    for (; *b && *b != '.' && i < 8; b++) out[i++] = *b;
    for (; i < 8; i++) out[i] = ' ';
}

static int read_file(const char *path, uint8_t *dest, uint32_t max, uint32_t *size)
{
    struct armregs r = { 0 };
    r.r0 = 0x3D00;
    r.r3 = (uint32_t)path;
    if (dos21(&r)) return -1;
    int h = r.r0 & 0xFFFF;
    uint32_t got = 0;
    for (;;) {
        memset(&r, 0, sizeof r);
        r.r0 = 0x3F00; r.r1 = h;
        r.r2 = max - got > 0x8000 ? 0x8000 : max - got;
        r.r3 = (uint32_t)(dest + got);
        if (dos21(&r)) break;
        uint32_t n = r.r0 & 0xFFFF;
        got += n;
        if (!n || got >= max) break;
    }
    memset(&r, 0, sizeof r);
    r.r0 = 0x3E00; r.r1 = h;
    dos21(&r);
    *size = got;
    return 0;
}

static void do_device(const char *p)
{
    char path[80], args[128];
    static char cmdline[200];
    if (!split_cmd(p, path, sizeof path, args, sizeof args)) { badparm(); return; }
    uint32_t size;
    uint8_t *tmp = (uint8_t *)SYSINIT_TEMP;
    uint32_t mark = memptr, base = memptr + 16, end;
    uint32_t entry;
    if (read_file(path, tmp, CFG_TEXT - SYSINIT_TEMP, &size) ||
        !(entry = ar1_place(tmp, size, base, SYSINIT_BASE, &end))) {
        sys_printf("\r\nBad or missing %s\r\n", path);
        err_line();
        return;
    }
    (void)entry;
    ksnprintf(cmdline, sizeof cmdline, "%s%s\r\n", path, args);

    uint32_t top = base;
    int installed = 0;
    struct devhdr *d = (struct devhdr *)base;
    for (int guard = 0; guard < 16; guard++) {
        struct devhdr *next = d->next;
        struct req_init q;
        memset(&q, 0, sizeof q);
        q.h.len = sizeof q;
        q.h.cmd = CMD_INIT;
        q.brk = SYSINIT_BASE;
        q.arg = (uint32_t)cmdline;
        q.drive = next_drive;
        d->strategy(&q.h);
        d->interrupt();
        int ok = !(q.h.status & RS_ERROR) && q.brk > base && q.brk <= SYSINIT_BASE;
        if (!(d->attr & DEVA_CHAR) && q.units == 0) ok = 0;
        if (ok) {
            if (d->attr & DEVA_CHAR) S.api->add_chardev(d);
            else {
                int toomany = 0;
                int first = S.api->add_blockdev(d, q.units, (struct bpb **)q.arg, &toomany);
                if (first < 0) {
                    sys_puts(toomany ? "\r\nToo many block devices\r\n" : "\r\nBad or missing ");
                    ok = 0;
                } else next_drive = first + q.units;
            }
        } else if (q.cfgerr) err_line();
        if (ok) { installed = 1; if (q.brk > top) top = q.brk; }
        if (next == DEV_END || next == 0 || (uint32_t)next < base || (uint32_t)next >= end) break;
        d = next;
    }
    if (!installed) return;
    top = (top + 15) & ~15u;
    struct devmark *m = (struct devmark *)mark;
    m->id = 'D';
    m->seg = (mark >> 4) + 1;
    m->size = (top - base) >> 4;
    memset(m->res, 0, 3);
    base_name8(path, m->name);
    memptr = top;
}

/* --------------------------------------------------------------- EXEC */

static int parse_fcb(const char *s, uint8_t *fcb, const char **rest)
{
    struct armregs r = { 0 };
    memset(fcb, 0, 16);
    memset(fcb + 1, ' ', 11);
    r.r0 = 0x2901;
    r.r4 = (uint32_t)s;
    r.r5 = (uint32_t)fcb;
    dos21(&r);
    *rest = (const char *)r.r4;
    return 0;
}

static int exec_prog(const char *path, const char *args, uint16_t envseg, int fcbdrive)
{
    static uint8_t tail[130];
    static uint8_t fcb1[20], fcb2[20];
    static struct { uint16_t env; uint32_t tail, fcb1, fcb2; } PACKED pb;
    int n = strlen(args);
    if (n > 126) n = 126;
    tail[0] = n;
    memcpy(tail + 1, args, n);
    tail[n + 1] = '\r';
    const char *rest;
    parse_fcb(args, fcb1, &rest);
    parse_fcb(rest, fcb2, &rest);
    if (fcbdrive) fcb1[0] = fcbdrive;
    pb.env = envseg;
    pb.tail = (uint32_t)tail;
    pb.fcb1 = (uint32_t)fcb1;
    pb.fcb2 = (uint32_t)fcb2;
    struct armregs r = { 0 };
    r.r0 = 0x4B00;
    r.r1 = (uint32_t)&pb;
    r.r3 = (uint32_t)path;
    return dos21(&r) ? -1 : 0;
}

/* -------------------------------------------------------- the parser */

typedef void (*kwfn)(const char *);
static void do_nothing(const char *p) { (void)p; }
static void do_shell(const char *p)
{
    if (!split_cmd(p, shell_path, sizeof shell_path, shell_args, sizeof shell_args)) { badparm(); return; }
    shell_given = 1;
}
static void do_install(const char *p)
{
    if (ninstall >= MAXINSTALL) return;
    struct install *in = &installs[ninstall];
    if (!split_cmd(p, in->path, sizeof in->path, in->args, sizeof in->args)) { badparm(); return; }
    in->line = lineno;
    ninstall++;
}
static void do_onoff(const char *p) { if (onoff(p) < 0) badparm(); }
static void do_comment(const char *p) { comment_char = *p; }

static const struct { const char *kw; kwfn fn; } keywords[] = {
    { "BUFFERS", do_buffers }, { "BREAK", do_break }, { "DEVICE", do_device },
    { "FILES", do_files }, { "FCBS", do_fcbs }, { "LASTDRIVE", do_lastdrive },
    { "MULTITRACK", do_onoff }, { "DRIVPARM", do_nothing }, { "STACKS", do_stacks },
    { "COUNTRY", do_country }, { "SHELL", do_shell }, { "INSTALL", do_install },
    { "IFS", do_nothing }, { "CPSW", do_onoff }, { "COMMENT", do_comment },
    { "REM", do_nothing }, { "SWITCHES", do_switches },
};

static void process_line(char *line)
{
    const char *p = line;
    while (*p && (uint8_t)*p <= ' ') p++;
    if (!*p) return;
    if (comment_char && *p == (char)comment_char) return;
    char kw[16];
    int n = 0;
    while (*p >= 'A' && *p <= 'Z' && n < 15) kw[n++] = *p++;
    kw[n] = 0;
    if (!n) { unrecognized(); return; }
    if (!is_delim(*p) && !(n == 3 && !strcmp(kw, "REM"))) { unrecognized(); return; }
    for (unsigned i = 0; i < sizeof keywords / sizeof keywords[0]; i++) {
        if (!strcmp(kw, keywords[i].kw)) {
            if (keywords[i].fn == do_nothing) return;
            p = skip_seps(p);
            cur_val = p;
            keywords[i].fn(p);
            return;
        }
    }
    unrecognized();
}

void sysinit_config(void)
{
    const struct dosapi *api = S.api;
    char drv = 'A' + S.bootunit;

    /* the ARM-PC system board's keyboard layout / code page mailbox (ports
       F6h/F7h, ARCH.md 4.6): at boot nothing is loaded - US, code page 437 */
    *(volatile uint8_t *)(0x10000000 + 0xF6) = 0;
    *(volatile uint8_t *)(0x10000000 + 0xF7) = 0;

    cfg.files = 8;
    cfg.fcbs = 4;
    cfg.fcbs_keep = 0;
    cfg.buffers = 15;                   /* DOS 4's default for a 640 KB machine */
    cfg.lookahead = 0;
    cfg.lastdrive = 5;                  /* E: */
    cfg.brk = 0;

    first_mcb = (S.dos_end + 15) & ~15u;
    memptr = first_mcb + 16;
    next_drive = S.nunits;

    /* current drive = boot drive */
    struct armregs r = { 0 };
    r.r0 = 0x0E00;
    r.r3 = S.bootunit;
    dos21(&r);

    static char cfgname[] = "?:\\CONFIG.SYS";
    cfgname[0] = drv;
    char *text = (char *)CFG_TEXT;
    uint32_t size = 0;
    if (read_file(cfgname, (uint8_t *)text, CFG_MAX, &size) == 0) {
        text[size] = 0;
        for (uint32_t i = 0; i < size; i++) {
            if (text[i] == 0x1A) { text[i] = 0; size = i; break; }
            if (text[i] >= 'a' && text[i] <= 'z') text[i] -= 32;
        }
        char *line = text;
        for (lineno = 1; line < text + size; lineno++) {
            char *e = line;
            while (*e && *e != '\n') e++;
            int last = *e == 0;
            *e = 0;
            if (e > line && e[-1] == '\r') e[-1] = 0;
            process_line(line);
            if (last) break;
            line = e + 1;
        }
    }

    if (cfg.lastdrive < next_drive) cfg.lastdrive = next_drive;
    api->set_break(cfg.brk);
    uint32_t end = api->build_tables(&cfg, memptr);
    end = (end + 15) & ~15u;
    api->make_arena(first_mcb >> 4, (end - first_mcb - 16) >> 4, SYSINIT_BASE >> 4);

    /* bind handles 0-4 to the final CON (ANSI.SYS may have replaced it) */
    sys_reopen_std();

    for (int i = 0; i < ninstall; i++) {
        lineno = installs[i].line;
        if (exec_prog(installs[i].path, installs[i].args, 0, 0)) {
            sys_printf("\r\nBad or missing %s\r\n", installs[i].path);
            err_line();
        }
    }

    /* the shell */
    if (!shell_given) {
        ksnprintf(shell_path, sizeof shell_path, "%c:\\COMMAND.COM", drv);
        strcpy(shell_args, " /P");
    }
    api->set_shell(shell_path, shell_args);
    api->extend_arena(0xA000);
    /* DOS 4 (SYSINIT1.ASM): no environment - the shell builds its own
       PATH= and COMSPEC= - and the boot drive in FCB1 */
    exec_prog(shell_path, shell_args, 0, S.bootunit + 1);

    sys_puts("\r\nBad or missing Command Interpreter\r\n");
    sys_halt();
}
