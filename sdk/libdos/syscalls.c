/*
 * syscalls.c - newlib system-call glue for ARM-DOS (INT 21h).
 *
 * Handles are DOS handles. Each handle has a translation mode:
 *   text   (default, like MS C): "\n" -> "\r\n" on write; "\r\n" -> "\n"
 *          on read; Ctrl-Z (1Ah) ends a text read (EOF; for a file the file
 *          pointer is left at the Ctrl-Z so every later read sees EOF too)
 *   binary no translation (O_BINARY, fopen "b", setmode)
 * The default for open() without O_TEXT/O_BINARY is _fmode (O_TEXT).
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/times.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "armdos.h"
#include "libdos.h"

int _fmode = O_TEXT;

#define M_TEXT    0x01
#define M_APPEND  0x02
#define M_DEVKNOWN 0x04
#define M_DEVICE  0x08
#define M_EOF     0x10   /* device saw Ctrl-Z: next read returns 0 */

static unsigned char fdmode[ARMDOS_MAXFD];
static short fdpend[ARMDOS_MAXFD];          /* pushed-back byte or -1 */

uint32_t _armdos_start_ticks;

#define CHUNK 0x8000u       /* keep counts in 16 bits, as DOS expects */

/* ------------------------------------------------------------ errors ---- */

int _armdos_errno(unsigned e)
{
    switch (e & 0xFFFF) {
    case 0x01: return ENOSYS;   /* invalid function */
    case 0x02: return ENOENT;   /* file not found */
    case 0x03: return ENOENT;   /* path not found */
    case 0x04: return EMFILE;   /* too many open files */
    case 0x05: return EACCES;   /* access denied */
    case 0x06: return EBADF;    /* invalid handle */
    case 0x07: return ENOMEM;   /* MCBs destroyed */
    case 0x08: return ENOMEM;   /* insufficient memory */
    case 0x09: return EINVAL;   /* invalid memory block */
    case 0x0A: return E2BIG;    /* bad environment */
    case 0x0B: return ENOEXEC;  /* bad format */
    case 0x0C: return EINVAL;   /* invalid access code */
    case 0x0D: return EINVAL;   /* invalid data */
    case 0x0F: return ENODEV;   /* invalid drive */
    case 0x10: return EACCES;   /* attempt to remove current directory */
    case 0x11: return EXDEV;    /* not same device */
    case 0x12: return ENOENT;   /* no more files */
    case 0x13: return EROFS;    /* write protected */
    case 0x15: return EAGAIN;   /* drive not ready */
    case 0x20: case 0x21: return EACCES;  /* sharing / lock violation */
    case 0x27: return ENOSPC;   /* disk full (DOS 4) */
    case 0x50: return EEXIST;   /* file exists */
    case 0x52: return EACCES;   /* cannot make directory entry */
    case 0x57: return EINVAL;   /* invalid parameter */
    default:   return EIO;
    }
}

int _armdos_seterr(unsigned e)
{
    errno = _armdos_errno(e);
    return -1;
}

/* ------------------------------------------------------- handle modes --- */

static int valid_fd(int fd) { return fd >= 0 && fd < ARMDOS_MAXFD; }

void _armdos_io_init(void)
{
    for (int i = 0; i < ARMDOS_MAXFD; i++) fdpend[i] = -1;
    for (int i = 0; i < 5; i++) fdmode[i] = M_TEXT;   /* stdin..stdprn */
}

static int is_device(int fd)
{
    struct armregs r;
    if (valid_fd(fd) && (fdmode[fd] & M_DEVKNOWN))
        return (fdmode[fd] & M_DEVICE) != 0;
    if (_dos21(0x4400, fd, 0, 0, &r) < 0)
        return 0;
    int dev = (r.r3 & 0x80) != 0;
    if (valid_fd(fd))
        fdmode[fd] |= M_DEVKNOWN | (dev ? M_DEVICE : 0);
    return dev;
}

int _armdos_getmode(int fd)
{
    if (!valid_fd(fd)) return O_TEXT;
    return (fdmode[fd] & M_TEXT) ? O_TEXT : O_BINARY;
}

