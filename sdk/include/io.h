/* io.h - low-level (handle) I/O, Microsoft C style. */
#ifndef _ARMDOS_IO_H
#define _ARMDOS_IO_H
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#ifdef __cplusplus
extern "C" {
#endif

/* open/read/write/close/lseek/access/unlink come from <unistd.h>/<fcntl.h>.
 * Handles opened without O_BINARY translate CR/LF (see _fmode, setmode). */
int  setmode(int fd, int mode);         /* O_TEXT / O_BINARY; returns old mode */
long filelength(int fd);
long tell(int fd);
int  eof(int fd);                       /* 1 at end of file, 0 if not, -1 error */
int  chmod(const char *path, mode_t mode);  /* S_IWRITE clear -> read-only */
int  dup(int fd);
int  dup2(int fd, int fd2);
int  creat(const char *path, mode_t mode);
mode_t umask(mode_t mask);

#ifndef S_IREAD
#define S_IREAD  S_IRUSR
#endif
#ifndef S_IWRITE
#define S_IWRITE S_IWUSR
#endif

#define _open open
#define _close close
#define _read read
#define _write write
#define _lseek lseek
#define _setmode setmode
#define _filelength filelength
#define _tell tell
#define _eof eof
#define _access access
#define _chmod chmod
#define _dup dup
#define _dup2 dup2
#define _creat creat
#define _umask umask
#define _unlink unlink

#ifdef __cplusplus
}
#endif
#endif
