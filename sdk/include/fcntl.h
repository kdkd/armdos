/* fcntl.h - newlib's fcntl.h plus the DOS text/binary open flags. */
#ifndef _ARMDOS_FCNTL_H
#define _ARMDOS_FCNTL_H

#include_next <fcntl.h>

/* Same values newlib's fopen() passes for "b" (_FBINARY), so fopen("rb")
 * gives a binary handle and fopen("r") a text one (unless _fmode says
 * otherwise). */
#ifndef O_BINARY
#define O_BINARY 0x10000
#endif
#ifndef O_TEXT
#define O_TEXT   0x20000
#endif
#ifndef O_RAW
#define O_RAW    O_BINARY
#endif
#define _O_BINARY O_BINARY
#define _O_TEXT   O_TEXT
#define _O_RDONLY O_RDONLY
#define _O_WRONLY O_WRONLY
#define _O_RDWR   O_RDWR
#define _O_APPEND O_APPEND
#define _O_CREAT  O_CREAT
#define _O_TRUNC  O_TRUNC
#define _O_EXCL   O_EXCL

/* Default translation mode for open()/fopen() without O_TEXT/O_BINARY
 * ("b"/"t"). O_TEXT at start-up, as in Microsoft C. */
extern int _fmode;

#endif