/* MS C setmode(): returns the previous mode or -1. */
int setmode(int fd, int mode)
{
    int old;
    if (!valid_fd(fd) || (mode != O_TEXT && mode != O_BINARY)) {
        errno = EINVAL;
        return -1;
    }
    old = _armdos_getmode(fd);
    if (mode == O_TEXT) fdmode[fd] |= M_TEXT;
    else fdmode[fd] &= ~M_TEXT;
    return old;
}
int _setmode(int fd, int mode) __attribute__((alias("setmode")));

/* --------------------------------------------------------- raw I/O ------ */

static int raw_read(int fd, void *buf, unsigned n)
{
    struct armregs r = {0};
    if (n > CHUNK) n = CHUNK;
    r.r0 = 0x3F00; r.r1 = fd; r.r2 = n; r.r3 = (uint32_t)buf;
    if (_armdos_int21(&r)) return _armdos_seterr(r.r0);
    return (int)(r.r0 & 0xFFFF);
}

static int raw_write(int fd, const void *buf, unsigned n)
{
    struct armregs r = {0};
    r.r0 = 0x4000; r.r1 = fd; r.r2 = n; r.r3 = (uint32_t)buf;
    if (_armdos_int21(&r)) return _armdos_seterr(r.r0);
    return (int)(r.r0 & 0xFFFF);
}

static long raw_seek(int fd, long off, int whence)
{
    struct armregs r = {0};
    r.r0 = 0x4200 | (whence & 0xFF);
    r.r1 = fd;
    r.r2 = ((uint32_t)off >> 16) & 0xFFFF;      /* CX:DX = offset */
    r.r3 = (uint32_t)off & 0xFFFF;
    if (_armdos_int21(&r)) return _armdos_seterr(r.r0);
    return (long)(((r.r3 & 0xFFFF) << 16) | (r.r0 & 0xFFFF));  /* DX:AX */
}

/* ------------------------------------------------------------- open ----- */

int _open(const char *path, int flags, int mode)
{
    struct armregs r;
    int acc = flags & O_ACCMODE;
    unsigned attr = (mode & S_IWUSR) ? 0 : 0x01;   /* read-only if no write perm */
    int fd;

    if (flags & O_CREAT) {
        if (flags & O_EXCL)
            fd = _dos21(0x5B00, 0, attr, (uint32_t)path, &r);        /* create new */
        else if (flags & O_TRUNC)
            fd = _dos21(0x3C00, 0, attr, (uint32_t)path, &r);        /* create */
        else {
            fd = _dos21(0x3D00 | acc, 0, 0, (uint32_t)path, &r);
            if (fd < 0 && errno == ENOENT)
                fd = _dos21(0x3C00, 0, attr, (uint32_t)path, &r);
        }
        /* 3Ch/5Bh open read/write; that's fine for any access mode */
    } else {
        fd = _dos21(0x3D00 | acc, 0, 0, (uint32_t)path, &r);
        if (fd >= 0 && (flags & O_TRUNC) && acc != O_RDONLY) {
            struct armregs t;
            _dos21(0x4000, fd, 0, 0, &t);                             /* write 0 = truncate */
        }
    }
    if (fd < 0)
        return -1;
    fd &= 0xFFFF;
    if (valid_fd(fd)) {
        int text = (flags & O_BINARY) ? 0 : (flags & O_TEXT) ? 1 : (_fmode != O_BINARY);
        fdmode[fd] = (text ? M_TEXT : 0) | ((flags & O_APPEND) ? M_APPEND : 0);
        fdpend[fd] = -1;
    }
    return fd;
}

int _close(int fd)
{
    struct armregs r;
    if (_dos21(0x3E00, fd, 0, 0, &r) < 0)
        return -1;
    if (valid_fd(fd)) { fdmode[fd] = 0; fdpend[fd] = -1; }
    return 0;
}

/* ------------------------------------------------------------- read ----- */

