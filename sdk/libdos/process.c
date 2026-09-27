/* process.c - spawn / exec / system via INT 21h AX=4B00h. */
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <process.h>
#include "libdos.h"

/* EXEC parameter block: DOS layout, each 4-byte far-pointer slot holds a
 * flat pointer (ARCH.md 5.1). */
struct execblk {
    uint16_t envseg;
    uint32_t tail;
    uint32_t fcb1;
    uint32_t fcb2;
} __attribute__((packed));

extern char **environ;

static const char *skip_ws(const char *s) { while (*s == ' ' || *s == '\t') s++; return s; }

/* INT 21h AX=2901h: parse a filename into an FCB (as COMMAND.COM does). */
static const char *parse_fcb(const char *s, uint8_t *fcb)
{
    struct armregs r = {0};
    memset(fcb, 0, 20);
    memset(fcb + 1, ' ', 11);
    r.r0 = 0x2901;
    r.r4 = (uint32_t)s;         /* DS:SI */
    r.r5 = (uint32_t)fcb;       /* ES:DI */
    _armdos_int21(&r);
    return r.r4 ? (const char *)r.r4 : s;
}

int _armdos_exec(const char *path, const char *tail, unsigned envseg)
{
    struct armregs r = {0};
    struct execblk pb;
    uint8_t cmd[128];
    uint8_t fcb1[20], fcb2[20];
    size_t n = strlen(tail);
    const char *p;

    if (n > 126) { errno = E2BIG; return -1; }
    cmd[0] = (uint8_t)n;
    memcpy(cmd + 1, tail, n);
    cmd[n + 1] = '\r';
    p = parse_fcb(skip_ws(tail), fcb1);
    parse_fcb(skip_ws(p), fcb2);

    pb.envseg = envseg;
    pb.tail = (uint32_t)cmd;
    pb.fcb1 = (uint32_t)fcb1;
    pb.fcb2 = (uint32_t)fcb2;

    r.r0 = 0x4B00;
    r.r1 = (uint32_t)&pb;       /* ES:BX */
    r.r3 = (uint32_t)path;      /* DS:DX */
    if (_armdos_int21(&r))
        return _armdos_seterr(r.r0);
    memset(&r, 0, sizeof r);
    r.r0 = 0x4D00;              /* get return code */
    _armdos_int21(&r);
    return r.r0 & 0xFF;
}

/* Build a DOS environment block in its own DOS memory block. */
static unsigned make_env(char *const envp[], unsigned *seg)
{
    size_t size = 1;
    char *dst;
    *seg = 0;
    if (!envp) return 0;
    for (int i = 0; envp[i]; i++) size += strlen(envp[i]) + 1;
    if (size > 32768) { errno = E2BIG; return 1; }
    struct armregs r = {0};
    r.r0 = 0x4800;
    r.r1 = (size + 2 + 15) >> 4;
    if (_armdos_int21(&r)) { errno = ENOMEM; return 1; }
    *seg = r.r0 & 0xFFFF;
    dst = ARMDOS_SEG2PTR(*seg);
    for (int i = 0; envp[i]; i++) {
        size_t l = strlen(envp[i]) + 1;
        memcpy(dst, envp[i], l);
        dst += l;
    }
    *dst++ = 0;
    if (size == 1) *dst = 0;    /* empty environment: two NULs */
    return 0;
}

static void free_seg(unsigned seg)
{
    struct armregs r = {0};
    if (!seg) return;
    r.r0 = 0x4900;
    r.r8 = seg;
    _armdos_int21(&r);
}

static int file_exists(const char *path)
{
    struct armregs r;
    if (_dos21(0x4300, 0, 0, (uint32_t)path, &r) < 0) return 0;
    return !(r.r2 & 0x18);      /* not a directory or label */
}

static const char *comspec(void)
{
    const char *c = getenv("COMSPEC");
    return c && *c ? c : "\\COMMAND.COM";
}

