/* speech.c - TERM speaks what the other end asks it to (docs/MODEM.md):
 *
 *     ESC P speak[:<voice>] ; <text> ESC \          (a DCS string)
 *
 * is spoken through the Sound Blaster with DRARM's text-to-speech engine (apps/drarm) while the
 * text itself is printed by the other end as usual. Voices: default, and "wopr" - a flat,
 * slightly high, slow monotone for WarGames' WOPR (399-2364). Utterances queue up; the queue is
 * pumped from TERM's main loop. Without a Sound Blaster (or with Alt-V off) the strings are
 * simply ignored, like any terminal ignores a DCS string it does not know.
 */
#include <string.h>
#include "tts.h"
#include "speak.h"
#include "term.h"
#include "lib/comm.h"

#define QSZ 1536
static char q[QSZ];           /* utterances: <voice byte><text>\0 ... */
static int qlen;
static int mode = -1;          /* -1 not tried yet, else SPK_* */
int speech_on = 1;

static void voice(int v)
{
    if (v == 'w') { tts_settings(7, 3, 5, 9); tts_monotone = 1; }     /* WOPR: flat, a little high, slow */
    else { tts_settings(5, 5, 5, 7); tts_monotone = 0; }
}

void speech_dcs(const char *s)
{
    int v = 'd';
    if (strncmp(s, "speak", 5)) return;
    s += 5;
    if (*s == ':') { if (!strncmp(s + 1, "wopr", 4)) v = 'w'; while (*s && *s != ';') s++; }
    if (*s != ';') return;
    s++;
    if (!speech_on || !*s) return;
    if (mode < 0) mode = speak_init(SPK_SB);          /* never the PC speaker: it takes the timer */
    if (mode != SPK_SB) return;
    int n = (int)strlen(s);
    if (qlen + n + 2 > QSZ) return;                   /* too much queued: drop it */
    q[qlen++] = (char)v;
    memcpy(q + qlen, s, n + 1);
    qlen += n + 1;
}

/* call often: starts the next utterance when the last one is done, feeds the sound card */
int speech_pump(void)
{
    static uint32_t quiet_since;
    if (mode != SPK_SB) return 0;
    int busy = speak_pump();
    if (busy || qlen) quiet_since = TICKS();
    else if (TICKS() - quiet_since > 15 * 18) {   /* 15 s of silence: give the Sound Blaster back */
        speak_shutdown(); mode = -1;
        return 0;
    }
    if (!busy && qlen) {
        char text[240];            /* WOPR's lines are short; the synthesiser is sized for this */
        int v = q[0];
        int n = (int)strlen(q + 1);
        if (n > (int)sizeof text - 1) n = sizeof text - 1;
        memcpy(text, q + 1, n); text[n] = 0;
        int used = (int)strlen(q + 1) + 2;
        memmove(q, q + used, qlen - used);
        qlen -= used;
        voice(v);
        speak_start(text);
        busy = 1;
    }
    return busy;
}

void speech_stop(void)
{
    qlen = 0;
    if (mode == SPK_SB) { speak_stop(); speak_shutdown(); }
    mode = -1;
}
