/* say.c - SAY.EXE: speak text through the Sound Blaster (or the PC speaker).
 *
 *   SAY [/P:n] [/S:n] [/T:n] [/V:n] [/PC] [/E] [text]
 *
 * With text on the command line, speaks it; otherwise reads standard input
 * (SAY < LETTER.TXT, TYPE X | SAY) and speaks it a sentence at a time.
 * /P pitch, /S speed, /T tone, /V volume: 0-9 (default 5). /PC forces the PC
 * speaker, /E echoes each sentence as it is spoken.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <armdos.h>
#include "tts.h"
#include "speak.h"

/* diagnostics on port E9 (the emulator's debug console) at exit */
static void report_underruns(void)
{
    char b[48];
    sprintf(b, "SPEAK underruns=%lu\n", speak_underruns());
    armdos_debug(b);
}

static int echo;

static void say(const char *t)
{
    const char *p = t;
    while (*p == ' ' || *p == '\t') p++;
    if (!*p) return;
    if (echo) { printf("%s\n", p); fflush(stdout); }
    speak_start(p);
    while (speak_pump()) {
        if (speak_break_pending()) {
            speak_shutdown();
            printf("^C\n");
            exit(0);
        }
        armdos_halt();
    }
}

static void usage(void)
{
    printf("Speaks text through the Sound Blaster.\n\n"
           "SAY [/P:n] [/S:n] [/T:n] [/V:n] [/PC] [/E] [text]\n\n"
           "  text   The text to speak. Without it, SAY reads standard input.\n"
           "  /P:n   Pitch  0-9 (5)\n  /S:n   Speed  0-9 (5)\n  /T:n   Tone   0-9 (5)\n"
           "  /V:n   Volume 0-9 (5)\n  /PC    Use the PC speaker\n  /E     Echo the text\n");
}

int main(int argc, char **argv)
{
    int p = 5, s = 5, t = 5, v = 5, i, want = SPK_AUTO;
    static char text[2048];
    int tl = 0;
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (a[0] == '/' && a[1]) {
            int c = toupper((unsigned char)a[1]);
            const char *n = a + 2;
            if (*n == ':' || *n == '=') n++;
            if (c == '?') { usage(); return 0; }
            if (c == 'P' && toupper((unsigned char)a[2]) == 'C') { want = SPK_PCSPK; continue; }
            if (c == 'E') { echo = 1; continue; }
            if (!isdigit((unsigned char)*n) || (c != 'P' && c != 'S' && c != 'T' && c != 'V')) {
                printf("Invalid switch - %s\n", a); return 1;
            }
            if (c == 'P') p = atoi(n); else if (c == 'S') s = atoi(n); else if (c == 'T') t = atoi(n); else v = atoi(n);
            continue;
        }
        if (tl + (int)strlen(a) + 2 < (int)sizeof text) { if (tl) text[tl++] = ' '; strcpy(text + tl, a); tl += (int)strlen(a); }
    }
    tts_settings(p, s, t, v);
    speak_init(want);
    atexit(report_underruns);
    speak_break_install();
    if (tl) { say(text); return 0; }
    /* standard input, a sentence at a time */
    {
        int c;
        tl = 0;
        while ((c = getchar()) != EOF) {
            if (speak_break) { printf("^C\n"); return 0; }
            if (c == '\r') continue;
            if (c == '\n' || c == '\t') c = ' ';
            if (tl < (int)sizeof text - 1) text[tl++] = (char)c;
            if ((c == '.' || c == '?' || c == '!') || tl > 400 || (c == ' ' && tl > 300)) {
                int d = getchar();
                if (d == EOF || d == ' ' || d == '\n' || d == '\r' || tl > 400) { text[tl] = 0; say(text); tl = 0; }
                if (d != EOF) ungetc(d, stdin);
            }
        }
        text[tl] = 0; say(text);
    }
    return 0;
}
