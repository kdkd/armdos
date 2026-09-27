/*
 * tshell.c - the kernel's test shell (not COMMAND.COM).  CONFIG.SYS:
 *     SHELL=\T\TSHELL.EXE \T\SCRIPT.TXT      run a script
 *     SHELL=\T\TSHELL.EXE                    interactive ("T>" prompt, AH=0Ah)
 * Lines:  PROG args [<in] [>out|>>out]   run a program (logs T:EXIT name code type)
 *         echo text | cd dir | ver | mem | exit n (emulator exit code) | halt
 *         interactive | # comment
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "armdos.h"
#include "t.h"

static uint8_t linebuf[130];

static int dos(struct armregs *r) { return _armdos_int21(r); }

static void puts_dos(const char *s)
{
    struct armregs r = { 0 };
    r.r0 = 0x4000; r.r1 = 1; r.r2 = strlen(s); r.r3 = (uint32_t)s;
    dos(&r);
}

static int parse_fcb(const char *s, uint8_t *fcb, const char **rest)
{
    struct armregs r = { 0 };
    memset(fcb, 0, 16);
    memset(fcb + 1, ' ', 11);
    r.r0 = 0x2901;
    r.r4 = (uint32_t)s;
    r.r5 = (uint32_t)fcb;
    dos(&r);
    *rest = (const char *)r.r4;
    return r.r0 & 0xFF;
}

static int redirect(int h, const char *name, int mode)
{
    struct armregs r = { 0 };
    if (mode == 0) { r.r0 = 0x3D00; r.r3 = (uint32_t)name; }
    else if (mode == 1) { r.r0 = 0x3C00; r.r3 = (uint32_t)name; }
    else {
        r.r0 = 0x3D01; r.r3 = (uint32_t)name;
        if (dos(&r)) { memset(&r, 0, sizeof r); r.r0 = 0x3C00; r.r3 = (uint32_t)name; }
        else goto opened;
    }
    if (dos(&r)) return -1;
opened:;
    int fh = r.r0 & 0xFFFF;
    if (mode == 2) { struct armregs s = { 0 }; s.r0 = 0x4202; s.r1 = fh; dos(&s); }
    struct armregs d = { 0 };
    d.r0 = 0x4500; d.r1 = h;                /* save the old one */
    dos(&d);
    int saved = d.r0 & 0xFFFF;
    memset(&d, 0, sizeof d);
    d.r0 = 0x4600; d.r1 = fh; d.r2 = h;     /* force dup onto h */
    dos(&d);
    memset(&d, 0, sizeof d);
    d.r0 = 0x3E00; d.r1 = fh;
    dos(&d);
    return saved;
}

static void restore(int h, int saved)
{
    if (saved < 0) return;
    struct armregs d = { 0 };
    d.r0 = 0x4600; d.r1 = saved; d.r2 = h;
    dos(&d);
    memset(&d, 0, sizeof d);
    d.r0 = 0x3E00; d.r1 = saved;
    dos(&d);
}

static int run(const char *prog, const char *args)
{
    static uint8_t tail[130], fcb1[20], fcb2[20];
    static struct { uint16_t env; uint32_t tail, fcb1, fcb2; } __attribute__((packed)) pb;
    char path[80];
    int n = strlen(args);
    if (n > 126) n = 126;
    tail[0] = n;
    memcpy(tail + 1, args, n);
    tail[n + 1] = '\r';
    const char *rest;
    parse_fcb(args, fcb1, &rest);
    parse_fcb(rest, fcb2, &rest);
    pb.env = 0;
    pb.tail = (uint32_t)tail;
    pb.fcb1 = (uint32_t)fcb1;
    pb.fcb2 = (uint32_t)fcb2;
    strcpy(path, prog);
    if (!strchr(path, '.')) strcat(path, ".EXE");
    struct armregs r = { 0 };
    r.r0 = 0x4B00;
    r.r1 = (uint32_t)&pb;
    r.r3 = (uint32_t)path;
    int cf = dos(&r);
    if (cf && (r.r0 & 0xFFFF) == 2 && !strpbrk(path, "\\:")) {
        char p2[90];
        strcpy(p2, "\\T\\");
        strcat(p2, path);
        strcpy(path, p2);
        memset(&r, 0, sizeof r);
        r.r0 = 0x4B00;
        r.r1 = (uint32_t)&pb;
        r.r3 = (uint32_t)path;
        cf = dos(&r);
    }
    if (cf) {
        t_log("T:EXECFAIL %s %u\n", path, (unsigned)(r.r0 & 0xFFFF));
        printf("EXEC %s failed: error %u\n", path, (unsigned)(r.r0 & 0xFFFF));
        return -1;
    }
    memset(&r, 0, sizeof r);
    r.r0 = 0x4D00;
    dos(&r);
    t_log("T:EXIT %s %u %u\n", prog, (unsigned)(r.r0 & 0xFF), (unsigned)((r.r0 >> 8) & 0xFF));
    return r.r0 & 0xFFFF;
}

static int interactive;

static void i24abort(struct armregs *f)
{
    t_log("T:I24 AX=%04X DI=%04X\n", (unsigned)(f->r0 & 0xFFFF), (unsigned)(f->r5 & 0xFFFF));
    f->r0 = (f->r0 & ~0xFFu) | 2;
}

