/*
 * env.c - the environment block (TENV.ASM, INIT.ASM), SET, PATH, PROMPT
 * and the prompt itself (TCMD2A.ASM PRINT_PROMPT).
 */
#include "cmd.h"


static char *envp(void) { return (char *)ARMDOS_SEG2PTR(env_seg); }

/* GETENVSIZ: size of the block from its MCB */
static unsigned envsize(void)
{
    struct mcb *m = (struct mcb *)ARMDOS_SEG2PTR(env_seg - 1);
    return (unsigned)m->size << 4;
}

void env_init(unsigned envsiz, int build)
{
    unsigned largest = 0;
    char *old = mypsp->envseg && !build ? (char *)ARMDOS_SEG2PTR(mypsp->envseg) : 0;

    dos_alloc(0xFFFF, &largest);
    unsigned envmax = largest > 128 + 16 ? largest - 128 : largest;
    if (envmax > 4095) envmax = 4095;
    unsigned seg = dos_alloc(envmax, 0);
    if (!seg) {
        /* nothing at all: keep the parent's block */
        env_seg = mypsp->envseg;
        return;
    }
    char *d = (char *)ARMDOS_SEG2PTR(seg);
    unsigned left = envmax * 16 - 1;
    unsigned used = 0;
    int err = 0;

    if (old) {
        for (const char *p = old; *p;) {
            unsigned l = strlen(p) + 1;
            if (l > left) {
                err = 1;
                break;
            }
            memcpy(d, p, l);
            d += l;
            used += l;
            left -= l;
            p += l;
        }
    }
    *d = 0;
    if (err) out(1, msg(M_OUTENVERR));
    unsigned paras = (used + 16) >> 4;
    if (paras < envsiz) paras = envsiz;
    dos_setblock(seg, paras, 0);
    env_seg = seg;
    mypsp->envseg = seg;
}

/* FIND: the string "NAME=..." (name compared upper-cased, up to the '=') */
static char *find(const char *name, char **start)
{
    char *e = envp();
    int n = 0;
    while (name[n] && name[n] != '=') n++;
    n++;                                    /* include the '=' */
    while (*e) {
        int i;
        for (i = 0; i < n; i++) {
            int c = name[i] == '=' ? '=' : upconv(name[i]);
            if (c != (uint8_t)e[i]) break;
        }
        if (i == n) {
            if (start) *start = e;
            return e + n;
        }
        e += strlen(e) + 1;
    }
    return 0;
}

char *env_find(const char *name)
{
    if (!env_seg) return 0;
    return find(name, 0);
}

void env_delete(const char *name)
{
    char *s;
    if (!find(name, &s)) return;
    char *next = s + strlen(s) + 1;
    char *end = env_end();
    memmove(s, next, end - next + 2);
}

/* SCAN_DOUBLE_NULL: where the next string goes */
char *env_end(void)
{
    char *e = envp();
    while (*e) e += strlen(e) + 1;
    return e;
}

/* STORE_CHAR: add one byte, keeping the double NUL; grow the block up to
 * 32 KB, else "Out of environment space" */
void env_store(char **dp, int c)
{
    char *base = envp();
    unsigned size = envsize();
    unsigned off = *dp - base;
    if (off >= size - 2) {
        unsigned bx = size;
        if (bx >= 0x8000 || dos_setblock(env_seg, (bx >> 4) + 1, 0))
            cerror_msg(M_ENVERR);
    }
    **dp = c;
    (*dp)++;
    (*dp)[0] = 0;
    (*dp)[1] = 0;
}

/* MOVE_NAME: the name, upper-cased, through its '=' */
static const char *move_name(char **dp, const char *s)
{
    while (*s != '\r') {
        int c = upconv((uint8_t)*s++);
        env_store(dp, c);
        if (c == '=') break;
    }
    return s;
}

/* GETARG: the first non-delimiter of the tail (NULL if none) */
static const char *getarg(void)
{
    if (TAIL[0] == 0) return 0;
    const char *s = scanoff((const char *)TAIL + 1);
    if (*s == '\r') return 0;
    return s;
}

/* -------------------------------------------------------------- SET -- */

void c_set(void)
{
    const char *s = getarg();
    if (!s) {
        /* DISP_ENV */
        const char *e = envp();
        while (*e) {
            printf_crlf(1, e);
            e += strlen(e) + 1;
        }
        return;
    }
    int eq = 0, empty = 0;
    for (const char *p = s; *p != '\r'; p++) {
        if (*p == '=') {
            eq++;
            if (p[1] == '\r') empty = 1;
        }
    }
    if (eq != 1) cerror_msg(M_SYNTMES);
    /* the name through '=' */
    char name[COMBUFLEN];
    int i = 0;
    while (s[i] != '=') { name[i] = s[i]; i++; }
    name[i] = '=';
    name[i + 1] = 0;
    env_delete(name);
    if (empty) return;
    char *d = env_end();
    char *namestart = d;
    s = move_name(&d, s);
    int is_comspec = !memcmp(namestart, "COMSPEC=", 8);
    const char *v = s;
    while (*s != '\r') env_store(&d, (uint8_t)*s++);
    if (is_comspec) {
        v = scanoff(v);
        int n = 0;
        while (v[n] != '\r' && !is_delim((uint8_t)v[n]) && n < 79) {
            comspec[n] = v[n];
            n++;
        }
        comspec[n] = 0;
    }
}