static int has_ext(const char *name)
{
    const char *base = name;
    for (const char *p = name; *p; p++)
        if (*p == '\\' || *p == '/' || *p == ':') base = p + 1;
    return strchr(base, '.') != 0;
}

static int is_bat(const char *name)
{
    size_t n = strlen(name);
    return n > 4 && (!strcmp(name + n - 4, ".BAT") || !strcmp(name + n - 4, ".bat"));
}

/* Resolve name (+ extensions) into out; 0 if found. */
static int resolve(const char *name, char *out, size_t outsz)
{
    static const char *const exts[] = { ".COM", ".EXE", ".BAT" };
    if (strlen(name) + 5 > outsz) { errno = ENAMETOOLONG; return -1; }
    strcpy(out, name);
    if (has_ext(name))
        return file_exists(out) ? 0 : (errno = ENOENT, -1);
    for (int i = 0; i < 3; i++) {
        strcpy(out, name);
        strcat(out, exts[i]);
        if (file_exists(out)) return 0;
    }
    errno = ENOENT;
    return -1;
}

static int resolve_path(const char *name, char *out, size_t outsz)
{
    const char *path;
    char buf[160];
    if (resolve(name, out, outsz) == 0) return 0;
    if (strpbrk(name, "\\/:")) return -1;
    path = getenv("PATH");
    while (path && *path) {
        const char *e = strchr(path, ';');
        size_t l = e ? (size_t)(e - path) : strlen(path);
        if (l && l + strlen(name) + 6 < sizeof buf) {
            memcpy(buf, path, l);
            if (buf[l - 1] != '\\' && buf[l - 1] != '/' && buf[l - 1] != ':') buf[l++] = '\\';
            strcpy(buf + l, name);
            if (resolve(buf, out, outsz) == 0) return 0;
        }
        path = e ? e + 1 : 0;
    }
    errno = ENOENT;
    return -1;
}

static int do_spawn(int mode, const char *name, char *const argv[], char *const envp[], int search)
{
    char path[160], tail[160];
    size_t n = 0;
    unsigned envseg;
    int rc;

    if (mode != P_WAIT && mode != P_OVERLAY) { errno = EINVAL; return -1; }
    if ((search ? resolve_path(name, path, sizeof path) : resolve(name, path, sizeof path)) < 0)
        return -1;

    /* command tail: " arg1 arg2 ..." (arguments joined as MS C does) */
    tail[0] = 0;
    if (is_bat(path)) {
        n = strlen(path) + 4;
        if (n >= sizeof tail) { errno = E2BIG; return -1; }
        strcpy(tail, " /C ");
        strcat(tail, path);
    }
    for (int i = 1; argv && argv[i]; i++) {
        size_t l = strlen(argv[i]);
        if (n + l + 1 > 126) { errno = E2BIG; return -1; }
        tail[n++] = ' ';
        memcpy(tail + n, argv[i], l);
        n += l;
        tail[n] = 0;
    }
    if (make_env(envp, &envseg)) return -1;
    rc = _armdos_exec(is_bat(path) ? comspec() : path, tail, envseg);
    free_seg(envseg);
    if (mode == P_OVERLAY && rc >= 0)
        exit(rc);
    return rc;
}

int spawnv(int mode, const char *path, char *const argv[])
{ return do_spawn(mode, path, argv, 0, 0); }
int spawnve(int mode, const char *path, char *const argv[], char *const envp[])
{ return do_spawn(mode, path, argv, envp, 0); }
int spawnvp(int mode, const char *file, char *const argv[])
{ return do_spawn(mode, file, argv, 0, 1); }
int spawnvpe(int mode, const char *file, char *const argv[], char *const envp[])
{ return do_spawn(mode, file, argv, envp, 1); }

