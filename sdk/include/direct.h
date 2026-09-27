/* direct.h - directories and drives, Microsoft C style. */
#ifndef _ARMDOS_DIRECT_H
#define _ARMDOS_DIRECT_H
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __cplusplus
extern "C" {
#endif

/* getcwd, chdir and rmdir come from <unistd.h>. getcwd(NULL, n) mallocs. */
int _armdos_mkdir(const char *path);
/* MS C's one-argument mkdir; the POSIX two-argument form also works. */
#define mkdir(path, ...) _armdos_mkdir(path)
#define _mkdir(path) _armdos_mkdir(path)
#define _getcwd getcwd
#define _chdir chdir
#define _rmdir rmdir

int _getdrive(void);            /* 1 = A: */
int _chdrive(int drive);        /* 1 = A:; 0 on success */
char *_getdcwd(int drive, char *buf, int size);   /* drive 0 = default */

#ifdef __cplusplus
}
#endif
#endif