/* ----------------------------------------------------------- PROMPT -- */

void c_prompt(void)
{
    env_delete("PROMPT=");
    const char *s = getarg();
    if (!s) return;
    char *d = env_end();
    const char *n = "PROMPT=\r";
    move_name(&d, n);
    while (*s != '\r') env_store(&d, (uint8_t)*s++);
}

/* ------------------------------------------------------------- PATH -- */

void c_path(void)
{
    char buf[COMBUFLEN + 2];
    int n = 0;
    const char *s = 0;
    if (TAIL[0]) {
        s = (const char *)TAIL + 1;
        while (is_delim((uint8_t)*s) && *s != ';') s++;
        if (*s == '\r') s = 0;
    }
    if (!s) {
        /* disppath */
        char *v = env_find("PATH=");
        if (!v || !*v) {
            std_printf(M_NULPATH);
        } else {
            out(1, v - 5);
        }
        crlf2();
        return;
    }
    if (*s == ';') {
        s++;
    } else {
        for (;;) {
            int c = (uint8_t)*s++;
            if (c == '\r') { s--; break; }
            c = upconv(c);
            if (c != ';' && is_delim(c)) break;
            if (n < COMBUFLEN) buf[n++] = c;
        }
    }
    /* scan_white */
    for (;;) {
        int c = (uint8_t)*s++;
        if (c == '\r') break;
        if (c == ' ' || c == '\t') continue;
        cerror_parse(P_TOOMANY, NULL);
    }
    buf[n] = 0;
    env_delete("PATH=");
    char *d = env_end();
    move_name(&d, "PATH=\r");
    for (int i = 0; i < n; i++) env_store(&d, (uint8_t)buf[i]);
}

/* ----------------------------------------------------------- prompt -- */

static void print_char(int c) { dos_putc(c); }

void print_version(void)
{
    REGS r = {0};
    char maj[4], min[4];
    r.r0 = 0x3000;
    int21(&r);
    fmt_uint(maj, r.r0 & 0xFF, 1, '0');
    fmt_uint(min, (r.r0 >> 8) & 0xFF, 2, '0');
    msgout(1, M_VERMES, maj, min, 0);
}

void print_date(void)
{
    int y, m, d, wd;
    char day[4], date[16];
    dos_getdate(&y, &m, &d, &wd);
    memcpy(day, msg(M_WEEKTAB) + 3 * wd, 3);
    day[3] = 0;
    fmt_date(date, y, m, d, 1);
    msgout(1, 1075, day, date, 0);
}

void print_time(void)
{
    int h, m, s, hs;
    char t[16], r[16];
    dos_gettime(&h, &m, &s, &hs);
    fmt_time(t, h, m, s, hs, 4, 0);
    int l = strlen(t), i = 0;
    while (l + i < 11) r[i++] = ' ';
    strcpy(r + i, t);
    msgout(1, 1076, r, 0, 0);
}

static void build_dir_for_prompt(void)
{
    char buf[80];
    buf[0] = curdrv + 'A';
    buf[1] = ':';
    buf[2] = dirchar;
    if (dos_curdir(0, buf + 3)) {
        std_printf(M_BADCURDRV);
        return;
    }
    out(1, buf);
}

void print_prompt(void)
{
    const char *p = env_find("PROMPT=");
    if (!p || !*p) {
        print_char(dos_getdrv() + 'A');
        print_char('>');
        return;
    }
    for (;;) {
        int c = (uint8_t)*p++;
        if (!c) return;
        if (c != '$') {
            print_char(c);
            continue;
        }
        c = (uint8_t)*p++;
        if (!c) return;
        switch (upconv(c)) {
        case 'B': print_char('|'); break;
        case 'D': print_date(); break;
        case 'E': print_char(0x1B); break;
        case 'G': print_char('>'); break;
        case 'H': std_printf(M_DBACK); break;
        case 'L': print_char('<'); break;
        case 'N': print_char(dos_getdrv() + 'A'); break;
        case 'P': build_dir_for_prompt(); break;
        case 'Q': print_char('='); break;
        case 'T': print_time(); break;
        case 'V': print_version(); break;
        case '_': crlf2(); break;
        case '$': print_char('$'); break;
        default: break;
        }
    }
}
