/*
 * zorkdos.c - the ARM-DOS front end for MojoZork: a Z-machine version 3
 * interpreter that looks like Infocom's own MS-DOS interpreter of 1988.
 *
 *   ZORK1 [storyfile]      (also ZORK2, ZORK3: the same program, built three
 *                           times with a different default story)
 *
 * Screen (when stdout is the console): cleared at start; row 0 is an
 * inverse-video status line (location left, "Score: n  Moves: n" right),
 * written straight into text memory at B800:0000; the story text scrolls in
 * rows 1-24 (the BIOS scroll-window call keeps the status line in place),
 * word-wrapped at the screen width, with a [MORE] prompt when a screenful
 * goes by without input. The text itself goes through DOS (stdout), so
 * "ZORK1 < MOVES.TXT > LOG.TXT" works; with stdout redirected there is no
 * status line and no [MORE].
 *
 * The story file is looked for next to the program (argv[0]'s directory),
 * then in the current directory. SAVE and RESTORE ask for a file name like
 * Infocom's interpreter ("Default is ZORK1.SAV").
 */
#define ARMDOS 1
#include "src/mojozork.c"

#include <dos.h>
#include <conio.h>
#include <io.h>
#include <ctype.h>
#include <armdos.h>

#ifdef ZDEBUG
#define DBG(s) armdos_debug(s)
#else
#define DBG(s) ((void) 0)
#endif

#ifndef STORYNAME
#define STORYNAME "ZORK1"
#endif

static int scr_out;             /* stdout is the screen */
static int scr_in;              /* stdin is the keyboard */
static int scr_cols = 80, scr_rows = 25;
static int col;                 /* output column */
static int lines_since_input;
static int wrapped;             /* the current line began with a word wrap */
static char word[128];
static int wlen;
static char savename[80];

/* ------------------------------------------------------------- BIOS ---- */
static void bios_scroll_up(void)       /* rows 1..last up by one, keep row 0 */
{
    union REGS r;
    r.x.ax = 0x0601;
    r.x.bx = 0x0700;                    /* BH = attribute of the new line */
    r.x.cx = 0x0100;                    /* CH=1 CL=0 */
    r.x.dx = ((scr_rows - 1) << 8) | (scr_cols - 1);
    int86(0x10, &r, &r);
}

static void bios_clear(void)
{
    union REGS r;
    r.x.ax = 0x0600;
    r.x.bx = 0x0700;
    r.x.cx = 0;
    r.x.dx = ((scr_rows - 1) << 8) | (scr_cols - 1);
    int86(0x10, &r, &r);
}

static void bios_gotoxy(int x, int y)
{
    union REGS r;
    r.x.ax = 0x0200;
    r.x.bx = 0;
    r.x.dx = (y << 8) | x;
    int86(0x10, &r, &r);
}

static int bios_row(void)
{
    union REGS r;
    r.x.ax = 0x0300;
    r.x.bx = 0;
    int86(0x10, &r, &r);
    return r.h.dh;
}

/* -------------------------------------------------------------- output -- */
static void raw(const char *s, int n)
{
    fwrite(s, 1, n, stdout);
}

static void newline(void);

static void more(void)
{
    if (!scr_out || !scr_in) return;
    if (++lines_since_input < scr_rows - 2) return;
    raw("[MORE]", 6);
    fflush(stdout);
    getch();
    raw("\r      \r", 8);
    lines_since_input = 0;
}

static void newline(void)
{
    col = 0;
    wrapped = 0;
    if (!scr_out) {
        raw("\n", 1);
        return;
    }
    fflush(stdout);
    if (bios_row() >= scr_rows - 1) {
        /* at the bottom: scroll only the story window and go back to
           column 0, so the status line stays put */
        bios_scroll_up();
        raw("\r", 1);
    } else {
        raw("\n", 1);
    }
    more();
}

static void flush_word(void)
{
    if (!wlen) return;
    if (col + wlen > scr_cols - 1 && col > 0) {
        newline();
        wrapped = 1;
    }
    raw(word, wlen);
    wrapped = 0;
    col += wlen;
    wlen = 0;
}

static void transcript(const char *str, uintptr n);

static void writestr_dos(const char *str, const uintptr slen)
{
    uintptr i;
#ifdef ZDEBUG
    { char b[300]; snprintf(b, sizeof b, "[%.*s]", (int) slen, str); DBG(b); }
#endif
    transcript(str, slen);
    for (i = 0; i < slen; i++) {
        const char ch = str[i];
        if (ch == '\n') {
            flush_word();
            newline();
        } else if (ch == ' ') {
            flush_word();
            if (col >= scr_cols - 1) {
                newline();
                wrapped = 1;
            } else if (col > 0 || !wrapped) {
                raw(" ", 1);
                col++;
            }
        } else {
            if (wlen == (int) sizeof (word) || wlen >= scr_cols - 1)
                flush_word();
            word[wlen++] = ch;
        }
    }
}

/* SCRIPT: Zork sets bit 0 of the header's flags 2; copy the text to a file
   (Infocom's interpreter sent it to the printer). */
static FILE *script_fp;
static void transcript(const char *str, uintptr n)
{
    if (!GState->story || !(GState->story[0x11] & 1)) {
        if (script_fp) { fclose(script_fp); script_fp = NULL; }
        return;
    }
    if (!script_fp) {
        script_fp = fopen(STORYNAME ".SCR", "a");
        if (!script_fp) { GState->story[0x11] &= ~1; return; }
    }
    fwrite(str, 1, n, script_fp);
}

static void out(const char *s)
{
    writestr_dos(s, strlen(s));
}

