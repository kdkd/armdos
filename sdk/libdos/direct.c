/* direct.c - direct.h */
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <direct.h>
#include "libdos.h"

int _armdos_mkdir(const char *path)
{
    struct armregs r;
    return _dos21(0x3900, 0, 0, (uint32_t)path, &r) < 0 ? -1 : 0;
}

int _getdrive(void)
{
    struct armregs r;
    return (_dos21(0x1900, 0, 0, 0, &r) & 0xFF) + 1;
}

int _chdrive(int drive)
{
    struct armregs r;
    if (drive < 1 || drive > 26) { errno = EINVAL; return -1; }
    _dos21(0x0E00, 0, 0, drive - 1, &r);
    if (_getdrive() != drive) { errno = EACCES; return -1; }
    return 0;
}

char *_getdcwd(int drive, char *buf, int size)
{
    struct armregs r = {0};
    char tmp[68];
    size_t len;
    if (drive == 0) drive = _getdrive();
    tmp[0] = 'A' + drive - 1; tmp[1] = ':'; tmp[2] = '\\';
    r.r0 = 0x4700;
    r.r3 = drive;
    r.r4 = (uint32_t)(tmp + 3);
    if (_armdos_int21(&r)) { _armdos_seterr(r.r0); return 0; }
    len = strlen(tmp) + 1;
    if (!buf) {
        if ((size_t)size < len) size = len;
        if (!(buf = malloc(size))) { errno = ENOMEM; return 0; }
    } else if ((size_t)size < len) { errno = ERANGE; return 0; }
    memcpy(buf, tmp, len);
    return buf;
}
