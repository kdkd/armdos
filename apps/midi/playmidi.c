/* PLAYMIDI.EXE - plays Standard MIDI Files on the MPU-401 (General MIDI).
 *
 *   PLAYMIDI [/Q] [/L] [drive:][path]filename[.MID] [...]
 *
 * A full-screen player in the style of the early-90s MIDI players: song
 * title, tempo, bar/beat, time and a position bar, and one row per MIDI
 * channel with its instrument, volume, pan, the last note and an activity
 * meter. Esc stops, Space pauses, N skips to the next file. /Q plays without
 * the screen (one line per file through DOS, so it can be redirected),
 * /L loops the list until Esc.
 *
 * ARM-DOS project; uses the SDK's midi.h (MPU-401 UART mode, SMF reader,
 * the 1 kHz MIDI clock).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dos.h>
#include <conio.h>
#include <armdos.h>
#include <midi.h>

static const char *const gm_names[128] = {
    "Acoustic Grand Piano", "Bright Acoustic Piano", "Electric Grand Piano", "Honky-tonk Piano",
    "Electric Piano 1", "Electric Piano 2", "Harpsichord", "Clavinet",
    "Celesta", "Glockenspiel", "Music Box", "Vibraphone", "Marimba", "Xylophone", "Tubular Bells", "Dulcimer",
    "Drawbar Organ", "Percussive Organ", "Rock Organ", "Church Organ", "Reed Organ", "Accordion", "Harmonica", "Tango Accordion",
    "Nylon String Guitar", "Steel String Guitar", "Jazz Guitar", "Clean Electric Guitar",
    "Muted Electric Guitar", "Overdriven Guitar", "Distortion Guitar", "Guitar Harmonics",
    "Acoustic Bass", "Fingered Bass", "Picked Bass", "Fretless Bass", "Slap Bass 1", "Slap Bass 2", "Synth Bass 1", "Synth Bass 2",
    "Violin", "Viola", "Cello", "Contrabass", "Tremolo Strings", "Pizzicato Strings", "Orchestral Harp", "Timpani",
    "String Ensemble 1", "String Ensemble 2", "Synth Strings 1", "Synth Strings 2", "Choir Aahs", "Voice Oohs", "Synth Voice", "Orchestra Hit",
    "Trumpet", "Trombone", "Tuba", "Muted Trumpet", "French Horn", "Brass Section", "Synth Brass 1", "Synth Brass 2",
    "Soprano Sax", "Alto Sax", "Tenor Sax", "Baritone Sax", "Oboe", "English Horn", "Bassoon", "Clarinet",
    "Piccolo", "Flute", "Recorder", "Pan Flute", "Blown Bottle", "Shakuhachi", "Whistle", "Ocarina",
    "Square Lead", "Sawtooth Lead", "Calliope Lead", "Chiff Lead", "Charang Lead", "Voice Lead", "Fifths Lead", "Bass + Lead",
    "New Age Pad", "Warm Pad", "Polysynth Pad", "Choir Pad", "Bowed Pad", "Metallic Pad", "Halo Pad", "Sweep Pad",
    "Rain", "Soundtrack", "Crystal", "Atmosphere", "Brightness", "Goblins", "Echoes", "Sci-Fi",
    "Sitar", "Banjo", "Shamisen", "Koto", "Kalimba", "Bagpipe", "Fiddle", "Shanai",
    "Tinkle Bell", "Agogo", "Steel Drums", "Woodblock", "Taiko Drum", "Melodic Tom", "Synth Drum", "Reverse Cymbal",
    "Guitar Fret Noise", "Breath Noise", "Seashore", "Bird Tweet", "Telephone Ring", "Helicopter", "Applause", "Gunshot",
};

static const char *kit_name(unsigned p)
{
    switch (p) {
    case 8: return "Room Kit";      case 16: return "Power Kit";   case 24: return "Electronic Kit";
    case 25: return "TR-808 Kit";   case 32: return "Jazz Kit";    case 40: return "Brush Kit";
    case 48: return "Orchestra Kit"; case 56: return "SFX Kit";    case 127: return "CM-64/32L Kit";
    default: return "Standard Kit";
    }
}

/* ------------------------------------------------------------ screen */
#define COLS 80
#define ROWS 25
#define A_FRAME 0x1B        /* bright cyan on blue */
#define A_LABEL 0x1E        /* yellow on blue */
#define A_TEXT  0x1F        /* white on blue */
#define A_DIM   0x17        /* grey on blue */
#define A_BAR   0x1A        /* bright green */
#define A_BARY  0x1E
#define A_BARR  0x1C
#define A_HEAD  0x70        /* black on grey */
#define A_KEY   0x30        /* black on cyan */

