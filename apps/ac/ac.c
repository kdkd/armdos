/*
 * AC.EXE - the ARM Commander's small resident loader.
 *
 * Runs ACMAIN.EXE (from the directory AC.EXE was started from) with the
 * address of the shared block (shared.h) in its command tail. When ACMAIN
 * exits asking for a command, the command is run here - with ACMAIN out of
 * memory, so the program gets everything but these few KB - and ACMAIN is
 * started again. F10 in ACMAIN ends both.
 *
 * A command line whose first word is a .COM/.EXE program (searched as
 * COMMAND.COM searches: as given, .COM, .EXE, .BAT, current directory then
 * PATH) and that has no redirection or pipe is EXECed directly; everything
 * else (internal commands, batch files, < > |, unknown names) goes to
 * %COMSPEC% /C, which also says "Bad command or file name".
 *
 * Written for ARM-DOS; no Symantec/Peter Norton code.
 */
#include <string.h>
#include "armdos.h"
#include "shared.h"

static struct acblk blk;
static char mainpath[144];
struct psp *_armdos_psp;

/* our own start-up (instead of libdos's, which brings the heap and argv):
   keep only PSP..stack top, run main, exit with its code */
void __armdos_start(struct psp *psp, uint8_t *base, uint8_t *blockend, uint8_t *stacktop)
{
    extern int main(void);
    struct armregs r = {0};
    (void)base; (void)blockend;
    _armdos_psp = psp;
    r.r0 = 0x4A00;
    r.r1 = ((uint32_t)(stacktop - (uint8_t *)psp) + 15) >> 4;
    r.r8 = (uint32_t)psp >> 4;
    _armdos_int21(&r);
    int rc = main();
    memset(&r, 0, sizeof r);
    r.r0 = 0x4C00 | (rc & 0xFF);
    _armdos_int21(&r);
    for (;;) ;
}

static void say(const char *s)
{
    struct armregs r = {0};
    r.r0 = 0x4000;
    r.r1 = 2;
    r.r2 = strlen(s);
    r.r3 = (uint32_t)s;
    _armdos_int21(&r);
}

static int upc(int c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }

static const char *envblock(void)
{
    uint16_t seg = *(uint16_t *)((uint8_t *)_armdos_psp + 0x2C);
    return seg ? (const char *)ARMDOS_SEG2PTR(seg) : 0;
}

static const char *getenv_dos(const char *name)
{
    const char *e = envblock();
    size_t n = strlen(name);
    if (!e) return 0;
    while (*e) {
        if (!strncmp(e, name, n) && e[n] == '=') return e + n + 1;
        e += strlen(e) + 1;
    }
    return 0;
}

/* the program's own path, stored after the environment strings */
static const char *ownpath(void)
{
    const char *e = envblock();
    if (!e) return 0;
    while (*e) e += strlen(e) + 1;
    e++;
    if (e[0] == 0 && e[1] == 0) return 0;
    return e + 2;
}

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

/* EXEC AX=4B00h; returns AX of 4Dh (type<<8 | code) or -1 */
static int dos_exec(const char *path, const char *tail)
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
    return r.r0 & 0xFFFF;
}

static int exists(const char *path)
{
    struct armregs r = {0};
    r.r0 = 0x4300;
    r.r3 = (uint32_t)path;
    if (_armdos_int21(&r)) return 0;
    return !(r.r2 & 0x18);
}

/* dir + name (+ .COM/.EXE/.BAT); 1 = .COM/.EXE, 2 = .BAT, 0 = none */
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

static int find_program(const char *word, char *full)
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

static const char *const internals[] = {
    "BREAK", "CALL", "CD", "CHCP", "CHDIR", "CLS", "COPY", "CTTY", "DATE", "DEL",
    "DIR", "ECHO", "ERASE", "EXIT", "FOR", "GOTO", "IF", "MD", "MKDIR", "PATH",
    "PAUSE", "PROMPT", "RD", "REM", "REN", "RENAME", "RMDIR", "SET", "SHIFT",
    "TIME", "TRUENAME", "TYPE", "VER", "VERIFY", "VOL", 0
};

static int run_line(const char *line)
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
    if (!i || (i == 2 && word[1] == ':')) goto viacommand;
    if (!haspath)
        for (int k = 0; internals[k]; k++)
            if (!strcmp(word, internals[k])) goto viacommand;
    if (find_program(word, full) != 1 || strlen(p) > 126) goto viacommand;
    strcpy(tail, p);
    return dos_exec(full, tail);
viacommand:
    memcpy(tail, " /C ", 4);
    strncpy(tail + 4, line, 122);
    tail[126] = 0;
    {
        const char *cs = getenv_dos("COMSPEC");
        return dos_exec(cs ? cs : "\\COMMAND.COM", tail);
    }
}

static void hex8(char *o, uint32_t v)
{
    for (int i = 7; i >= 0; i--, v >>= 4) o[i] = "0123456789ABCDEF"[v & 15];
    o[8] = 0;
}

int main(void)
{
    const char *me = ownpath();
    char tail[24];
    int aborts = 0;

    if (me && strlen(me) < sizeof mainpath - 12) {
        strcpy(mainpath, me);
        char *b = strrchr(mainpath, '\\');
        if (!b) b = strchr(mainpath, ':');
        strcpy(b ? b + 1 : mainpath, "ACMAIN.EXE");
    } else
        strcpy(mainpath, "ACMAIN.EXE");
    if (!exists(mainpath) && find_program("ACMAIN.EXE", mainpath) != 1) {
        say("Cannot find ACMAIN.EXE\r\n");
        return 1;
    }
    blk.magic = AC_MAGIC;
    blk.version = AC_VERSION;
    memcpy(tail, " /$", 3);
    hex8(tail + 3, (uint32_t)&blk);
    for (;;) {
        blk.action = ACT_QUIT;
        int rc = dos_exec(mainpath, tail);
        if (rc < 0) {
            say("Cannot run ACMAIN.EXE\r\n");
            return 1;
        }
        if ((rc >> 8) != 0) {           /* ^C or critical-error abort: start again */
            if (++aborts > 3) break;
            continue;
        }
        aborts = 0;
        if (blk.action != ACT_RUN) break;
        rc = run_line(blk.cmd);
        blk.lastrc = rc < 0 ? 0xFFFF : rc;
        if (blk.restarts < 255) blk.restarts++;
    }
    return 0;
}
