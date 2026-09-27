/*
 * runcmd.c - run one command line the way COMMAND.COM would, for SHELLB and
 * for SHELLC when it runs without SHELLB.
 *
 * A line whose first word is a .COM or .EXE program (found as COMMAND finds
 * it: the name as given, then .COM, .EXE, .BAT, in the current directory and
 * then along PATH) and which has no redirection or pipe is EXECed directly -
 * the real Shell ran programs from the resident COMMAND.COM through
 * DOSSHELL.BAT, not from a second copy, so the program gets the same memory
 * it would get at the DOS prompt. Internal commands, batch files, anything
 * with < > | and names that cannot be found go to %COMSPEC% /C, which also
 * produces "Bad command or file name".
 */
#include <string.h>
#include "armdos.h"
#include "shared.h"

static const char *const internals[] = {
    "BREAK", "CALL", "CD", "CHCP", "CHDIR", "CLS", "COPY", "CTTY", "DATE", "DEL",
    "DIR", "ECHO", "ERASE", "EXIT", "FOR", "GOTO", "IF", "MD", "MKDIR", "PATH",
    "PAUSE", "PROMPT", "RD", "REM", "REN", "RENAME", "RMDIR", "SET", "SHIFT",
    "TIME", "TRUENAME", "TYPE", "VER", "VERIFY", "VOL", 0
};

const char *getenv_dos(const char *name)
{
    extern struct psp *_armdos_psp;
    const char *e = ARMDOS_SEG2PTR(*(uint16_t *)((uint8_t *)_armdos_psp + 0x2C));
    size_t n = strlen(name);
    if (!e || e == (const char *)0) return 0;
    while (*e) {
        if (!strncmp(e, name, n) && e[n] == '=') return e + n + 1;
        e += strlen(e) + 1;
    }
    return 0;
}

/* EXEC AX=4B00h with a raw command tail; returns the exit code or -1
   (DOS layout parameter block, flat pointers in the far-pointer slots) */
struct execblk {
    uint16_t envseg;
    uint32_t tail, fcb1, fcb2;
} __attribute__((packed));

static const char *parse_fcb(const char *s, uint8_t *fcb)
{
    struct armregs r = {0};
    while (*s == ' ' || *s == '\t') s++;
    memset(fcb, 0, 20);
    memset(fcb + 1, ' ', 11);
    r.r0 = 0x2901;
    r.r4 = (uint32_t)s;
    r.r5 = (uint32_t)fcb;
    _armdos_int21(&r);
    return r.r4 ? (const char *)r.r4 : s;
}

int dos_exec(const char *path, const char *tail)
{
    struct armregs r = {0};
    struct execblk pb;
    uint8_t cmd[128], fcb1[20], fcb2[20];
    size_t n = strlen(tail);
    if (n > 126) n = 126;
    cmd[0] = (uint8_t)n;
    memcpy(cmd + 1, tail, n);
    cmd[n + 1] = '\r';
    parse_fcb(parse_fcb(tail, fcb1), fcb2);
    pb.envseg = 0;
    pb.tail = (uint32_t)cmd;
    pb.fcb1 = (uint32_t)fcb1;
    pb.fcb2 = (uint32_t)fcb2;
    r.r0 = 0x4B00;
    r.r1 = (uint32_t)&pb;
    r.r3 = (uint32_t)path;
    if (_armdos_int21(&r)) return -1;
    memset(&r, 0, sizeof r);
    r.r0 = 0x4D00;
    _armdos_int21(&r);
    return r.r0 & 0xFFFF;               /* AH = termination type, AL = code */
}

static int upc(int c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }

static int exists(const char *path)
{
    struct armregs r = {0};
    r.r0 = 0x4300;
    r.r3 = (uint32_t)path;
    if (_armdos_int21(&r)) return 0;
    return !(r.r2 & 0x18);          /* not a directory or label */
}

/* try dir+name, and with .COM .EXE .BAT if name has no extension;
   returns 1 (.COM/.EXE) or 2 (.BAT) with the full name in out */
