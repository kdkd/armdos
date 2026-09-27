/* speak.h - speech output on ARM-DOS (Sound Blaster, or the PC speaker) */
#ifndef DRARM_SPEAK_H
#define DRARM_SPEAK_H

enum { SPK_NONE, SPK_SB, SPK_PCSPK, SPK_AUTO };

int  speak_init(int want);          /* returns the mode in use */
int  speak_mode(void);
void speak_shutdown(void);
void speak_start(const char *text); /* plans the utterance and starts playing */
int  speak_pump(void);              /* call often; 1 while still speaking */
long speak_position(void);          /* samples of this utterance played so far */
void speak_stop(void);
int  speak_busy(void);
unsigned long speak_underruns(void);   /* ring-buffer underruns (samples) so far */

/* Ctrl-C / Ctrl-Break: install the INT 23h/1Bh flag handlers (restored at
 * exit); speak_break_pending() says whether one was pressed (and consumes a
 * waiting ^C key). */
void speak_break_install(void);
int  speak_break_pending(void);
extern volatile int speak_break;

#endif
