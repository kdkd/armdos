/* term.h - what term.c shares with the script interpreter (script.c). */
#ifndef TERM_H
#define TERM_H
#include <stdint.h>
#include "lib/vt.h"

struct config {
    int  port;
    long baud;
    char init[48];
    char dialpfx[16];
    char hangup[16];
    char dldir[64];
    int  redial_max;
    int  redial_pause;              /* seconds */
    int  autozm;
    int  resume;
    int  echo;
};
extern struct config cfg;
extern struct vt vt;
extern int online;
extern char homedir[80];

/* term.c */
void status(void);
void pump(void);
void check_carrier(void);
int  dial(const char *name, const char *number, long baud);    /* 0 = CONNECT */
void hangup(void);
int  ask_yn(const char *q);
void message(const char *title, const char *msg, int ms);
int  do_download(int proto);                                   /* ZM_* / 0 = ok */
int  send_files(int proto, char *const *list, int n);          /* ZM_* / 0 = ok */
void term_exit(int code);
extern int xfer_auto_pending;       /* a ZMODEM auto-download ran ... */
extern int xfer_auto_rc;            /* ... with this result */

/* speech.c: ESC P speak[:voice];text ESC \ spoken through the Sound Blaster */
extern int speech_on;
void speech_dcs(const char *s);
int  speech_pump(void);
void speech_stop(void);

/* script.c */
extern int script_running;
extern int script_stopped;          /* Esc or an error stopped it: Alt-X then exits with errorlevel 1 */
extern char script_name[16];
void script_rx(int c);              /* every byte received from the line */
int  script_find(const char *arg, char *path, int max);
void script_run(const char *path);
#endif
