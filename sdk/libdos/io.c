/* io.c - io.h extras. */
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <io.h>
#include "libdos.h"

long filelength(int fd)
{
    long cur = lseek(fd, 0, SEEK_CUR), end;
    if (cur < 0) return -1;
    end = lseek(fd, 0, SEEK_END);
    lseek(fd, cur, SEEK_SET);
    return end;
}

long tell(int fd) { return lseek(fd, 0, SEEK_CUR); }

int eof(int fd)
{
    long cur = lseek(fd, 0, SEEK_CUR), len;
    if (cur < 0) return -1;
    len = filelength(fd);
    if (len < 0) return -1;
    return cur >= len;
}

int chmod(const char *path, mode_t mode)
{
    struct armregs r;
    unsigned attr;
    if (_dos21(0x4300, 0, 0, (uint32_t)path, &r) < 0) return -1;
    attr = r.r2 & 0xFFFF;
    attr = (mode & S_IWUSR) ? (attr & ~1u) : (attr | 1u);
    return _dos21(0x4301, 0, attr & 0x27, (uint32_t)path, &r) < 0 ? -1 : 0;
}

extern int setmode(int, int);

int dup(int fd)
{
    struct armregs r;
    int n = _dos21(0x4500, fd, 0, 0, &r);
    if (n < 0) return -1;
    n &= 0xFFFF;
    setmode(n, _armdos_getmode(fd));
    return n;
}

int dup2(int fd, int fd2)
{
    struct armregs r;
    if (_dos21(0x4600, fd, fd2, 0, &r) < 0) return -1;
    setmode(fd2, _armdos_getmode(fd));
    return fd2;
}

int creat(const char *path, mode_t mode)
{
    return open(path, O_WRONLY | O_CREAT | O_TRUNC, mode);
}

static mode_t cur_umask;
mode_t umask(mode_t m) { mode_t o = cur_umask; cur_umask = m; return o; }