int _read(int fd, void *vbuf, size_t n)
{
    char *buf = vbuf;
    int got, i, o;

    if (n == 0) return 0;
    if (!valid_fd(fd) || !(fdmode[fd] & M_TEXT))
        return raw_read(fd, buf, n);

    if (fdmode[fd] & M_EOF) {           /* device hit Ctrl-Z last time */
        fdmode[fd] &= ~M_EOF;
        return 0;
    }
    got = 0;
    if (fdpend[fd] >= 0) {
        buf[got++] = (char)fdpend[fd];
        fdpend[fd] = -1;
    }
    if ((size_t)got < n) {
        int k = raw_read(fd, buf + got, n - got);
        if (k < 0) return got ? got : -1;
        got += k;
    }
    /* Ctrl-Z ends the text */
    for (i = 0; i < got; i++) {
        if (buf[i] == 0x1A) {
            if (is_device(fd)) {
                if (i == 0) return 0;
                fdmode[fd] |= M_EOF;
            } else {
                raw_seek(fd, -(long)(got - i), SEEK_CUR);
            }
            got = i;
            break;
        }
    }
    /* CR LF -> LF (a lone CR stays) */
    for (i = 0, o = 0; i < got; i++) {
        char c = buf[i];
        if (c == '\r') {
            if (i + 1 < got) {
                if (buf[i + 1] == '\n') continue;
            } else {
                char nx;                  /* CR is the last byte: peek */
                if (raw_read(fd, &nx, 1) == 1) {
                    if (nx == '\n') { buf[o++] = '\n'; continue; }
                    if (is_device(fd)) fdpend[fd] = (unsigned char)nx;
                    else raw_seek(fd, -1, SEEK_CUR);
                }
            }
        }
        buf[o++] = c;
    }
    return o;
}

/* ------------------------------------------------------------ write ----- */

int _write(int fd, const void *vbuf, size_t n)
{
    const char *buf = vbuf;
    size_t done = 0;

    if (n == 0) return 0;                 /* a 0-byte DOS write truncates! */
    if (valid_fd(fd) && (fdmode[fd] & M_APPEND))
        raw_seek(fd, 0, SEEK_END);

    if (!valid_fd(fd) || !(fdmode[fd] & M_TEXT)) {
        while (done < n) {
            unsigned c = n - done > CHUNK ? CHUNK : n - done;
            int k = raw_write(fd, buf + done, c);
            if (k < 0) return done ? (int)done : -1;
            done += k;
            if ((unsigned)k < c) {
                if (!done) { errno = ENOSPC; return -1; }
                break;
            }
        }
        return done;
    }

    /* text: expand LF to CR LF through a small buffer */
    char tmp[256];
    while (done < n) {
        size_t used = 0, t = 0;
        while (done + used < n && t < sizeof tmp - 1) {
            char c = buf[done + used];
            if (c == '\n') tmp[t++] = '\r';
            tmp[t++] = c;
            used++;
        }
        int k = raw_write(fd, tmp, t);
        if (k < 0) return done ? (int)done : -1;
        if ((size_t)k < t) {              /* disk full */
            if (!done) { errno = ENOSPC; return -1; }
            return done;
        }
        done += used;
    }
    return done;
}

/* ------------------------------------------------------------ seek ------ */

off_t _lseek(int fd, off_t off, int whence)
{
    if (valid_fd(fd)) {
        if (fdpend[fd] >= 0 && whence == SEEK_CUR) off--;
        fdpend[fd] = -1;
        fdmode[fd] &= ~M_EOF;
    }
    return raw_seek(fd, off, whence);
}

/* ------------------------------------------------------------ stat ------ */

static long days_from_civil(int y, unsigned m, unsigned d)
{
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long)doe - 719468;
}
long _armdos_days_from_civil(int y, unsigned m, unsigned d) { return days_from_civil(y, m, d); }

long _armdos_dostime_to_unix(unsigned date, unsigned time)
{
    int y = 1980 + ((date >> 9) & 0x7F);
    unsigned mo = (date >> 5) & 15, d = date & 31;
    if (mo < 1) mo = 1;
    if (d < 1) d = 1;
    return days_from_civil(y, mo, d) * 86400L
         + ((time >> 11) & 31) * 3600L + ((time >> 5) & 63) * 60L + (time & 31) * 2L;
}

