/* sio.h - session I/O for the BBS and its doors: output goes to the caller
 * (COM port) and to the local screen (through the ANSI emulator, so the
 * sysop sees what the caller sees); input comes from either. Carrier loss
 * and running out of time end the session through sio_drop (longjmp). */
#ifndef SIO_H
#define SIO_H
#include <setjmp.h>
#include <stdint.h>
#include "../term/lib/vt.h"

#define SIO_CARRIER 1       /* longjmp values */
#define SIO_TIMEUP  2
#define SIO_IDLE    3
#define SIO_HANGUP  4       /* sysop pressed Alt-H */

extern int sio_remote;              /* a caller on the modem (else local only) */
extern int sio_ansi;                /* caller has ANSI */
extern int sio_lines;               /* caller's screen length */
extern uint32_t sio_deadline;       /* BIOS ticks when time is up (0 = none) */
extern uint32_t sio_idle_ticks;     /* inactivity limit (0 = none) */
extern jmp_buf sio_drop;
extern struct vt sio_vt;            /* the local mirror */
extern void (*sio_status_hook)(void);
extern int sio_linecount;           /* lines since the last pause (for More) */

void sio_begin(int remote, int ansi, int local_rows);
void sio_putc(int c);               /* raw byte */
void sio_write(const char *s, int n);
void sio_puts(const char *s);       /* @Xbf colour codes, \n -> CR LF, ANSI stripped if !ansi */
void sio_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void sio_color(int attr);           /* PC attribute -> ANSI */
void sio_cls(void);
void sio_flush(void);
int  sio_key(void);                 /* blocking; -1 never; ESC sequences give K_* codes */
int  sio_key_timeout(int ms);       /* -1 timeout */
int  sio_getline(char *buf, int max, int mode);
#define GL_UPPER 1
#define GL_PASSWORD 2
#define GL_NAME 4                   /* Capitalise Words */
#define GL_DIGITS 8
int  sio_hotkey(const char *valid); /* uppercase key out of valid (Enter -> first if ' ' in valid) */
int  sio_yesno(const char *q, int def);
int  sio_more(void);                /* "More [Y,n,=]?"; 0 = stop */
int  sio_line_done(void);           /* count a line; asks More when the screen is full; 0 = stop */
void sio_pause(void);               /* "Press [Enter] to continue" */
int  sio_showfile(const char *path);/* .ANS/.TXT with @X codes; 0 if missing */
uint32_t sio_minutes_left(void);
int  sio_carrier(void);
void sio_touch(void);               /* count as input (after a long file transfer) */
#endif
