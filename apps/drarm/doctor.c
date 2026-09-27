/* doctor.c - DOCTOR.EXE: Dr. ARMitso, the talking psychologist.
 *
 *   DOCTOR [/PC] [/Q]        /PC: speak through the PC speaker, /Q: silent
 *
 * White upper-case text on a blue screen; every line the doctor prints is
 * spoken as it appears (the words are revealed in step with the voice). The
 * patient's name is spelled aloud letter by letter as it is typed. See
 * README.md for the commands and the easter eggs.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dos.h>
#include <bios.h>
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
#include "eliza.h"

#define A_TEXT   0x1F      /* bright white on blue */
#define A_USER   0x1E      /* yellow on blue */
#define A_BOX    0x1B      /* cyan on blue */
#define A_TITLE  0x1F
#define TOP      5         /* first row of the conversation */
#define BOTTOM   24
#define WIDTH    80

static volatile unsigned short *vram;

/* the BIOS tick count at 0040:006C (through a register, so the compiler does
 * not treat the constant address as an empty array) */
static unsigned long bios_ticks(void)
{
    volatile unsigned long *p;
    __asm__("" : "=r"(p) : "0"(0x46C));
    return *p;
}
static int cx, cy;
static char patient[32];
static int quiet;

/* Ctrl-C / Ctrl-Break: stop talking and go back to DOS at once, the same
 * way QUIT leaves (silence, the video mode reset, vectors restored at exit) */
static void check_break(void)
{
    union REGS r;
    if (!speak_break_pending()) return;
    speak_shutdown();
    r.x.ax = 0x0003; int86(0x10, &r, &r);
    exit(0);
}

/* ---- screen ---------------------------------------------------------------- */
static void set_cursor(int x, int y)
{
    union REGS r;
    r.h.ah = 2; r.h.bh = 0; r.h.dh = (unsigned char)y; r.h.dl = (unsigned char)x;
    int86(0x10, &r, &r);
}

static void putc_at(int x, int y, int c, int a) { vram[y * WIDTH + x] = (unsigned short)((a << 8) | (unsigned char)c); }

static void fill_rows(int y0, int y1, int a)
{
    int i;
    for (i = y0 * WIDTH; i < (y1 + 1) * WIDTH; i++) vram[i] = (unsigned short)((a << 8) | ' ');
}

static void puts_at(int x, int y, const char *s, int a) { while (*s && x < WIDTH) putc_at(x++, y, (unsigned char)*s++, a); }

static void scroll_up(void)
{
    int i;
    for (i = TOP * WIDTH; i < BOTTOM * WIDTH; i++) vram[i] = vram[i + WIDTH];
    fill_rows(BOTTOM, BOTTOM, A_TEXT);
}

static void newline(void)
{
    cx = 0;
    if (++cy > BOTTOM) { scroll_up(); cy = BOTTOM; }
    set_cursor(cx, cy);
}

static void out_char(int c, int a)
{
    if (c == '\n') { newline(); return; }
    if (cx >= WIDTH) newline();
    putc_at(cx++, cy, c, a);
    set_cursor(cx < WIDTH ? cx : WIDTH - 1, cy);
}

static void banner(void)
{
    int i;
    fill_rows(0, 24, A_TEXT);
    putc_at(0, 0, 0xC9, A_BOX); putc_at(79, 0, 0xBB, A_BOX);
    putc_at(0, 3, 0xC8, A_BOX); putc_at(79, 3, 0xBC, A_BOX);
    for (i = 1; i < 79; i++) { putc_at(i, 0, 0xCD, A_BOX); putc_at(i, 3, 0xCD, A_BOX); }
    putc_at(0, 1, 0xBA, A_BOX); putc_at(79, 1, 0xBA, A_BOX);
    putc_at(0, 2, 0xBA, A_BOX); putc_at(79, 2, 0xBA, A_BOX);
    puts_at(3, 1, "DR. ARMITSO", 0x1E);
    puts_at(17, 1, "-  ARM Intelligent Text-to-Speech Operator", A_BOX);
    puts_at(66, 1, "Version 1.0", A_BOX);
    puts_at(3, 2, "for the Sound Blaster", A_BOX);
    puts_at(38, 2, "(C) Copyright Europa Micro Systems 1991", A_BOX);
    cx = 0; cy = TOP;
    set_cursor(cx, cy);
}