#define MAXARGS 64
#define COLLECT(first, ap, argv, envp, want_env) do {               \
        int _n = 0;                                                 \
        const char *_a = (first);                                   \
        while (_a && _n < MAXARGS - 1) { argv[_n++] = (char *)_a; _a = va_arg(ap, const char *); } \
        argv[_n] = 0;                                               \
        if (want_env) envp = va_arg(ap, char *const *);             \
    } while (0)

int spawnl(int mode, const char *path, const char *arg0, ...)
{
    char *argv[MAXARGS]; char *const *envp = 0; va_list ap;
    va_start(ap, arg0); COLLECT(arg0, ap, argv, envp, 0); va_end(ap);
    return do_spawn(mode, path, argv, envp, 0);
}
int spawnle(int mode, const char *path, const char *arg0, ...)
{
    char *argv[MAXARGS]; char *const *envp = 0; va_list ap;
    va_start(ap, arg0); COLLECT(arg0, ap, argv, envp, 1); va_end(ap);
    return do_spawn(mode, path, argv, envp, 0);
}
int spawnlp(int mode, const char *file, const char *arg0, ...)
{
    char *argv[MAXARGS]; char *const *envp = 0; va_list ap;
    va_start(ap, arg0); COLLECT(arg0, ap, argv, envp, 0); va_end(ap);
    return do_spawn(mode, file, argv, envp, 1);
}
int spawnlpe(int mode, const char *file, const char *arg0, ...)
{
    char *argv[MAXARGS]; char *const *envp = 0; va_list ap;
    va_start(ap, arg0); COLLECT(arg0, ap, argv, envp, 1); va_end(ap);
    return do_spawn(mode, file, argv, envp, 1);
}

/* exec*: spawn, then exit with the child's exit code. */
int execv(const char *path, char *const argv[]) { return do_spawn(P_OVERLAY, path, argv, 0, 0); }
int execve(const char *path, char *const argv[], char *const envp[]) { return do_spawn(P_OVERLAY, path, argv, envp, 0); }
int execvp(const char *file, char *const argv[]) { return do_spawn(P_OVERLAY, file, argv, 0, 1); }
int execvpe(const char *file, char *const argv[], char *const envp[]) { return do_spawn(P_OVERLAY, file, argv, envp, 1); }
int execl(const char *path, const char *arg0, ...)
{
    char *argv[MAXARGS]; char *const *envp = 0; va_list ap;
    va_start(ap, arg0); COLLECT(arg0, ap, argv, envp, 0); va_end(ap);
    return do_spawn(P_OVERLAY, path, argv, envp, 0);
}
int execle(const char *path, const char *arg0, ...)
{
    char *argv[MAXARGS]; char *const *envp = 0; va_list ap;
    va_start(ap, arg0); COLLECT(arg0, ap, argv, envp, 1); va_end(ap);
    return do_spawn(P_OVERLAY, path, argv, envp, 0);
}
int execlp(const char *file, const char *arg0, ...)
{
    char *argv[MAXARGS]; char *const *envp = 0; va_list ap;
    va_start(ap, arg0); COLLECT(arg0, ap, argv, envp, 0); va_end(ap);
    return do_spawn(P_OVERLAY, file, argv, envp, 1);
}
int execlpe(const char *file, const char *arg0, ...)
{
    char *argv[MAXARGS]; char *const *envp = 0; va_list ap;
    va_start(ap, arg0); COLLECT(arg0, ap, argv, envp, 1); va_end(ap);
    return do_spawn(P_OVERLAY, file, argv, envp, 1);
}

/* system(): %COMSPEC% /C command. system(NULL) = is a command processor there? */
int system(const char *cmd)
{
    char tail[130];
    const char *cs = comspec();
    if (!cmd) return file_exists(cs);
    if (strlen(cmd) + 4 > 126) { errno = E2BIG; return -1; }
    strcpy(tail, " /C ");
    strcat(tail, cmd);
    return _armdos_exec(cs, tail, 0);
}