static void do_line(char *line)
{
    while (*line == ' ' || *line == '\t') line++;
    char *e = line + strlen(line);
    while (e > line && (e[-1] == '\r' || e[-1] == '\n' || e[-1] == ' ')) *--e = 0;
    if (!*line || *line == '#') return;

    /* redirections */
    char *in = 0, *out = 0;
    int append = 0;
    for (char *p = line; *p; p++) {
        if (*p == '<' || *p == '>') {
            char c = *p;
            *p++ = 0;
            if (c == '>' && *p == '>') { append = 1; p++; }
            while (*p == ' ') p++;
            static char names[2][80];
            char *name = names[c == '<' ? 0 : 1];
            int n = 0;
            while (*p && *p != ' ' && *p != '\t' && *p != '<' && *p != '>' && n < 79) name[n++] = *p++;
            name[n] = 0;
            if (c == '<') in = name; else out = name;
            p--;
        }
    }
    e = line + strlen(line);
    while (e > line && e[-1] == ' ') *--e = 0;

    char *args = line;
    while (*args && *args != ' ') args++;
    char cmd[80];
    int n = args - line;
    if (n > 79) n = 79;
    memcpy(cmd, line, n);
    cmd[n] = 0;
    for (char *p = cmd; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32;

    int sin = -1, sout = -1;
    if (in) sin = redirect(0, in, 0);
    if (out) sout = redirect(1, out, append ? 2 : 1);

    if (!strcmp(cmd, "ECHO")) {
        puts_dos(*args ? args + 1 : "");
        puts_dos("\r\n");
    } else if (!strcmp(cmd, "EXIT")) {
        t_log("T:SHELLEXIT %s\n", *args ? args + 1 : "0");
        armdos_emu_exit(atoi(args));
    } else if (!strcmp(cmd, "ONCE")) {
        struct armregs r = { 0 };
        r.r0 = 0x5B00; r.r3 = (uint32_t)"C:\\MARK.TXT";
        if (dos(&r)) { t_log("T:SECONDRUN\n"); armdos_emu_exit(0); }
        struct armregs c = { 0 };
        c.r0 = 0x3E00; c.r1 = r.r0 & 0xFFFF;
        dos(&c);
    } else if (!strcmp(cmd, "I24ABORT")) {
        armdos_setvect(0x24, i24abort);
    } else if (!strcmp(cmd, "PAUSE")) {
        t_log("T:PAUSE\n");
        struct armregs r = { 0 };
        r.r0 = 0x0800;
        dos(&r);
    } else if (!strcmp(cmd, "HALT")) {
        for (;;) armdos_halt();
    } else if (!strcmp(cmd, "INTERACTIVE")) {
        interactive = 1;
    } else if (!strcmp(cmd, "CD")) {
        struct armregs r = { 0 };
        r.r0 = 0x3B00; r.r3 = (uint32_t)(args + 1);
        if (dos(&r)) printf("Invalid directory\n");
    } else if (!strcmp(cmd, "VER")) {
        struct armregs r = { 0 };
        r.r0 = 0x3000;
        dos(&r);
        printf("DOS %u.%02u\n", (unsigned)(r.r0 & 0xFF), (unsigned)((r.r0 >> 8) & 0xFF));
    } else if (!strcmp(cmd, "MEM")) {
        struct armregs r = { 0 };
        r.r0 = 0x4800; r.r1 = 0xFFFF;
        dos(&r);
        unsigned long kb = (unsigned long)(r.r1 & 0xFFFF) * 16;
        printf("largest free block %lu bytes\n", kb);
        t_log("T:MEM %lu\n", kb);
    } else {
        run(cmd, args);
    }
    fflush(stdout);
    restore(1, sout);
    restore(0, sin);
}

int main(int argc, char **argv)
{
    setvbuf(stdout, 0, _IONBF, 0);
    if (!_armdos_psp->envseg) {
        /* DOS 4's SYSINIT passes the shell no environment: build one, as COMMAND does */
        static const char env[] = "PATH=\0COMSPEC=C:\\T\\TSHELL.EXE\0";
        struct armregs r = { 0 };
        r.r0 = 0x4800; r.r1 = 8;
        if (!dos(&r)) {
            char *e = (char *)((r.r0 & 0xFFFF) << 4);
            memcpy(e, env, sizeof env);
            e[sizeof env] = 0;
            _armdos_psp->envseg = r.r0 & 0xFFFF;
        }
    }
    t_log("T:SHELL start argc=%d\n", argc);
    if (argc > 1) t_log("T:ARGV1 %s\n", argv[1]);
    if (argc > 1) {
        const char *script = argv[1][0] == '/' ? "\\T\\S.TXT" : argv[1];
        FILE *f = fopen(script, "r");
        if (!f) { printf("TSHELL: cannot open %s\n", argv[1]); t_log("T:FAIL tshell: no script\n"); }
        else {
            char line[200];
            while (!interactive && fgets(line, sizeof line, f)) do_line(line);
            fclose(f);
        }
        if (!interactive) armdos_emu_exit(0);
    }
    linebuf[0] = 127;
    for (;;) {
        puts_dos("T>");
        struct armregs r = { 0 };
        r.r0 = 0x0A00;
        r.r3 = (uint32_t)linebuf;
        dos(&r);
        puts_dos("\r\n");
        char line[130];
        memcpy(line, linebuf + 2, linebuf[1]);
        line[linebuf[1]] = 0;
        t_log("T:LINE [%s]\n", line);
        do_line(line);
    }
}