void _armdos_unix_to_dostime(long t, unsigned *date, unsigned *time)
{
    long days = t / 86400, s = t % 86400;
    if (s < 0) { s += 86400; days--; }
    /* civil_from_days */
    days += 719468;
    long era = (days >= 0 ? days : days - 146096) / 146097;
    unsigned doe = (unsigned)(days - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long y = (long)yoe + era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    unsigned d = doy - (153 * mp + 2) / 5 + 1;
    unsigned m = mp < 10 ? mp + 3 : mp - 9;
    y += m <= 2;
    if (y < 1980) y = 1980;
    if (date) *date = (unsigned)(((y - 1980) << 9) | (m << 5) | d);
    if (time) *time = (unsigned)(((s / 3600) << 11) | (((s / 60) % 60) << 5) | ((s % 60) / 2));
}

int _fstat(int fd, struct stat *st)
{
    struct armregs r;
    memset(st, 0, sizeof *st);
    if (_dos21(0x4400, fd, 0, 0, &r) < 0)
        return -1;
    st->st_nlink = 1;
    st->st_dev = st->st_rdev = r.r3 & 0x1F;
    if (r.r3 & 0x80) {
        st->st_mode = S_IFCHR | 0666;
        return 0;
    }
    st->st_mode = S_IFREG | 0666;
    long cur = raw_seek(fd, 0, SEEK_CUR);
    if (cur >= 0) {
        st->st_size = raw_seek(fd, 0, SEEK_END);
        raw_seek(fd, cur, SEEK_SET);
    }
    if (_dos21(0x5700, fd, 0, 0, &r) >= 0)
        st->st_mtime = st->st_atime = st->st_ctime =
            _armdos_dostime_to_unix(r.r3 & 0xFFFF, r.r2 & 0xFFFF);
    return 0;
}

/* The DOS "find" DTA (INT 21h AH=4Eh/4Fh). */
struct dosdta {
    uint8_t  reserved[21];
    uint8_t  attrib;
    uint16_t wr_time;
    uint16_t wr_date;
    uint32_t size;
    char     name[13];
} __attribute__((packed));

int _stat(const char *path, struct stat *st)
{
    struct armregs r;
    struct dosdta dta;
    uint32_t olddta;
    size_t len = strlen(path);

    memset(st, 0, sizeof *st);
    st->st_nlink = 1;
    if (len && path[1] == ':')
        st->st_dev = ((path[0] | 0x20) - 'a');
    /* roots ("\", "C:\", "C:") have no directory entry */
    if ((len == 1 && (path[0] == '\\' || path[0] == '/')) ||
        (len == 2 && path[1] == ':') ||
        (len == 3 && path[1] == ':' && (path[2] == '\\' || path[2] == '/'))) {
        if (len >= 2) {
            /* check the drive exists: get free space */
            if (_dos21(0x3600, 0, 0, (path[0] | 0x20) - 'a' + 1, &r) >= 0 && (r.r0 & 0xFFFF) == 0xFFFF) {
                errno = ENOENT;
                return -1;
            }
        }
        st->st_mode = S_IFDIR | 0777;
        return 0;
    }
    if (strpbrk(path, "*?")) { errno = ENOENT; return -1; }

    _dos21(0x2F00, 0, 0, 0, &r);
    olddta = r.r1;
    _dos21(0x1A00, 0, 0, (uint32_t)&dta, &r);
    int rc = _dos21(0x4E00, 0, 0x16, (uint32_t)path, &r);
    _dos21(0x1A00, 0, 0, olddta, &r);
    if (rc < 0)
        return -1;
    st->st_mode = (dta.attrib & 0x10) ? (S_IFDIR | 0777) : (S_IFREG | 0666);
    if (dta.attrib & 0x01) st->st_mode &= ~0222;
    st->st_size = (dta.attrib & 0x10) ? 0 : dta.size;
    st->st_mtime = st->st_atime = st->st_ctime = _armdos_dostime_to_unix(dta.wr_date, dta.wr_time);
    return 0;
}

int _isatty(int fd)
{
    struct armregs r;
    if (_dos21(0x4400, fd, 0, 0, &r) < 0)
        return 0;
    return (r.r3 & 0x80) != 0;
}

/* --------------------------------------------------------- files -------- */

int _unlink(const char *path)
{
    struct armregs r;
    return _dos21(0x4100, 0, 0, (uint32_t)path, &r) < 0 ? -1 : 0;
}

int _rename(const char *oldp, const char *newp)
{
    struct armregs r = {0};
    r.r0 = 0x5600;
    r.r3 = (uint32_t)oldp;      /* DS:DX */
    r.r5 = (uint32_t)newp;      /* ES:DI */
    if (_armdos_int21(&r)) return _armdos_seterr(r.r0);
    return 0;
}

/* newlib's rename() calls _rename_r, which (built without HAVE_RENAME)
 * does _link + _unlink - and there are no links on FAT: go to DOS. */
struct _reent;
int _rename_r(struct _reent *re, const char *oldp, const char *newp)
{
    (void)re;
    return _rename(oldp, newp);
}

int _link(const char *oldp, const char *newp)
{
    (void)oldp; (void)newp;
    errno = EMLINK;
    return -1;
}

int _mkdir(const char *path, mode_t mode)
{
    struct armregs r;
    (void)mode;
    return _dos21(0x3900, 0, 0, (uint32_t)path, &r) < 0 ? -1 : 0;
}

/* newlib's arm-none-eabi libc has no mkdir() wrapper around _mkdir, so a
 * POSIX program (sys/stat.h, no direct.h) would not link. (DOOM port) */
int mkdir(const char *path, mode_t mode)
{
    return _mkdir(path, mode);
}

int rmdir(const char *path)
{
    struct armregs r;
    return _dos21(0x3A00, 0, 0, (uint32_t)path, &r) < 0 ? -1 : 0;
}

int chdir(const char *path)
{
    struct armregs r;
    return _dos21(0x3B00, 0, 0, (uint32_t)path, &r) < 0 ? -1 : 0;
}

char *getcwd(char *buf, size_t size)
{
    struct armregs r;
    char tmp[68];
    int drive;
    size_t len;

    drive = _dos21(0x1900, 0, 0, 0, &r) & 0xFF;
    tmp[0] = 'A' + drive;
    tmp[1] = ':';
    tmp[2] = '\\';
    memset(&r, 0, sizeof r);
    r.r0 = 0x4700;
    r.r3 = 0;                       /* DL = 0: default drive */
    r.r4 = (uint32_t)(tmp + 3);     /* DS:SI */
    if (_armdos_int21(&r)) { _armdos_seterr(r.r0); return 0; }
    tmp[sizeof tmp - 1] = 0;
    len = strlen(tmp) + 1;
    if (!buf) {
        if (size < len) size = len;
        buf = malloc(size);
        if (!buf) { errno = ENOMEM; return 0; }
    } else if (size < len) {
        errno = ERANGE;
        return 0;
    }
    memcpy(buf, tmp, len);
    return buf;
}

/* ------------------------------------------------------ process -------- */

extern unsigned int _psp;
int _getpid(void) { return (int)_psp; }

int _kill(int pid, int sig)
{
    if (pid != _getpid()) { errno = ESRCH; return -1; }
    if (sig == SIGABRT) {
        static const char msg[] = "\r\nabnormal program termination\r\n";
        raw_write(2, msg, sizeof msg - 1);
        _exit(3);
    }
    _exit(128 + (sig & 0x7F));
    return 0;
}

/* ------------------------------------------------------------ time ------ */

int _gettimeofday(struct timeval *tv, void *tz)
{
    struct armregs r;
    unsigned y, mo, d, h, mi, s, cs, d2;
    (void)tz;
    do {
        _dos21(0x2A00, 0, 0, 0, &r);
        y = r.r2 & 0xFFFF; mo = (r.r3 >> 8) & 0xFF; d = r.r3 & 0xFF;
        _dos21(0x2C00, 0, 0, 0, &r);
        h = (r.r2 >> 8) & 0xFF; mi = r.r2 & 0xFF; s = (r.r3 >> 8) & 0xFF; cs = r.r3 & 0xFF;
        _dos21(0x2A00, 0, 0, 0, &r);
        d2 = r.r3 & 0xFF;
    } while (d2 != d);              /* crossed midnight: read again */
    if (tv) {
        tv->tv_sec = days_from_civil(y, mo, d) * 86400L + h * 3600L + mi * 60L + s;
        tv->tv_usec = cs * 10000L;
    }
    return 0;
}

/* BIOS ticks (18.2065 Hz) since program start, in CLOCKS_PER_SEC units. */
clock_t _times(struct tms *t)
{
    uint32_t now = ARMDOS_BIOS_TICKS;
    uint32_t ticks = now >= _armdos_start_ticks ? now - _armdos_start_ticks
                                                : now + 0x1800B0u - _armdos_start_ticks;
    clock_t c = (clock_t)((unsigned long long)ticks * CLOCKS_PER_SEC * 10000ull / 182065ull);
    if (t) {
        t->tms_utime = c;
        t->tms_stime = 0;
        t->tms_cutime = 0;
        t->tms_cstime = 0;
    }
    return c;
}
