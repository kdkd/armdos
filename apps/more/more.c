/*
 * MORE - ARM-DOS re-creation of the MS-DOS 4.00 MORE filter.
 *
 *   MORE < file          command | MORE          MORE file
 *
 * A port of CMD/MORE/MORE.ASM of the MS-DOS 4.0 source (MIT licence,
 * (C) Microsoft Corp.): the input handle is duplicated and standard input
 * re-opened from standard error, so the key press comes from the console;
 * a CR LF first; then every character goes through INT 21h AH=02h while
 * the row/column is tracked (CR, LF, BS, TAB, BEL, wrap at the screen width
 * from INT 10h AH=0Fh, rows from ANSI.SYS's IOCTL if it answers, else 25);
 * on the last row "-- More --" goes to standard error, a key is read
 * (AH=0Ch/08h, extended keys swallowed) and CR LF CR LF follows.  ^Z or the
 * end of the input ends it.
 *
 * One addition to 4.00, whose MORE ignored its command line: "MORE file"
 * pages the file (as MORE did from later versions on), since that is how
 * most people remember it. The first argument not starting with a switch
 * character is the file; the rest are ignored as before.
 */
#include "u4.h"

static uint8_t buffer[4096];

int main(void)
{
    struct armregs r;
    uint8_t maxrow = 25, maxcol, currow = 1, curcol = 1;

    if (!u4_version_ok()) { u4_puts(STDERR, "Incorrect DOS version\r\n"); return 0; }

    /* ANSI.SYS: IOCTL get display information on the console */
    {
        uint8_t ansi[18];
        memset(ansi, 0, sizeof ansi);
        ansi[2] = 14;
        u4_clr(&r);
        r.r0 = 0x440C; r.r1 = STDERR; r.r2 = 0x037F; r.r3 = (uint32_t)ansi;
        if (!u4_int21(&r) && ansi[6] == 1) maxrow = ansi[16];
    }
    u4_clr(&r);
    r.r0 = 0x0F00;
    u4_int10(&r);
    maxcol = (r.r0 >> 8) & 0xFF;

    /* a file named on the command line (not in 4.00), else standard input on a new handle */
    char *a = u4_cmdline(), *name = 0;
    for (;;) {
        while (*a == ' ' || *a == '\t' || *a == ',' || *a == ';' || *a == '=') a++;
        if (*a == '\r' || !*a) break;
        char *w = a;
        while (*a && *a != '\r' && *a != ' ' && *a != '\t' && *a != ',' && *a != ';' && *a != '=') a++;
        if (*w != '/') { *a = 0; name = w; break; }
    }
    int in;
    if (name) {
        in = u4_open(name, 0);
        if (in < 0) { u4_exterr(STDERR, u4_err, name); return 1; }
    } else {
        u4_clr(&r);
        r.r0 = 0x4500; r.r1 = STDIN;
        u4_int21(&r);
        in = r.r0 & 0xFFFF;
    }
    /* standard input from standard error: the key press comes from the console */
    u4_close(STDIN);
    u4_clr(&r);
    r.r0 = 0x4500; r.r1 = STDERR;
    u4_int21(&r);

    u4_write(STDOUT, "\r\n", 2);
    for (;;) {
        int n = u4_read(in, buffer, sizeof buffer);
        if (n <= 0) return 0;
        for (int i = 0; i < n; i++) {
            uint8_t c = buffer[i];
            if (c == 0x1A) return 0;
            if (c == 13) curcol = 1;
            else if (c == 10) currow++;
            else if (c == 8) { if (curcol != 1) curcol--; }
            else if (c == 9) curcol = ((curcol + 7) & 0xF8) + 1;
            else if (c != 7) {
                curcol++;
                if (curcol > maxcol) { currow++; curcol = 1; }
            }
            u4_clr(&r);
            r.r0 = 0x0200; r.r3 = c;
            u4_int21(&r);
            if (currow >= maxrow) {
                u4_puts(STDERR, "-- More --");
                u4_clr(&r);
                r.r0 = 0x0C08;
                u4_int21(&r);
                if ((r.r0 & 0xFF) == 0) {
                    u4_clr(&r);
                    r.r0 = 0x0800;
                    u4_int21(&r);
                }
                u4_write(STDERR, "\r\n\r\n", 4);
                curcol = 1;
                currow = 1;
            }
        }
    }
}