/* --------------------------------------------------------- status line -- */
static void armdos_status(void)
{
    volatile uint16_t *v = ARMDOS_TEXT_VRAM;
    char line[160], right[48], loc[64];
    const uint8 *addr;
    uint16 objid, a, b;
    const uint8 *objzstr;
    int i, n;

    if (!armdos_have_status || !GState->story) return;
    addr = varAddress(0x10, 0, 0);
    objid = READUI16(addr);
    a = READUI16(addr);
    b = READUI16(addr);

    loc[0] = '\0';
    objzstr = objid ? getObjectShortName(objid) : NULL;
    if (objzstr) {
        uintptr len = sizeof (loc) - 1;
        decode_zscii(objzstr, 0, loc, &len);
        loc[len < sizeof (loc) ? len : sizeof (loc) - 1] = '\0';
    }
    if (GState->header.version >= 3 && (GState->header.flags1 & 2))   /* time game */
        snprintf(right, sizeof right, "Time: %u:%02u %s ",
                 (unsigned) ((a + 11) % 12) + 1, (unsigned) b, a < 12 ? "am" : "pm");
    else
        snprintf(right, sizeof right, "Score: %d        Moves: %u ", (int) (sint16) a, (unsigned) b);

    memset(line, ' ', scr_cols);
    n = strlen(loc);
    if (n > scr_cols - (int) strlen(right) - 2) n = scr_cols - strlen(right) - 2;
    memcpy(line + 1, loc, n);
    memcpy(line + scr_cols - strlen(right), right, strlen(right));
    for (i = 0; i < scr_cols; i++)
        v[i] = 0x7000 | (uint8) line[i];
}

/* --------------------------------------------------------------- input -- */
static int armdos_readline(char *buf, int len)
{
    flush_word();
    fflush(stdout);
    lines_since_input = 0;
    if (!fgets(buf, len, stdin))
        return 0;
    if (!scr_in)            /* redirected input: show what was "typed" */
        raw(buf, strlen(buf));
    if (!strchr(buf, '\n')) {       /* too long: drop the rest of the line */
        int c;
        while ((c = getchar()) != EOF && c != '\n')
            ;
    }
    col = 0;
    armdos_status();        /* DOS's echo of Enter may have scrolled the screen */
    return 1;
}

static const char *armdos_savename(int saving)
{
    char buf[80], *p, *e;
    out("Enter a file name.\n");
    out("(Default is \"");
    out(savename);
    out("\"): ");
    if (!armdos_readline(buf, sizeof buf))
        return NULL;
    for (p = buf; *p == ' ' || *p == '\t'; p++)
        ;
    for (e = p + strlen(p); e > p && isspace((unsigned char) e[-1]); e--)
        ;
    *e = '\0';
    if (*p) {
        /* no extension given: add .SAV, as Infocom's interpreter did */
        const char *base = strrchr(p, '\\');
        base = base ? base + 1 : p;
        snprintf(savename, sizeof savename, "%s%s", p, strchr(base, '.') ? "" : ".SAV");
        for (p = savename; *p; p++) *p = toupper((unsigned char) *p);
    }
    if (!saving && access(savename, 0) != 0) {
        out("File not found.\n");
        return NULL;
    }
    return savename;
}

/* ---------------------------------------------------------------- misc -- */
static void die_dos(const char *fmt, ...) __attribute__((noreturn));
static void die_dos(const char *fmt, ...)
{
    va_list ap;
    flush_word();
    fflush(stdout);
    fprintf(stderr, "\r\n*** Internal error: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, " (pc=%X)\n", (unsigned int) GState->logical_pc);
#ifdef ZDEBUG
    {
        char b[200];
        va_start(ap, fmt);
        vsnprintf(b, sizeof b, fmt, ap);
        va_end(ap);
        DBG("zork: die: "); DBG(b); DBG("\n");
    }
#endif
    exit(1);
}

/* the story: next to the program, else in the current directory */
static const char *find_story(const char *argv0, char *path, size_t n)
{
    const char *slash = argv0 ? strrchr(argv0, '\\') : NULL;
    if (slash) {
        snprintf(path, n, "%.*s%s.DAT", (int) (slash - argv0 + 1), argv0, STORYNAME);
        if (access(path, 0) == 0)
            return path;
    }
    snprintf(path, n, "%s.DAT", STORYNAME);
    return path;
}

int main(int argc, char **argv)
{
    static ZMachineState zmachine_state;
    static char path[128];
    const char *fname;
    FILE *io;

    GState = &zmachine_state;
    GState->die = die_dos;
    GState->writestr = writestr_dos;
    random_seed = (int) time(NULL);

    DBG("zork: start\n");
    fname = (argc >= 2) ? argv[1] : find_story(argc ? argv[0] : NULL, path, sizeof path);
    if ((io = fopen(fname, "rb")) == NULL) {
        fprintf(stderr, "Can't find the story file %s\n", fname);
        return 1;
    }
    fclose(io);
    DBG("zork: story found\n");
    snprintf(savename, sizeof savename, "%s.SAV", STORYNAME);

    scr_out = isatty(1);
    scr_in = isatty(0);
    if (scr_out) {
        uint8_t c = ARMDOS_BDA[0x4A], r = ARMDOS_BDA[0x84];
        if (c >= 40) scr_cols = c;
        if (r >= 20 && r < 60) scr_rows = r + 1;
        armdos_have_status = 1;
        bios_clear();
        bios_gotoxy(0, 1);
    }
    if (scr_cols > (int) sizeof word) scr_cols = sizeof word;

    DBG("zork: screen set up\n");
    loadStory(fname);
    DBG("zork: story loaded\n");
    armdos_status();
    DBG("zork: running\n");

    while (!GState->quit)
        runInstruction();
    DBG("zork: quit\n");

    flush_word();
    if (col) newline();
    fflush(stdout);
    if (script_fp) fclose(script_fp);
    return 0;
}