static volatile uint16_t *vram;
static uint16_t saved_screen[COLS * ROWS];
static unsigned saved_cursor, saved_shape;
static int screen_on;

static void put(int x, int y, const char *s, int attr)
{
    volatile uint16_t *p = vram + y * COLS + x;
    while (*s && x++ < COLS) *p++ = (uint16_t)((attr << 8) | (unsigned char)*s++);
}
static void putn(int x, int y, const char *s, int n, int attr)       /* padded / cut to n */
{
    volatile uint16_t *p = vram + y * COLS + x;
    while (n-- > 0 && x++ < COLS) *p++ = (uint16_t)((attr << 8) | (unsigned char)(*s ? *s++ : ' '));
}
static void putc_(int x, int y, int ch, int attr) { vram[y * COLS + x] = (uint16_t)((attr << 8) | (ch & 0xFF)); }
static void fill(int x, int y, int n, int ch, int attr) { while (n-- > 0) putc_(x++, y, ch, attr); }

static void bios_cursor(unsigned pos) { struct armregs r = { 0 }; r.r0 = 0x0200; r.r1 = 0; r.r3 = pos; _armdos_int10(&r); }

static void screen_begin(void)
{
    struct armregs r = { 0 };
    int i;
    r.r0 = 0x0F00; _armdos_int10(&r);
    if ((r.r0 & 0xFF) != 3 && (r.r0 & 0xFF) != 2 && (r.r0 & 0xFF) != 7) { memset(&r, 0, sizeof r); r.r0 = 0x0003; _armdos_int10(&r); }
    vram = ARMDOS_TEXT_VRAM;
    for (i = 0; i < COLS * ROWS; i++) saved_screen[i] = vram[i];
    memset(&r, 0, sizeof r); r.r0 = 0x0300; _armdos_int10(&r);
    saved_cursor = r.r3 & 0xFFFF; saved_shape = r.r2 & 0xFFFF;
    memset(&r, 0, sizeof r); r.r0 = 0x0100; r.r2 = 0x2000; _armdos_int10(&r);      /* cursor off */
    screen_on = 1;
}
static void screen_end(void)
{
    struct armregs r = { 0 };
    int i;
    if (!screen_on) return;
    for (i = 0; i < COLS * ROWS; i++) vram[i] = saved_screen[i];
    r.r0 = 0x0100; r.r2 = saved_shape; _armdos_int10(&r);
    bios_cursor(saved_cursor);
    screen_on = 0;
}

/* ------------------------------------------------------------ channel state */
typedef struct {
    unsigned program, bank, vol, pan, expr;
    int note;              /* last note, -1 none */
    int held;              /* notes sounding */
    unsigned level;        /* meter 0-1000 */
    int drum;
    int used;
} chan_t;
static chan_t ch[16];

static void chan_reset(void)
{
    int i;
    memset(ch, 0, sizeof ch);
    for (i = 0; i < 16; i++) { ch[i].vol = 100; ch[i].pan = 64; ch[i].expr = 127; ch[i].note = -1; ch[i].drum = i == 9; }
}

static void track_event(const smf_event *e)
{
    chan_t *c;
    if (e->status >= 0xF0) {
        /* GS "use for rhythm part": F0 41 dev 42 12 40 1x 15 vv */
        if (e->status == 0xF0 && e->len >= 8 && e->data[0] == 0x41 && e->data[2] == 0x42 && e->data[3] == 0x12
            && e->data[4] == 0x40 && (e->data[5] & 0xF0) == 0x10 && e->data[6] == 0x15) {
            unsigned p = e->data[5] & 15, n = p == 0 ? 9 : p <= 9 ? p - 1 : p;
            ch[n].drum = e->data[7] != 0;
        }
        return;
    }
    c = &ch[e->status & 15];
    switch (e->status & 0xF0) {
    case 0x90:
        if (e->d2) {
            unsigned lv = (unsigned)e->d2 * c->vol / 127 * c->expr / 127 * 1000 / 127;
            if (lv > c->level) c->level = lv;
            c->note = e->d1; c->held++; c->used = 1;
            break;
        }
        __attribute__((fallthrough));     /* velocity 0 = note off */
    case 0x80: if (c->held > 0) c->held--; break;
    case 0xB0:
        if (e->d1 == 7) c->vol = e->d2;
        else if (e->d1 == 10) c->pan = e->d2;
        else if (e->d1 == 11) c->expr = e->d2;
        else if (e->d1 == 0) c->bank = e->d2;
        else if (e->d1 == 123 || e->d1 == 120) c->held = 0;
        else if (e->d1 == 121) { c->expr = 127; }
        c->used = 1;
        break;
    case 0xC0: c->program = e->d1; c->used = 1; break;
    }
}

