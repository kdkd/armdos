/* door.h - the small door library shared by The ARM Pit's door games
 * (apps/doors/<game>). Built on the BBS's own session I/O (apps/bbs/sio.c: @X
 * colour codes, caller + local mirror, More prompts, carrier/time checks)
 * and the comm library of apps/term/lib.
 *
 * A door is started by BBS.EXE as   DOOR.EXE <directory with DOOR.SYS>
 * (DORINFO1.DEF is read when there is no DOOR.SYS), or   DOOR.EXE /L   to
 * play locally. The door opens the COM port named in the drop file itself,
 * keeps DTR up and never drops it on exit, as doors did.
 *
 * Typical main():
 *
 *     int main(int argc, char **argv)
 *     {
 *         door_init(argc, argv, my_status_bar);
 *         if (setjmp(sio_drop) == 0) {
 *             ... play, using sio_puts/sio_printf/sio_hotkey/sio_getline ...
 *         } else {
 *             ... carrier lost / time up / idle: save state quietly ...
 *         }
 *         door_exit();
 *         return 0;
 *     }
 *
 * Conventions every door follows (the BBS's "Today's news" bulletin reads them):
 *   NEWS.TXT    in the door's directory: one headline per line, "MM-DD-YY  text"
 *               (door_news appends; the BBS shows the last few).
 *   SCORES.TXT  in the door's directory: a short (<= 8 lines) plain-text score
 *               table the door rewrites whenever it changes.
 */
#ifndef DOOR_H
#define DOOR_H
#include <setjmp.h>
#include <stdint.h>
#include "../../bbs/sio.h"
#include "../../term/lib/comm.h"
#include "../../term/lib/scr.h"

extern char door_user[36];     /* caller's name, "First Last" as the BBS has it */
extern char door_bbs[40];      /* board name */
extern int  door_port;         /* 1-4, 0 = local */
extern long door_baud;
extern int  door_ansi;         /* caller has ANSI colour */
extern int  door_minutes;      /* minutes left for the call */
extern int  door_sec;          /* security level */
extern int  door_local;        /* /L on the command line or no drop file */

/* parse the command line and the drop file, open the port, start the session.
 * status (may be NULL) paints the local two-line status bar (rows 23-24). */
void door_init(int argc, char **argv, void (*status)(void));
void door_exit(void);          /* flush, release the port with DTR up */

/* dates: MM-DD-YY and a day number that changes at midnight */
void door_today(char *b);
int  door_daynum(void);
int  door_rnd(int n);          /* 0..n-1 */

/* append "MM-DD-YY  text" to NEWS.TXT in the current directory */
void door_news(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* the local status bar helper: two 80-column lines at rows 23-24 */
void door_bar(int attr, const char *line1, const char *line2);

/* first word of a name ("Ada Lovelace" -> "Ada") */
const char *door_first(const char *name, char *buf, int n);
#endif
