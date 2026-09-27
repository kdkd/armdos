/*
 * eggs.c - ARM-DOS's hidden internal commands (not in DOS 4.00; not files,
 * so DIR never shows them).  Each prints a short message on stdout and sets
 * ERRORLEVEL 0; two of them then run a real command.  Spoilers: README.md.
 */
#include "cmd.h"

extern __attribute__((noreturn)) void jump_docom1(void);

static void egg(int n)
{
    std_printf(n);
    retcode = 0;
}

void e_msd(void) { egg(1910); }
void e_win(void) { egg(1911); }
void e_deltree(void) { egg(1912); }
void e_memmaker(void) { egg(1913); }
void e_intel(void) { egg(1914); }
void e_xyzzy(void) { egg(1915); }
void e_plugh(void) { egg(1916); }
void e_iddqd(void) { egg(1917); }
void e_idkfa(void) { egg(1918); }
void e_hal(void) { egg(1919); }
void e_42(void) { egg(1921); }
void e_sudo(void) { egg(1922); }

/* LS: DIR with the same arguments */
void e_ls(void)
{
    egg(1923);
    c_dir();
}

/* ELIZA: DOCTOR, found on the PATH like any program */
void e_eliza(void)
{
    egg(1920);
    const char *t = (const char *)TAIL + 1;
    char line[COMBUFLEN];
    int n = 0;
    const char *d = "DOCTOR";
    while (*d) line[n++] = *d++;
    while (*t != '\r' && n < COMBUFLEN - 2) line[n++] = *t++;
    memcpy(combuf + 2, line, n);
    combuf[2 + n] = '\r';
    combuf[1] = n;
    jump_docom1();
}

/* UNAME: the GNU options, the ARM/AT's answers */
static const struct { char c; const char *lng; const char *val; } uname_f[] = {
    { 's', "kernel-name", "ARM-DOS" },
    { 'n', "nodename", "ARMAT" },
    { 'r', "kernel-release", "4.00" },
    { 'v', "kernel-version", "4.00-1988.06.17" },
    { 'm', "machine", "armv5tel" },
    { 'p', "processor", "ARM926EJ-S" },
    { 'i', "hardware-platform", "ARM/AT" },
    { 'o', "operating-system", "ARM-DOS" },
};
#define NUF (int)(sizeof uname_f / sizeof uname_f[0])

static void uname_err(int n, const char *s)
{
    msgout(2, n, s, 0, 0);
    msgout(2, 1933, 0, 0, 0);
    retcode = 1;
}

void e_uname(void)
{
    const char *p = (const char *)TAIL + 1;
    unsigned want = 0;
    retcode = 0;
    for (;;) {
        char tok[COMBUFLEN];
        int n = 0;
        while (is_delim((uint8_t)*p)) p++;
        if (*p == '\r') break;
        while (*p != '\r' && !is_delim((uint8_t)*p) && n < COMBUFLEN - 1) tok[n++] = *p++;
        tok[n] = 0;
        for (int i = 0; i < n; i++) tok[i] = tok[i] >= 'A' && tok[i] <= 'Z' ? tok[i] + 32 : tok[i];
        if (tok[0] == '-' && tok[1] == '-') {
            const char *l = tok + 2;
            if (!strcmp(l, "help")) { std_printf(1934); return; }
            if (!strcmp(l, "version")) { std_printf(1935); return; }
            if (!strcmp(l, "all")) { want = ~0u; continue; }
            int i;
            for (i = 0; i < NUF; i++) if (!strcmp(l, uname_f[i].lng)) break;
            if (i == NUF) { uname_err(1931, tok); return; }
            want |= 1u << i;
        } else if (tok[0] == '-' && tok[1]) {
            for (const char *c = tok + 1; *c; c++) {
                int i;
                if (*c == 'a') { want = ~0u; continue; }
                for (i = 0; i < NUF; i++) if (uname_f[i].c == *c) break;
                if (i == NUF) {
                    char b[2] = { *c, 0 };
                    uname_err(1930, b);
                    return;
                }
                want |= 1u << i;
            }
        } else {
            uname_err(1932, tok);
            return;
        }
    }
    if (!want) want = 1;
    int first = 1;
    for (int i = 0; i < NUF; i++) {
        if (!(want & (1u << i))) continue;
        if (!first) out(1, " ");
        out(1, uname_f[i].val);
        first = 0;
    }
    crlf(1);
}