static const char *note_name(int n, char *buf)
{
    static const char *const nm[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    if (n < 0) return "";
    sprintf(buf, "%s%d", nm[n % 12], n / 12 - 1);
    return buf;
}

/* ------------------------------------------------------------ drawing */
static const char *cur_path;
static smf_file song;
static int file_index, file_total;
static int paused;

static void frame(void)
{
    int y;
    char buf[81];
    for (y = 0; y < ROWS; y++) fill(0, y, COLS, ' ', A_TEXT);
    putc_(0, 0, 0xC9, A_FRAME); fill(1, 0, 78, 0xCD, A_FRAME); putc_(79, 0, 0xBB, A_FRAME);
    put(29, 0, " ARM-PC MIDI Player 1.00 ", A_LABEL);
    for (y = 1; y < 24; y++) { putc_(0, y, 0xBA, A_FRAME); putc_(79, y, 0xBA, A_FRAME); }
    putc_(0, 6, 0xC7, A_FRAME); fill(1, 6, 78, 0xC4, A_FRAME); putc_(79, 6, 0xB6, A_FRAME);
    putc_(0, 24, 0xC8, A_FRAME); fill(1, 24, 78, 0xCD, A_FRAME); putc_(79, 24, 0xBC, A_FRAME);
    put(2, 1, "File:", A_LABEL); put(2, 2, "Title:", A_LABEL); put(2, 3, "Device:", A_LABEL); put(2, 4, "Tempo:", A_LABEL);
    put(2, 7, "Ch Instrument             Vol Pan Note Activity", A_LABEL);
    for (y = 0; y < 16; y++) { sprintf(buf, "%2d", y + 1); put(2, 8 + y, buf, A_DIM); }
    put(3, 24, " Esc ", A_KEY); put(8, 24, " Stop ", A_FRAME);
    put(15, 24, " Space ", A_KEY); put(22, 24, " Pause ", A_FRAME);
    put(30, 24, " N ", A_KEY); put(33, 24, " Next ", A_FRAME);
    put(56, 24, " Europa Micro Systems ", A_FRAME);
}

static void header(void)
{
    char buf[100];
    const char *t = song.title[0] ? song.title : "(untitled)";
    putn(10, 1, cur_path, 36, A_TEXT);
    sprintf(buf, "Format %u, %u track%s, %u PPQN", song.format, song.ntracks, song.ntracks == 1 ? "" : "s",
            (song.division & 0x8000) ? 0 : song.division);
    putn(47, 1, buf, 31, A_DIM);
    putn(10, 2, t, 50, A_TEXT);
    sprintf(buf, "%d of %d", file_index + 1, file_total);
    putn(66, 2, buf, 12, A_DIM);
    sprintf(buf, "MPU-401 at %Xh, UART mode, IRQ %u", mpu_info.port, mpu_info.irq);
    putn(10, 3, buf, 40, A_TEXT);
    putn(52, 3, "General MIDI (GS)", 26, A_DIM);
}

static void fmt_time(char *b, unsigned long us)
{
    unsigned long s = us / 1000000ul;
    sprintf(b, "%02lu:%02lu", s / 60, s % 60);
}

static void status_line(unsigned long now, unsigned long tick)
{
    char buf[100], t1[16], t2[16];
    unsigned bpm = song.tempo ? (unsigned)((60000000ul + song.tempo / 2) / song.tempo) : 120;
    unsigned long beat = (song.division & 0x8000) || !song.division ? 0 : tick / song.division;
    unsigned num = song.timesig_num ? song.timesig_num : 4;
    int w, i, filled;
    fmt_time(t1, now); fmt_time(t2, song.length_us);
    sprintf(buf, "%3u BPM   %u/%u   Bar %4lu:%u   Time %s / %s  %s", bpm, song.timesig_num, song.timesig_den,
            beat / num + 1, (unsigned)(beat % num) + 1, t1, t2, paused ? "PAUSED" : "      ");
    putn(10, 4, buf, 68, A_TEXT);
    /* position bar */
    w = 76;
    filled = song.length_us ? (int)((unsigned long long)now * (w * 2) / song.length_us) : 0;
    if (filled > w * 2) filled = w * 2;
    for (i = 0; i < w; i++) {
        int c = filled >= (i + 1) * 2 ? 0xDB : filled == i * 2 + 1 ? 0xDD : 0xB0;
        putc_(2 + i, 5, c, c == 0xB0 ? A_DIM : A_TEXT);
    }
}

static void channels(void)
{
    int i, j;
    char buf[40], nb[16];
    for (i = 0; i < 16; i++) {
        chan_t *c = &ch[i];
        int y = 8 + i, a = c->used ? A_TEXT : A_DIM, bars;
        putn(5, y, c->drum ? kit_name(c->program) : gm_names[c->program & 127], 22, a);
        sprintf(buf, "%3u", c->vol); putn(27, y, buf, 4, a);
        if (c->pan == 64) strcpy(buf, " C ");
        else sprintf(buf, "%c%2d", c->pan < 64 ? 'L' : 'R', c->pan < 64 ? 64 - c->pan : c->pan - 64);
        putn(31, y, buf, 4, a);
        putn(35, y, note_name(c->note, nb), 5, a);
        /* activity meter: 38 cells, green, yellow at 70%, red at 90% */
        bars = (int)(c->level * 76 / 1000);
        for (j = 0; j < 38; j++) {
            int ch_ = bars >= (j + 1) * 2 ? 0xDB : bars == j * 2 + 1 ? 0xDD : 0xFA;
            int at = ch_ == 0xFA ? A_DIM : j >= 34 ? A_BARR : j >= 27 ? A_BARY : A_BAR;
            putc_(40 + j, y, ch_, at);
        }
    }
}

static void meters_decay(unsigned ms)
{
    int i;
    for (i = 0; i < 16; i++) {
        unsigned floor_ = ch[i].held > 0 ? ch[i].level / 2 : 0;
        unsigned d = ms * 2;             /* 2 per ms of 1000: a full bar falls in 0.5 s */
        if (ch[i].level > d) ch[i].level -= d; else ch[i].level = 0;
        if (ch[i].held > 0 && ch[i].level < floor_) ch[i].level = floor_;
    }
}

/* ------------------------------------------------------------ playing */
enum { END_DONE, END_NEXT, END_QUIT };

static void pause_notes(void)
{
    unsigned c;
    for (c = 0; c < 16; c++) { mpu_msg(0xB0 | c, 64, 0); mpu_msg(0xB0 | c, 123, 0); ch[c].held = 0; }
}

static int keys(void)
{
    while (kbhit()) {
        int k = getch();
        if (k == 0 || k == 0xE0) { getch(); continue; }
        if (k == 27) return END_QUIT;
        if (k == 'n' || k == 'N' || k == '\r') return END_NEXT;
        if (k == ' ') return -2;
    }
    return -1;
}

static int play(int quiet)
{
    static const unsigned char gm_on[] = { 0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7 };
    smf_event ev;
    unsigned long t0, now = 0, last_draw = 0, pause_at = 0;
    int r = END_DONE;
    chan_reset();
    paused = 0;
    if (!quiet) { frame(); header(); status_line(0, 0); channels(); }
    mpu_send(gm_on, sizeof gm_on);
    smf_rewind(&song);
    t0 = midi_timer_us() + 150000;
    while (smf_next(&song, &ev)) {
        for (;;) {
            unsigned long t = midi_timer_us();
            int k;
            if (paused) now = pause_at;
            else {
                if ((long)(t - t0 - ev.time_us) >= 0) break;     /* (t0 may lie ahead of t) */
                now = (long)(t - t0) > 0 ? t - t0 : 0;
            }
            if (!quiet && t - last_draw >= 40000) {
                meters_decay((t - last_draw) / 1000);
                last_draw = t;
                status_line(now, song.cur_tick);
                channels();
            }
            k = keys();
            if (k == -2) {
                if (!paused) { paused = 1; pause_at = now; pause_notes(); }
                else { paused = 0; t0 = t - pause_at; }
                if (!quiet) status_line(now, song.cur_tick);
            } else if (k >= 0) { r = k; goto out; }
            armdos_halt();
        }
        track_event(&ev);
        mpu_send_event(&ev);
    }
out:
    mpu_all_notes_off();
    if (!quiet && r == END_DONE) {       /* let the last notes ring out while the bars fall */
        unsigned long te = midi_timer_us() + 1500000;
        while (midi_timer_us() < te) {
            unsigned long t = midi_timer_us();
            int k = keys();
            if (k == END_QUIT) return END_QUIT;
            if (k >= 0) break;
            if (t - last_draw >= 40000) { meters_decay((t - last_draw) / 1000); last_draw = t; status_line(song.length_us, song.length_ticks); channels(); }
            armdos_halt();
        }
    }
    return r;
}

/* ------------------------------------------------------------ files */
#define MAX_FILES 128
static char *files[MAX_FILES];
static int nfiles;

static void add_file(const char *s)
{
    if (nfiles < MAX_FILES) { files[nfiles] = malloc(strlen(s) + 1); if (files[nfiles]) strcpy(files[nfiles++], s); }
}

static void add_pattern(const char *arg)
{
    char path[128], dir[128];
    struct find_t ft;
    const char *base;
    int found = 0;
    strncpy(path, arg, sizeof path - 5); path[sizeof path - 5] = 0;
    base = strrchr(path, '\\');
    if (!base) base = strrchr(path, ':');
    base = base ? base + 1 : path;
    if (!strchr(base, '.')) strcat(path, ".MID");
    memcpy(dir, path, base - path); dir[base - path] = 0;
    if (!strpbrk(base, "*?")) { add_file(path); return; }
    if (_dos_findfirst(path, 0, &ft) == 0) {
        do {
            char full[140];
            sprintf(full, "%s%s", dir, ft.name);
            add_file(full); found = 1;
        } while (_dos_findnext(&ft) == 0);
    }
    if (!found) add_file(path);     /* reported as not found when played */
}

static void usage(void)
{
    printf("Plays Standard MIDI Files on the MPU-401.\n\n"
           "PLAYMIDI [/Q] [/L] [drive:][path]filename[.MID] [...]\n\n"
           "  /Q   Quiet: no player screen, one line per file\n"
           "  /L   Loop the list until Esc is pressed\n\n"
           "Wildcards are allowed.  Keys: Esc stops, Space pauses, N plays the next file.\n");
}

int main(int argc, char **argv)
{
    int i, quiet = 0, loop = 0, r = END_DONE, played = 0, errors = 0;
    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '/' || argv[i][0] == '-') {
            switch (toupper((unsigned char)argv[i][1])) {
            case 'Q': quiet = 1; break;
            case 'L': loop = 1; break;
            case '?': usage(); return 0;
            default: printf("Invalid switch - %s\n", argv[i]); return 1;
            }
        } else add_pattern(argv[i]);
    }
    if (!nfiles) { usage(); return 1; }
    if (!mpu_detect(NULL)) { printf("MPU-401 not found at %Xh.\n", mpu_info.port); return 2; }
    if (!mpu_uart()) { printf("MPU-401 at %Xh does not enter UART mode.\n", mpu_info.port); return 2; }
    midi_timer_start();
    file_total = nfiles;
    do {
        for (i = 0; i < nfiles && r != END_QUIT; i++) {
            int e;
            char t[16];
            file_index = i; cur_path = files[i];
            e = smf_load(&song, files[i]);
            if (e) {
                screen_end();
                printf("%s: %s\n", files[i], e == -1 ? "File not found" : e == -3 ? "Not enough memory" : "Not a MIDI file");
                errors++;
                continue;
            }
            if (quiet) {
                fmt_time(t, song.length_us);
                printf("Playing %s (%s)\n", files[i], t);
                if (song.title[0]) printf("  %s\n", song.title);
            } else if (!screen_on) screen_begin();
            r = play(quiet);
            smf_free(&song);
            played++;
        }
    } while (loop && r != END_QUIT && played);
    screen_end();
    midi_timer_stop();
    mpu_shutdown();
    if (quiet) printf(r == END_QUIT ? "Stopped.\n" : "Done.\n");
    return errors && !played ? 1 : 0;
}