static int try_dir(char *out, const char *dir, int dlen, const char *name, int hasext)
{
    static const char *const ext[] = { ".COM", ".EXE", ".BAT" };
    int n = 0;
    if (dlen) {
        memcpy(out, dir, dlen);
        n = dlen;
        if (out[n - 1] != '\\' && out[n - 1] != '/' && out[n - 1] != ':') out[n++] = '\\';
    }
    strcpy(out + n, name);
    int l = strlen(out);
    if (hasext) {
        if (!exists(out)) return 0;
        const char *d = strrchr(out, '.');
        if (!strcmp(d, ".BAT")) return 2;
        return (!strcmp(d, ".COM") || !strcmp(d, ".EXE")) ? 1 : 0;
    }
    for (int i = 0; i < 3; i++) {
        strcpy(out + l, ext[i]);
        if (exists(out)) return i == 2 ? 2 : 1;
    }
    return 0;
}

/* COMMAND's search for an upper-case program word: 1 = .COM/.EXE, 2 = .BAT */
int find_program(const char *word, char *full)
{
    int hasext = 0, haspath = 0;
    for (const char *q = word; *q; q++) {
        if (*q == '.') hasext = 1;
        if (*q == '\\' || *q == ':') { haspath = 1; hasext = 0; }
    }
    int kind = try_dir(full, "", 0, word, hasext);
    if (!kind && !haspath) {
        const char *path = getenv_dos("PATH");
        while (path && *path && !kind) {
            const char *e = strchr(path, ';');
            int l = e ? e - path : (int)strlen(path);
            if (l > 0 && l < 64) kind = try_dir(full, path, l, word, hasext);
            path += l + (e ? 1 : 0);
        }
    }
    return kind;
}

/* "NAME.EXE" or "\\DIR\\NAME.EXE" -> "C:\\CUR\\DIR\\NAME.EXE" */
void make_absolute(char *path)
{
    char t[144];
    struct armregs r = {0};
    if (path[0] && path[1] == ':' && path[2] == '\\') return;
    r.r0 = 0x1900;
    _armdos_int21(&r);
    int drive = r.r0 & 0xFF;
    const char *rest = path;
    if (path[0] && path[1] == ':') { drive = upc(path[0]) - 'A'; rest = path + 2; }
    t[0] = 'A' + drive;
    t[1] = ':';
    t[2] = '\\';
    t[3] = 0;
    if (*rest != '\\') {
        memset(&r, 0, sizeof r);
        r.r0 = 0x4700;
        r.r3 = drive + 1;
        r.r4 = (uint32_t)(t + 3);
        _armdos_int21(&r);
        int n = strlen(t);
        if (t[n - 1] != '\\') { t[n] = '\\'; t[n + 1] = 0; }
    } else rest++;
    if (strlen(t) + strlen(rest) < 143) strcat(t, rest);
    strcpy(path, t);
}

int run_comspec(const char *tail)
{
    const char *cs = getenv_dos("COMSPEC");
    return dos_exec(cs ? cs : "\\COMMAND.COM", tail);
}

int run_line(const char *line)
{
    char word[80], full[144], tail[130];
    const char *p = line;
    int i = 0, haspath = 0;

    while (*p == ' ' || *p == '\t' || *p == '@') p++;
    if (!*p) return 0;
    if (strpbrk(p, "<>|")) goto viacommand;
    while (*p && !strchr(" \t/;=,+[]\"", *p) && i < 78) {
        int c = upc(*p++);
        if (c == '\\' || c == ':') haspath = 1;
        word[i++] = c;
    }
    word[i] = 0;
    if (!i) goto viacommand;
    if (i == 2 && word[1] == ':') goto viacommand;          /* drive change */
    if (!haspath)
        for (int k = 0; internals[k]; k++)
            if (!strcmp(word, internals[k])) goto viacommand;
    {
        int kind = find_program(word, full);
        if (kind != 1) goto viacommand;
        if (strlen(p) > 126) goto viacommand;
        strcpy(tail, p);
        return dos_exec(full, tail);
    }
viacommand:
    tail[0] = ' ';
    tail[1] = '/';
    tail[2] = 'C';
    tail[3] = ' ';
    strncpy(tail + 4, line, 122);
    tail[126] = 0;
    return run_comspec(tail);
}