/* ---- speaking and showing a line together ----------------------------------- */
static void wrap(const char *s, char *d, int col)
{
    /* copy s to d replacing spaces with '\n' where a word would pass the edge */
    int i, n = (int)strlen(s), x = col, last_sp = -1;
    strcpy(d, s);
    for (i = 0; i < n; i++) {
        if (d[i] == ' ') last_sp = i;
        if (++x >= WIDTH - 1 && last_sp >= 0) {
            d[last_sp] = '\n';
            x = i - last_sp;
            last_sp = -1;
        }
    }
}

static int esc_pressed(void)
{
    unsigned k;
    while ((k = _bios_keybrd(_KEYBRD_READY)) != 0) {
        if ((k & 0xFF) == 27) { _bios_keybrd(_KEYBRD_READ); return 1; }
        return 0;           /* leave other keys for the input line */
    }
    return 0;
}

/* print `text` in `attr` and speak it; words appear as they are spoken */
static void say(const char *text, int attr)
{
    static char disp[600];
    int shown = 0, n, w = 0;
    if ((int)strlen(text) >= (int)sizeof disp) return;
    wrap(text, disp, cx);
    n = (int)strlen(disp);
    if (!quiet) speak_start(text);
    if (quiet || speak_mode() == SPK_NONE) {
        while (shown < n) out_char(disp[shown++], attr);
        newline();
        return;
    }
    for (;;) {
        int busy = speak_pump();
        long pos = speak_position();
        int upto = shown;
        while (w < tts_word_count() && tts_word_sample(w) <= pos) {
            w++;
            upto = w < tts_word_count() ? tts_word_offset(w) : n;
        }
        if (!busy) upto = n;
        /* reveal up to (not including) the next word's first character */
        while (shown < upto && shown < n) out_char(disp[shown++], attr);
        if (!busy) break;
        if (esc_pressed()) { speak_stop(); }
        check_break();
        armdos_halt();
    }
    newline();
}

/* a pause of `ms` milliseconds (18.2 Hz ticks) */
static void pause_ms(int ms)
{
    unsigned long t0 = bios_ticks(), n = (unsigned long)ms * 182 / 10000 + 1;
    while (bios_ticks() - t0 < n) { speak_pump(); check_break(); armdos_halt(); }
}

/* ---- input ---------------------------------------------------------------- */
static unsigned getkey(void)
{
    unsigned k;
    for (;;) {
        check_break();
        if ((k = _bios_keybrd(_KEYBRD_READY)) != 0) break;
        speak_pump(); armdos_halt();
    }
    return _bios_keybrd(_KEYBRD_READ);
}

/* read a line (upper case) at the cursor; `spell`: speak each letter */
static int read_line(char *buf, int max, int spell)
{
    int n = 0, x0 = cx, y0 = cy;
    buf[0] = 0;
    for (;;) {
        unsigned k = getkey();
        int c = k & 0xFF;
        if (c == '\r') break;
        if (c == 8) {
            if (n) { n--; buf[n] = 0; cx--; if (cx < 0) { cx = WIDTH - 1; cy--; } putc_at(cx, cy, ' ', A_USER); set_cursor(cx, cy); }
            continue;
        }
        if (c == 27) {   /* Esc clears the line */
            while (n) { n--; cx--; if (cx < 0) { cx = WIDTH - 1; cy--; } putc_at(cx, cy, ' ', A_USER); }
            buf[0] = 0; set_cursor(cx, cy);
            continue;
        }
        if (c < 32 || c > 126 || n >= max) continue;
        if (spell && !(isalpha(c) || c == ' ')) continue;
        c = toupper(c);
        buf[n++] = (char)c; buf[n] = 0;
        out_char(c, A_USER);
        if (spell && c != ' ' && !quiet) {
            char l[2] = { (char)c, 0 };
            tts_spell_mode = 1;
            speak_start(l);
            tts_spell_mode = 0;
        }
    }
    (void)x0; (void)y0;
    while (spell && speak_pump()) { check_break(); armdos_halt(); }
    newline();
    return n;
}

/* ---- the crash gag ---------------------------------------------------------- */
static unsigned rnd_state = 12345;
static unsigned rnd(void) { rnd_state = rnd_state * 1103515245u + 12345u; return (rnd_state >> 16) & 0x7FFF; }

