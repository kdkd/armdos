/*
 * REDIR.EXE - test helper: run a program with stdin and/or stdout redirected
 * the way COMMAND.COM does (open, 45h/46h onto handle 0/1, EXEC, restore).
 * The kernel's test shell mangles a line that has both "<" and ">".
 *
 *   REDIR infile|- outfile|- [>>]  PROGRAM [arguments]
 */
#include "u4.h"

static char *word(char **p)
{
    char *s = *p;
    while (*s == ' ') s++;
    char *w = s;
    while (*s && *s != ' ' && *s != '\r') s++;
    if (*s) *s++ = 0;
    *p = s;
    return w;
}

static int redirect(int h, const char *name, int out)
{
    int fh;
    if (!strcmp(name, "-")) return -1;
    fh = out ? u4_creat(name, 0) : u4_open(name, 0);
    if (fh < 0) { u4_puts(STDERR, "REDIR: cannot open "); u4_puts(STDERR, name); u4_puts(STDERR, "\r\n"); u4_exit(1); }
    struct armregs r;
    u4_clr(&r); r.r0 = 0x4500; r.r1 = h; u4_int21(&r);
    int saved = r.r0 & 0xFFFF;
    u4_clr(&r); r.r0 = 0x4600; r.r1 = fh; r.r2 = h; u4_int21(&r);
    u4_close(fh);
    return saved;
}

int main(void)
{
    char *p = u4_cmdline();
    char *in = word(&p), *out = word(&p), *prog = word(&p);
    static uint8_t tail[130];
    int n = strlen(p);
    while (n && (p[n - 1] == '\r' || p[n - 1] == '\n')) n--;
    tail[0] = n + 1;
    tail[1] = ' ';
    memcpy(tail + 2, p, n);
    tail[n + 2] = '\r';
    static struct { uint16_t env; uint32_t tail, fcb1, fcb2; } __attribute__((packed)) pb;
    static uint8_t fcb[2][20];
    memset(fcb, 0, sizeof fcb);
    memset(fcb[0] + 1, ' ', 11); memset(fcb[1] + 1, ' ', 11);
    pb.env = 0; pb.tail = (uint32_t)tail; pb.fcb1 = (uint32_t)fcb[0]; pb.fcb2 = (uint32_t)fcb[1];
    int si = redirect(0, in, 0), so = redirect(1, out, 1);
    struct armregs r;
    u4_clr(&r); r.r0 = 0x4B00; r.r1 = (uint32_t)&pb; r.r3 = (uint32_t)prog;
    int cf = u4_int21(&r);
    int err = r.r0 & 0xFFFF;
    if (so >= 0) { u4_clr(&r); r.r0 = 0x4600; r.r1 = so; r.r2 = 1; u4_int21(&r); u4_close(so); }
    if (si >= 0) { u4_clr(&r); r.r0 = 0x4600; r.r1 = si; r.r2 = 0; u4_int21(&r); u4_close(si); }
    if (cf) { char b[8]; u4_puts(STDERR, "REDIR: EXEC error "); u4_puts(STDERR, u4_utoa(err, b)); u4_puts(STDERR, "\r\n"); return 1; }
    u4_clr(&r); r.r0 = 0x4D00; u4_int21(&r);
    return r.r0 & 0xFF;
}