static void garbled_line(const char *text)
{
    /* the doctor's words decay into noise as he says them */
    static char g[200];
    int i, n = (int)strlen(text);
    static const char junk[] = "#$%&*@!?~^{}[]<>|\\/";
    if (n > (int)sizeof g - 1) n = sizeof g - 1;
    for (i = 0; i < n; i++) {
        g[i] = text[i];
        if (i > n / 3 && text[i] != ' ' && (int)(rnd() % 100) < (i * 100 / n)) g[i] = junk[rnd() % (sizeof junk - 1)];
    }
    g[n] = 0;
    {
        int a = cx;
        tts_settings(5, 7, 5, 5);
        speak_start(text);
        for (i = 0; i < n; i++) {
            out_char(g[i], A_TEXT);
            if (i % 3 == 0) pause_ms(40);
        }
        while (speak_pump()) { check_break(); armdos_halt(); }
        (void)a;
        newline();
    }
}

static void crash_gag(const char *line)
{
    int p, s, t, v, i;
    static unsigned short save[80 * 25];
    char buf[90];
    tts_get_settings(&p, &s, &t, &v);
    garbled_line(line);
    /* stutter */
    tts_settings(9, 9, 9, v);
    for (i = 0; i < 3; i++) { speak_start("ER ER ER"); while (speak_pump()) { check_break(); armdos_halt(); } }
    memcpy(save, (void *)vram, sizeof save);
    /* the abort screen */
    fill_rows(0, 24, 0x07);
    puts_at(0, 1, "  *** ARM DATA ABORT ***", 0x4F);
    puts_at(0, 3, "  Exception in DOCTOR.EXE at PC=0001F3A8   LDR r0,[r4,#8]", 0x0F);
    puts_at(0, 4, "  FSR=00000808  (external abort: parity error in patient data)", 0x07);
    puts_at(0, 5, "  FAR=DEADBEE8", 0x07);
    for (i = 0; i < 16; i++) {
        static const unsigned vals[16] = { 0x66756300, 0x0000002A, 0x00000BAD, 0x00000003, 0xDEADBEE0, 0x0001C0DE,
            0x00000000, 0x00000640, 0x0000F00D, 0x00000007, 0x00000220, 0x0001FFE4, 0x00000001, 0x0009FFC0, 0x0001F3A0, 0x0001F3A8 };
        sprintf(buf, "r%-2d=%08X", i, vals[i]);
        puts_at(2 + (i % 4) * 16, 7 + i / 4, buf, 0x07);
    }
    puts_at(2, 11, "CPSR=600000DF  (nZCv, IRQ FIQ off, SYS mode)", 0x07);
    puts_at(2, 13, "Psychology module has performed an illegal operation.", 0x0F);
    set_cursor(0, 24);
    tts_settings(2, 3, 2, v);
    speak_start("DATA ABORT. DATA ABORT.");
    while (speak_pump()) { check_break(); armdos_halt(); }
    pause_ms(600);
    puts_at(2, 15, "Attempting recovery", 0x07);
    for (i = 0; i < 12; i++) { putc_at(21 + i, 15, '.', 0x07); pause_ms(150); }
    puts_at(2, 16, "Flushing caches ................ OK", 0x07);
    pause_ms(300);
    puts_at(2, 17, "Reloading psychology module .... OK", 0x07);
    pause_ms(300);
    puts_at(2, 18, "Restoring patient from core dump OK", 0x07);
    pause_ms(700);
    tts_settings(p, s, t, v);
    memcpy((void *)vram, save, sizeof save);
    set_cursor(cx, cy);
    sprintf(buf, "SORRY %s, I HAD A LITTLE BREAKDOWN THERE. WHERE WERE WE?", patient);
    say(buf, A_TEXT);
}

/* ---- commands --------------------------------------------------------------- */
static void show_help(void)
{
    static const char *const lines[] = {
        "  HELP            THIS LIST",
        "  SAY <TEXT>      I WILL REPEAT WHATEVER YOU TYPE",
        "  .READ <FILE>    I WILL READ A TEXT FILE ALOUD",
        "  .PHON <TEXT>    SHOW HOW I PRONOUNCE SOMETHING",
        "  PITCH n         MY VOICE PITCH, 0-9",
        "  SPEED n         HOW FAST I TALK, 0-9",
        "  TONE n          BASS 0 TO TREBLE 9",
        "  VOLUME n        HOW LOUD I AM, 0-9",
        "  PARAM           SHOW MY VOICE SETTINGS",
        "  CLS             CLEAR THE SCREEN",
        "  NEW             A NEW PATIENT",
        "  QUIT            END THE SESSION",
        0 };
    int i;
    say("HERE ARE THE THINGS I UNDERSTAND.", A_TEXT);
    for (i = 0; lines[i]; i++) { const char *p = lines[i]; while (*p) out_char(*p++, A_BOX); newline(); }
}

static void show_params(void)
{
    int p, s, t, v;
    char b[120];
    tts_get_settings(&p, &s, &t, &v);
    sprintf(b, "PITCH %d, SPEED %d, TONE %d, VOLUME %d.", p, s, t, v);
    say(b, A_TEXT);
}

static void read_file(const char *name)
{
    FILE *f = fopen(name, "r");
    char line[200], b[120];
    if (!f) { sprintf(b, "I CANNOT FIND %s. ARE YOU SURE IT EXISTS?", name); say(b, A_TEXT); return; }
    while (fgets(line, sizeof line, f)) {
        int l = (int)strlen(line), i;
        while (l && (line[l - 1] == '\n' || line[l - 1] == '\r')) line[--l] = 0;
        for (i = 0; i < l; i++) line[i] = (char)toupper((unsigned char)line[i]);
        if (l) say(line, A_TEXT);
        if (esc_pressed()) break;
    }
    fclose(f);
}

static int word_is(const char *s, const char *w, const char **rest)
{
    size_t n = strlen(w);
    if (strncmp(s, w, n)) return 0;
    if (s[n] && s[n] != ' ') return 0;
    if (rest) { *rest = s + n; while (**rest == ' ') (*rest)++; }
    return 1;
}

/* returns 1 to quit, 2 for a new patient */
static int command(const char *in)
{
    const char *rest;
    const char *s = in;
    char b[200];
    int p, sp, t, v;
    while (*s == ' ') s++;
    tts_get_settings(&p, &sp, &t, &v);
    if (word_is(s, "QUIT", 0) || word_is(s, "EXIT", 0) || word_is(s, "BYE", 0) || word_is(s, "GOODBYE", 0)
        || word_is(s, ".QUIT", 0)) {
        sprintf(b, "GOODBYE, %s. REMEMBER, I AM ALWAYS HERE, UNLESS SOMEBODY SWITCHES ME OFF.", patient);
        say(b, A_TEXT);
        return 1;
    }
    if (word_is(s, "HELP", &rest) && !*rest) { show_help(); return 0; }
    if (word_is(s, "HELP", 0) || word_is(s, ".HELP", 0) || word_is(s, "?", 0)) { show_help(); return 0; }
    if (word_is(s, "SAY", &rest) || word_is(s, ".SAY", &rest)) {
        if (*rest) say(rest, A_TEXT); else say("SAY WHAT?", A_TEXT);
        return 0;
    }
    if (word_is(s, ".READ", &rest) || (word_is(s, "READ", &rest) && strchr(rest, '.'))) {
        if (*rest) read_file(rest); else say("WHICH FILE SHALL I READ?", A_TEXT);
        return 0;
    }
    if (word_is(s, ".PHON", &rest) || word_is(s, "PHON", &rest)) {
        static char ph[1200];
        const char *q;
        tts_text_phonemes(*rest ? rest : "HELLO", ph, sizeof ph);
        for (q = ph; *q; q++) out_char(*q, A_BOX);
        newline();
        say(*rest ? rest : "HELLO", A_TEXT);
        return 0;
    }
    {
        static const char *const names[4] = { "PITCH", "SPEED", "TONE", "VOLUME" };
        int i;
        for (i = 0; i < 4; i++) {
            char dot[12];
            sprintf(dot, ".%s", names[i]);
            if (word_is(s, names[i], &rest) || word_is(s, dot, &rest)) {
                int *val = i == 0 ? &p : i == 1 ? &sp : i == 2 ? &t : &v;
                if (*rest == '=') rest++;
                while (*rest == ' ') rest++;
                if (isdigit((unsigned char)*rest) && !rest[1]) {
                    *val = *rest - '0';
                    tts_settings(p, sp, t, v);
                    sprintf(b, "%s IS NOW %d. HOW DO I SOUND?", names[i], *val);
                    say(b, A_TEXT);
                    return 0;
                }
                if (!*rest) { sprintf(b, "MY %s IS %d. GIVE ME A NUMBER FROM 0 TO 9 TO CHANGE IT.", names[i], *val); say(b, A_TEXT); return 0; }
                break;      /* "SPEED OF LIGHT..." - just conversation */
            }
        }
    }
    if ((word_is(s, "PARAM", &rest) || word_is(s, ".PARAM", &rest) || word_is(s, "SETTINGS", &rest)) && !*rest) { show_params(); return 0; }
    if ((word_is(s, "CLS", &rest) || word_is(s, "CLEAR", &rest)) && !*rest) { banner(); return 0; }
    if ((word_is(s, "NEW", &rest) || word_is(s, "NAME", &rest)) && !*rest) return 2;
    if (word_is(s, "RESET", &rest) && !*rest) { tts_settings(5, 5, 5, 5); say("MY VOICE IS BACK TO NORMAL.", A_TEXT); return 0; }
    return -1;
}

/* ---- the session ------------------------------------------------------------- */
static void get_name(void)
{
    say("HELLO, WHAT IS YOUR NAME?", A_TEXT);
    for (;;) {
        int n = read_line(patient, 25, 1);
        while (n && patient[n - 1] == ' ') patient[--n] = 0;
        while (patient[0] == ' ') memmove(patient, patient + 1, strlen(patient));
        if (patient[0]) break;
        say("EVERYBODY HAS A NAME. PLEASE TYPE YOURS.", A_TEXT);
    }
}

int main(int argc, char **argv)
{
    int i, want = SPK_AUTO;
    union REGS r;
    static char line[100];
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "/PC") || !strcmp(argv[i], "/pc")) want = SPK_PCSPK;
        else if (!strcmp(argv[i], "/Q") || !strcmp(argv[i], "/q")) { want = SPK_NONE; quiet = 1; }
        else if (!strcmp(argv[i], "/?")) {
            printf("Dr. ARMitso, the talking psychologist.\n\nDOCTOR [/PC] [/Q]\n\n"
                   "  /PC   Speak through the PC speaker\n  /Q    Quiet (no speech)\n");
            return 0;
        }
    }
    rnd_state = bios_ticks() | 1;
    r.x.ax = 0x0003; int86(0x10, &r, &r);
    vram = (volatile unsigned short *)0xB8000;
    speak_init(want);
    atexit(report_underruns);
    speak_break_install();
    tts_settings(5, 5, 5, 5);
new_patient:
    banner();
    say("DR. ARMITSO, BY EUROPA MICRO SYSTEMS.", A_TEXT);
    newline();
    get_name();
    newline();
    eliza_init(patient);
    /* the greeting: the name goes through the letter-to-sound rules only */
    {
        char b[160];
        sprintf(b, "HELLO %s, MY NAME IS DR. ARMITSO.", patient);
        tts_rules_words(patient);
        say(b, A_TEXT);
    }
    say("I AM A PSYCHOLOGIST, RUNNING ON AN ARM926 AT ONE HUNDRED MEGAHERTZ.", A_TEXT);
    say("YOU CAN TELL ME ANYTHING. IT STAYS BETWEEN YOU, ME AND THIS SOUND CARD,", A_TEXT);
    say("AND MY RAM WILL BE CLEARED WHEN YOU SWITCH ME OFF.", A_TEXT);
    say("NOW, WHAT SEEMS TO BE TROUBLING YOU?", A_TEXT);
    for (;;) {
        int c, act;
        const char *reply;
        newline();
        read_line(line, 76, 0);
        c = command(line);
        if (c == 1) break;
        if (c == 2) goto new_patient;
        if (c == 0) continue;
        reply = eliza_reply(line, &act);
        if (act == ELIZA_PARITY) crash_gag(reply);
        else say(reply, A_TEXT);
    }
    speak_shutdown();
    r.x.ax = 0x0003; int86(0x10, &r, &r);
    return 0;
}
