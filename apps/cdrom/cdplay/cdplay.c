/*
 * CDPLAY.EXE - the ARM-DOS CD Player: plays the audio tracks of a CD in the
 * CD-ROM drive through the CD-ROM extensions (ARMCDEX.EXE, INT 2Fh AX=1510h
 * device requests to ARMCD.SYS).
 *
 *   CDPLAY          the full-screen player
 *   CDPLAY /R       install the pop-up player: Ctrl+Alt+C over any text-mode
 *                   program (refused, with a beep, in graphics modes)
 *   CDPLAY /U       remove the pop-up player from memory
 *   CDPLAY /?       help
 *
 * Keys (both modes): 1-9 play that track, Enter play the selected track,
 * Space/P play or pause, S stop, Left/Right previous/next track, E eject or
 * load, H shuffle, R repeat, Esc leave (the music keeps playing - a CD drive
 * plays on its own). The full-screen player also takes the mouse (INT 33h).
 *
 * Track titles come from CDPLAY.INI (next to CDPLAY.EXE), a disc database in
 * the style of the Windows 3.1 CD player's: a section per disc named by its
 * disc ID (8 hex digits, see disc_id()), with title=, artist=, numtracks= and
 * 0=, 1=, ... (the titles of tracks 1, 2, ..., 0-based).
 *
 * The pop-up is resident the way apps/popup is: INT 15h AH=4Fh sees the hot
 * key, INT 09h/08h/28h pop up when DOS is idle (InDOS = 0, no critical error,
 * no BIOS video/disk call in progress, or from INT 28h), on CDPLAY's own
 * stack. With shuffle or repeat on, the same safe moments (every 2 s) let the
 * resident player start the next track when one ends.
 *
 * Freestanding (no C library): the resident part is the whole program, its
 * variables and a 3 KB stack.
 *
 * Copyright (C) 1993 Europa Micro Systems (ARM-DOS project).
 */
#include <stdint.h>
#include <stddef.h>
#include "armdos.h"
#include "../inc/cdrom.h"

#define MPLEX_ID 0xC7
#define SIG      0x4C504443u            /* "CDPL" */
#define MAXT     32                     /* tracks with titles */
#define TLEN     32

/* ------------------------------------------------------------- helpers */

void *memcpy(void *d, const void *s, size_t n) { uint8_t *p = d; const uint8_t *q = s; while (n--) *p++ = *q++; return d; }
void *memset(void *d, int c, size_t n) { uint8_t *p = d; while (n--) *p++ = c; return d; }
static int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }
static void clr(struct armregs *r) { memset(r, 0, sizeof *r); }
static int dos(struct armregs *r) { return _armdos_int21(r); }
static char *scat(char *b, const char *s) { while (*s) *b++ = *s++; *b = 0; return b; }
static char *udec(char *b, uint32_t v)
{
    char t[12]; int n = 0;
    do { t[n++] = '0' + v % 10; v /= 10; } while (v);
    while (n) *b++ = t[--n];
    *b = 0;
    return b;
}
static char *dec2(char *b, unsigned v) { *b++ = '0' + v / 10 % 10; *b++ = '0' + v % 10; *b = 0; return b; }
static char *mmss(char *b, uint32_t secs) { b = udec(b, secs / 60); *b++ = ':'; return dec2(b, secs % 60); }
static char *hex8(char *b, uint32_t v) { for (int i = 7; i >= 0; i--) *b++ = "0123456789ABCDEF"[(v >> (i * 4)) & 15]; *b = 0; return b; }
static uint8_t up(uint8_t c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }

/* ------------------------------------------------------------- the drive */

static int cdl;                         /* the CD drive (0 = A:) */
uint32_t psp_seg;                       /* ours (the pop-up borrows it for file I/O) */
static int resident;                    /* 1 in the pop-up (resident) copy */
static int dos_ok = 1;                  /* 0: popped up inside a DOS console read - no DOS calls */
static int titles_pending;              /* the titles still have to be read (when dos_ok) */

static uint16_t request(void *rq)
{
    struct cdreq *h = rq;
    struct armregs r; clr(&r);
    r.r0 = 0x1510; r.r2 = cdl; r.r1 = (uint32_t)rq;
    h->status = 0;
    _armdos_int2f(&r);
    return h->status;
}

static uint16_t ioctl(int out, uint8_t *cb, unsigned n)
{
    struct cdreq_ioctl q;
    memset(&q, 0, sizeof q);
    q.h.len = sizeof q; q.h.cmd = out ? C_IOCTL_OUT : C_IOCTL_IN;
    q.buf = (uint32_t)cb; q.count = n;
    return request(&q);
}

/* the disc */
static struct {
    uint8_t  ok, first, last;
    uint32_t leadout;
    uint32_t start[100];
    uint8_t  data[100];                 /* 1 = a data track */
} toc;
static uint32_t disc_id_now;
static char disc_title[TLEN + 1], disc_artist[TLEN + 1];
static char titles[MAXT][TLEN + 1];     /* by track number - 1 */

/* the drive's state, from poll() */
enum { ST_NODISC, ST_OPEN, ST_STOP, ST_PLAY, ST_PAUSE };
static struct {
    int state;
    int track, index;
    uint32_t rel, abs;                  /* seconds in the track / on the disc */
    uint32_t pos;                       /* LBA */
} st;

/* the player */
static int shuffle, repeat;
static int prog;                        /* a play of ours is in progress (for shuffle/repeat) */
static uint8_t order[99];
static int norder, opos;
static uint32_t rnd = 0x1993;
static char ini_path[80];
static uint32_t status_until;
static char status_msg[60];

static uint32_t bcd2bin(uint8_t b) { return (b >> 4) * 10 + (b & 15); }

/* a disc ID from the TOC: ntracks << 24, plus every track's start and the
   lead-out as Red Book frames (LBA + 150). tools/build-disc.mjs computes the
   same for CDPLAY.INI. */
static uint32_t disc_id(void)
{
    uint32_t id = (uint32_t)(toc.last - toc.first + 1) << 24;
    for (int t = toc.first; t <= toc.last; t++) id += toc.start[t] + 150;
    return id + toc.leadout + 150;
}

/* ---- CDPLAY.INI */

static struct armregs ir;
static int ini_fh, ini_n, ini_i;
static char ini_buf[128];

static int ini_getc(void)
{
    if (ini_i >= ini_n) {
        clr(&ir); ir.r0 = 0x3F00; ir.r1 = ini_fh; ir.r2 = sizeof ini_buf; ir.r3 = (uint32_t)ini_buf;
        if (dos(&ir)) return -1;
        ini_n = ir.r0 & 0xFFFF; ini_i = 0;
        if (!ini_n) return -1;
    }
    return (uint8_t)ini_buf[ini_i++];
}
static int ini_line(char *l, int max)
{
    int c, n = 0;
    while ((c = ini_getc()) >= 0 && c != '\n') if (c != '\r' && c != 0x1A && n < max - 1) l[n++] = c;
    l[n] = 0;
    return c < 0 && !n ? -1 : n;
}
static void copy_val(char *d, const char *s)
{
    int n = 0;
    while (*s == ' ') s++;
    while (*s && n < TLEN) d[n++] = *s++;
    while (n && d[n - 1] == ' ') n--;
    d[n] = 0;
}

static void load_titles(void)
{
    memset(titles, 0, sizeof titles);
    disc_title[0] = disc_artist[0] = 0;
    titles_pending = !dos_ok;
    if (!ini_path[0] || !dos_ok) return;
    /* file handles belong to the current PSP: in the pop-up, borrow ours */
    struct armregs r; clr(&r);
    uint32_t other = 0;
    if (resident) { r.r0 = 0x5100; dos(&r); other = r.r1 & 0xFFFF; clr(&r); r.r0 = 0x5000; r.r1 = psp_seg; dos(&r); }
    clr(&ir); ir.r0 = 0x3D00; ir.r3 = (uint32_t)ini_path;
    if (!dos(&ir)) {
        ini_fh = ir.r0 & 0xFFFF; ini_n = ini_i = 0;
        char want[12], l[80];
        want[0] = '['; hex8(want + 1, disc_id_now); want[9] = ']'; want[10] = 0;
        int in = 0;
        while (ini_line(l, sizeof l) >= 0) {
            if (l[0] == '[') {
                in = 1;
                for (int i = 0; i < 10; i++) if (up(l[i]) != want[i]) { in = 0; break; }
                continue;
            }
            if (!in) continue;
            char *eq = l;
            while (*eq && *eq != '=') eq++;
            if (!*eq) continue;
            *eq = 0;
            char k0 = up(l[0]);
            if (k0 >= '0' && k0 <= '9') {
                unsigned n = 0;
                for (char *p = l; *p >= '0' && *p <= '9'; p++) n = n * 10 + *p - '0';
                if (n < MAXT) copy_val(titles[n], eq + 1);
            } else if (k0 == 'T' && up(l[1]) == 'I') copy_val(disc_title, eq + 1);
            else if (k0 == 'A' && up(l[1]) == 'R') copy_val(disc_artist, eq + 1);
        }
        clr(&ir); ir.r0 = 0x3E00; ir.r1 = ini_fh; dos(&ir);
    }
    if (resident) { clr(&r); r.r0 = 0x5000; r.r1 = other; dos(&r); }
}

static int read_toc(void)
{
    uint8_t b[8];
    toc.ok = 0;
    memset(b, 0, sizeof b);
    b[0] = IOI_DISKINFO;
    if (ioctl(0, b, 7) & ST_ERROR) return -1;
    toc.first = b[1]; toc.last = b[2];
    toc.leadout = rb_to_lba(rd32le(b + 3));
    if (toc.first < 1 || toc.last > 99 || toc.first > toc.last) return -1;
    for (int t = toc.first; t <= toc.last; t++) {
        memset(b, 0, sizeof b);
        b[0] = IOI_TRACKINFO; b[1] = t;
        if (ioctl(0, b, 7) & ST_ERROR) return -1;
        toc.start[t] = rb_to_lba(rd32le(b + 2));
        toc.data[t] = (b[6] & 0x40) != 0;
    }
    toc.ok = 1;
    disc_id_now = disc_id();
    load_titles();
    return 0;
}

static uint32_t track_end(int t) { return t < toc.last ? toc.start[t + 1] : toc.leadout; }
static int audio_track(int t) { return toc.ok && t >= toc.first && t <= toc.last && !toc.data[t]; }
static int first_audio(void) { for (int t = toc.first; t <= toc.last; t++) if (audio_track(t)) return t; return 0; }
static int naudio(void) { int n = 0; for (int t = toc.first; t <= toc.last; t++) if (audio_track(t)) n++; return n; }

/* the drive's state now */
static void poll(void)
{
    uint8_t b[12];
    memset(b, 0, sizeof b);
    b[0] = IOI_MEDIACHG;
    ioctl(0, b, 2);
    if ((int8_t)b[1] == -1) { toc.ok = 0; prog = 0; }
    memset(b, 0, sizeof b);
    b[0] = IOI_DEVSTAT;
    ioctl(0, b, 5);
    uint32_t ds = rd32le(b + 1);
    if (ds & (DS_DOOROPEN | DS_NODISC)) {
        st.state = (ds & DS_DOOROPEN) ? ST_OPEN : ST_NODISC;
        toc.ok = 0; prog = 0;
        st.track = 0; st.rel = st.abs = 0;
        return;
    }
    if (!toc.ok && read_toc()) { st.state = ST_NODISC; return; }
    if (titles_pending && dos_ok) load_titles();
    memset(b, 0, sizeof b);
    b[0] = IOI_QCHAN;
    uint16_t s = ioctl(0, b, 11);
    if (!(s & ST_ERROR)) {
        st.track = bcd2bin(b[2]);
        st.index = b[3];
        st.rel = b[4] * 60 + b[5];
        st.abs = b[8] * 60 + b[9];
        st.pos = b[8] * 4500 + b[9] * 75 + b[10] - 150;
    }
    if (s & ST_BUSY) { st.state = ST_PLAY; return; }
    memset(b, 0, sizeof b);
    b[0] = IOI_AUDIOSTAT;
    ioctl(0, b, 11);
    st.state = (rd16le(b + 1) & 1) ? ST_PAUSE : ST_STOP;
}

static int msg_on(void) { return status_msg[0] && (int32_t)(status_until - ARMDOS_BIOS_TICKS) > 0; }
static void say_status(const char *m)
{
    int n = 0;
    while (m[n] && n < (int)sizeof status_msg - 1) { status_msg[n] = m[n]; n++; }
    status_msg[n] = 0;
    status_until = ARMDOS_BIOS_TICKS + 55;
}

static int play_range(uint32_t from, uint32_t to)
{
    struct cdreq_play q;
    memset(&q, 0, sizeof q);
    q.h.len = sizeof q; q.h.cmd = C_PLAY;
    q.addrmode = 0; q.start = from; q.count = to - from;
    return (request(&q) & ST_ERROR) ? -1 : 0;
}

static void stop_req(void)
{
    struct cdreq q;
    memset(&q, 0, sizeof q);
    q.len = sizeof q; q.cmd = C_STOP;
    request(&q);
}

/* play track t (to the end of the disc, or only t when shuffling) */
static void play_track(int t)
{
    if (!audio_track(t)) { say_status("That is not an audio track."); return; }
    uint32_t end = shuffle ? track_end(t) : toc.leadout;
    if (!shuffle) {                     /* stop before a data track after the audio */
        for (int k = t + 1; k <= toc.last; k++) if (toc.data[k]) { end = toc.start[k]; break; }
    }
    if (play_range(toc.start[t], end)) { say_status("The drive could not play that track."); prog = 0; return; }
    prog = t;
    poll();
}

static void make_order(int first)
{
    norder = 0;
    for (int t = toc.first; t <= toc.last; t++) if (audio_track(t)) order[norder++] = t;
    for (int i = norder - 1; i > 0; i--) {
        rnd = rnd * 1103515245u + 12345u + ARMDOS_BIOS_TICKS;
        int j = (rnd >> 16) % (i + 1);
        uint8_t x = order[i]; order[i] = order[j]; order[j] = x;
    }
    opos = 0;
    if (first) for (int i = 0; i < norder; i++) if (order[i] == first) { order[i] = order[0]; order[0] = first; }
}

static void cmd_play(void)
{
    if (!toc.ok) { say_status("No disc."); return; }
    if (st.state == ST_PAUSE) {
        struct cdreq q; memset(&q, 0, sizeof q);
        q.len = sizeof q; q.cmd = C_RESUME;
        request(&q);
        poll();
        return;
    }
    if (st.state == ST_PLAY) return;
    if (!naudio()) { say_status("This disc has no audio tracks."); return; }
    if (shuffle) { make_order(0); play_track(order[0]); }
    else play_track(first_audio());
}
static void cmd_pause(void)
{
    if (st.state == ST_PLAY) { stop_req(); poll(); }
    else if (st.state == ST_PAUSE) cmd_play();
}
static void cmd_playpause(void) { if (st.state == ST_PLAY) cmd_pause(); else cmd_play(); }
static void cmd_stop(void)
{
    if (st.state == ST_PLAY) stop_req();
    if (st.state == ST_PLAY || st.state == ST_PAUSE) stop_req();
    prog = 0;
    poll();
}
static void cmd_track(int t)
{
    if (!toc.ok) return;
    if (shuffle) {                      /* the chosen track goes first in a new order */
        make_order(t);
    }
    play_track(t);
}
static void cmd_skip(int dir)
{
    if (!toc.ok) return;
    int cur = (st.state == ST_PLAY || st.state == ST_PAUSE) ? st.track : 0;
    if (shuffle && norder) {
        if (dir > 0) { if (opos + 1 < norder) opos++; else if (repeat) make_order(0); else { cmd_stop(); return; } }
        else if (cur && st.rel < 3) { if (opos) opos--; }
        play_track(order[opos]);
        return;
    }
    int t;
    if (dir > 0) {
        for (t = cur + 1; t <= toc.last && !audio_track(t); t++) ;
        if (t > toc.last) { if (!repeat) return; t = first_audio(); }
    } else {
        t = cur ? cur : first_audio();
        if (cur && st.rel < 3) {
            for (t = cur - 1; t >= toc.first && !audio_track(t); t--) ;
            if (t < toc.first) t = cur;
        }
    }
    play_track(t);
}
static void cmd_eject(void)
{
    uint8_t b[2] = { IOO_EJECT, 0 };
    if (st.state == ST_OPEN) b[0] = IOO_CLOSETRAY;
    if (st.state == ST_PLAY || st.state == ST_PAUSE) { stop_req(); stop_req(); }
    prog = 0;
    if (ioctl(1, b, 1) & ST_ERROR) say_status("The drive did not respond.");
    toc.ok = 0;
    poll();
}

/* a play of ours ended: shuffle / repeat go on */
static void advance(void)
{
    if (!prog || !toc.ok) return;
    if (st.state == ST_PLAY || st.state == ST_PAUSE) return;
    if (shuffle && norder) {
        if (opos + 1 < norder) { opos++; play_track(order[opos]); return; }
        if (repeat) { make_order(0); play_track(order[0]); return; }
    } else if (repeat) { play_track(first_audio()); return; }
    prog = 0;
}

/* ------------------------------------------------------------- the screen */

static volatile uint16_t *vram;
static int cols = 80;

static void putc_at(int x, int y, uint8_t c, uint8_t a) { if (x >= 0 && x < cols && y >= 0 && y < 25) vram[y * cols + x] = c | (a << 8); }
static void put(int x, int y, const char *s, uint8_t a) { while (*s) putc_at(x++, y, (uint8_t)*s++, a); }
static void fill(int x, int y, int w, int h, uint8_t c, uint8_t a)
{
    for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) putc_at(x + i, y + j, c, a);
}
static void putn(int x, int y, const char *s, int w, uint8_t a)     /* padded / cut to w */
{
    for (int i = 0; i < w; i++) putc_at(x + i, y, *s ? (uint8_t)*s++ : ' ', a);
}
static void frame(int x, int y, int w, int h, uint8_t a, int dbl)
{
    uint8_t hz = dbl ? 0xCD : 0xC4, vt = dbl ? 0xBA : 0xB3;
    for (int i = 1; i < w - 1; i++) { putc_at(x + i, y, hz, a); putc_at(x + i, y + h - 1, hz, a); }
    for (int j = 1; j < h - 1; j++) { putc_at(x, y + j, vt, a); putc_at(x + w - 1, y + j, vt, a); }
    putc_at(x, y, dbl ? 0xC9 : 0xDA, a); putc_at(x + w - 1, y, dbl ? 0xBB : 0xBF, a);
    putc_at(x, y + h - 1, dbl ? 0xC8 : 0xC0, a); putc_at(x + w - 1, y + h - 1, dbl ? 0xBC : 0xD9, a);
}
static void shadow(int x, int y, int w, int h)
{
    for (int j = 1; j <= h; j++) for (int i = 0; i < 2; i++) {
        int xx = x + w + i, yy = y + j;
        if (xx < cols && yy < 25) vram[yy * cols + xx] = (vram[yy * cols + xx] & 0xFF) | 0x0800;
    }
    for (int i = 2; i < w + 2; i++) if (x + i < cols && y + h < 25) vram[(y + h) * cols + x + i] = (vram[(y + h) * cols + x + i] & 0xFF) | 0x0800;
}

/* LCD digits: seven segments on a w x h grid of half-block "pixels"
   (two per character cell, one above the other). Unlit segments are drawn
   dimly, like the ghost segments of a real LCD. */
static const uint8_t segs[10] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F };
#define LCD_ON  0x0A
#define LCD_OFF 0x08
#define LCD_BG  0x00

static unsigned seg_at(int px, int py, int w, int h)
{
    int mid = (h - 1) / 2;
    unsigned m = 0;
    if (py == 0) m |= 0x01;
    if (py == h - 1) m |= 0x08;
    if (py == mid) m |= 0x40;
    if (px == 0 && py <= mid) m |= 0x20;
    if (px == w - 1 && py <= mid) m |= 0x02;
    if (px == 0 && py >= mid) m |= 0x10;
    if (px == w - 1 && py >= mid) m |= 0x04;
    return m;
}
static void lcd_digit(int x, int y, int c, int w, int h)
{
    unsigned lit = c >= '0' && c <= '9' ? segs[c - '0'] : c == '-' ? 0x40 : 0;
    for (int row = 0; row < (h + 1) / 2; row++) {
        for (int px = 0; px < w; px++) {
            unsigned s0 = seg_at(px, 2 * row, w, h), s1 = 2 * row + 1 < h ? seg_at(px, 2 * row + 1, w, h) : 0;
            int l0 = (s0 & lit) != 0, l1 = (s1 & lit) != 0;
            uint8_t ch, a = LCD_ON;
            if (l0 || l1) ch = l0 && l1 ? 0xDB : l0 ? 0xDF : 0xDC;
            else { a = LCD_OFF; ch = s0 && s1 ? 0xDB : s0 ? 0xDF : s1 ? 0xDC : ' '; }
            putc_at(x + px, y + row, ch, a | (LCD_BG << 4));
        }
    }
}
/* "NN MM:SS"-style strings; returns the width used */
static int lcd_text(int x, int y, const char *s, int w, int h)
{
    int x0 = x;
    for (; *s; s++) {
        if (*s == ':' || *s == ';') {           /* ';' = an unlit colon */
            uint8_t a = *s == ':' ? LCD_ON : LCD_OFF;
            if (h < 7) { putc_at(x, y, 0xDC, a); putc_at(x, y + 1, 0xDC, a); }     /* dots at pixel rows 1, 3 */
            else { putc_at(x, y + 1, 0xDF, a); putc_at(x, y + 2, 0xDF, a); }      /* rows 2, 4 */
            x += 2;
            continue;
        }
        if (*s == ' ') { x += 2; continue; }
        lcd_digit(x, y, *s, w, h);
        x += w + 1;
    }
    return x - x0;
}

static const char *track_title(int t)
{
    if (t >= 1 && t <= MAXT && titles[t - 1][0]) return titles[t - 1];
    return 0;
}
static char *track_name(char *b, int t)
{
    const char *tt = track_title(t);
    if (tt) return scat(b, tt);
    if (toc.ok && toc.data[t]) return scat(b, "(data track)");
    b = scat(b, "Track "); return udec(b, t);
}
static uint32_t track_secs(int t) { return (track_end(t) - toc.start[t]) / 75; }

/* what the LCD shows: track and time strings, and a blink phase for PAUSE */
static void lcd_strings(char *trk, char *tim)
{
    int blank = st.state == ST_PAUSE && (ARMDOS_BIOS_TICKS & 8);
    if (st.state == ST_PLAY || st.state == ST_PAUSE) {
        dec2(trk, st.track);
        char *p = tim;
        uint32_t m = st.rel / 60, s = st.rel % 60;
        if (m > 99) m = 99;
        p = dec2(p, m); *p++ = blank ? ';' : ':'; dec2(p, s);
        if (blank) { tim[0] = tim[1] = tim[3] = tim[4] = ' '; }
    } else if (toc.ok) {
        int t = first_audio();
        dec2(trk, t ? t : toc.first);
        char *p = tim;
        uint32_t tot = (toc.leadout + 150) / 75;
        p = dec2(p, tot / 60 > 99 ? 99 : tot / 60); *p++ = ':'; dec2(p, tot % 60);
    } else {
        scat(trk, "--"); scat(tim, "--;--");
    }
}

static const char *state_word(void)
{
    switch (st.state) {
    case ST_PLAY: return "PLAY";
    case ST_PAUSE: return "PAUSE";
    case ST_STOP: return "STOP";
    case ST_OPEN: return "OPEN";
    }
    return "NO DISC";
}

/* ---- the full-screen player */

enum { A_DESK = 0x17, A_BAR = 0x70, A_BARK = 0x74, A_PANEL = 0x78, A_LIST = 0x1F, A_LISTSEL = 0x3F,
       A_LISTPLAY = 0x1E, A_BTN = 0x70, A_BTNK = 0x74, A_BTNON = 0x2F, A_BTNONK = 0x2E, A_DIM = 0x19 };

struct button { const char *label; int key; int x; };
static struct button btns[] = {
    { "\x10 Play", 'P', 0 }, { "\xBA Pause", 'A', 0 }, { "\xFE Stop", 'S', 0 }, { "\x11\x11 Prev", 'V', 0 },
    { "Next \x10\x10", 'N', 0 }, { "\x1E Eject", 'E', 0 }, { "Shuffle", 'H', 0 }, { "Repeat", 'R', 0 },
};
#define NBTN 8
#define BTN_Y 13
#define LIST_Y 16
#define LIST_H 7
static int sel = 1, top = 1, sel_touched;

static void draw_button(int i)
{
    struct button *b = &btns[i];
    int on = (i == 0 && st.state == ST_PLAY) || (i == 1 && st.state == ST_PAUSE) ||
             (i == 6 && shuffle) || (i == 7 && repeat);
    uint8_t a = on ? A_BTNON : A_BTN, ak = on ? A_BTNONK : A_BTNK;
    int w = slen(b->label) + 2;
    putc_at(b->x, BTN_Y, ' ', a);
    int hk = 0;
    for (int k = 0; b->label[k]; k++) {
        uint8_t c = b->label[k];
        putc_at(b->x + 1 + k, BTN_Y, c, (!hk && up(c) == b->key) ? (hk = 1, ak) : a);
    }
    putc_at(b->x + w - 1, BTN_Y, ' ', a);
    /* the button's shadow on the desk */
    putc_at(b->x + w, BTN_Y, 0xDC, 0x10);
    for (int k = 1; k <= w; k++) putc_at(b->x + k, BTN_Y + 1, 0xDF, 0x10);
}

static void full_static(void)
{
    fill(0, 0, cols, 25, ' ', A_DESK);
    fill(0, 0, cols, 1, ' ', A_BAR);
    put(2, 0, "ARM-DOS CD Player  1.00", A_BAR);
    char d[] = "Drive D:";
    d[6] = 'A' + cdl;
    put(cols - 10, 0, d, A_BAR);
    /* the LCD, in a grey panel */
    fill(2, 2, 76, 9, ' ', A_PANEL);
    frame(2, 2, 76, 9, 0x7F, 0);
    fill(4, 3, 72, 7, ' ', 0x00);
    shadow(2, 2, 76, 9);
    int x = 2;
    for (int i = 0; i < NBTN; i++) {           /* 76 columns: a button, its shadow, ... */
        btns[i].x = x;
        x += slen(btns[i].label) + 2 + 1;
    }
    fill(0, 24, cols, 1, ' ', A_BAR);
}

static void full_lcd(void)
{
    char trk[4], tim[8];
    lcd_strings(trk, tim);
    put(6, 3, "TRACK", 0x02);
    put(26, 3, "MIN   SEC", 0x02);
    lcd_text(6, 4, trk, 6, 7);
    lcd_text(26, 4, tim, 6, 7);
    static const char *const ind[5] = { "PLAY", "PAUSE", "STOP", "SHUFFLE", "REPEAT" };
    int lit[5] = { st.state == ST_PLAY, st.state == ST_PAUSE, st.state == ST_STOP, shuffle, repeat };
    for (int i = 0; i < 5; i++) putn(62, 3 + i, ind[i], 8, lit[i] ? LCD_ON : 0x08);
    /* the line under the digits */
    char b[80], *p = b;
    if (st.state == ST_PLAY || st.state == ST_PAUSE) {
        p = track_name(p, st.track);
        p = scat(p, "  ");
        p = mmss(p, st.rel); p = scat(p, " / "); mmss(p, track_secs(st.track) );
    } else if (st.state == ST_OPEN) scat(p, "The drive is open.  Press E to close it.");
    else if (st.state == ST_NODISC) scat(p, "No disc.  Put a CD in the drive.");
    else if (!naudio()) scat(p, "A data CD: no audio tracks to play.");
    else {
        p = udec(p, naudio()); p = scat(p, " audio tracks.  Press P or Enter to play.");
    }
    putn(6, 8, b, 54, LCD_ON);
    if (st.state == ST_PLAY || st.state == ST_PAUSE) {
        p = scat(b, "DISC ");
        mmss(p, st.abs);
    } else b[0] = 0;
    putn(62, 8, b, 12, 0x02);
}

static void full_info(void)
{
    char b[80], *p = b;
    fill(2, 11, 76, 1, ' ', A_DESK);
    if (toc.ok) {
        p = scat(p, disc_title[0] ? disc_title : "Audio CD");
        if (disc_artist[0]) { p = scat(p, " - "); p = scat(p, disc_artist); }
        p = scat(p, "   \xFA   "); p = udec(p, toc.last - toc.first + 1); p = scat(p, " tracks   \xFA   ");
        mmss(p, (toc.leadout + 150) / 75);
        put(3, 11, b, 0x1F);
    } else put(3, 11, "No disc in drive ", 0x1F);
}

static void full_list(void)
{
    frame(2, LIST_Y - 1, 76, LIST_H + 2, 0x1B, 1);
    put(4, LIST_Y - 1, " Tracks ", 0x1E);
    if (!toc.ok) {
        fill(3, LIST_Y, 74, LIST_H, ' ', A_LIST);
        return;
    }
    int n = toc.last - toc.first + 1;
    if (sel < toc.first) sel = toc.first;
    if (sel > toc.last) sel = toc.last;
    if (sel < top) top = sel;
    if (sel >= top + LIST_H) top = sel - LIST_H + 1;
    if (top < toc.first) top = toc.first;
    for (int i = 0; i < LIST_H; i++) {
        int t = top + i;
        uint8_t a = A_LIST;
        char b[80], *p = b;
        if (t > toc.last) { fill(3, LIST_Y + i, 74, 1, ' ', A_LIST); continue; }
        int cur = (st.state == ST_PLAY || st.state == ST_PAUSE) && st.track == t;
        if (t == sel) a = A_LISTSEL; else if (cur) a = A_LISTPLAY;
        *p++ = ' ';
        *p++ = cur ? (st.state == ST_PLAY ? 0x10 : 0xBA) : ' ';
        *p++ = ' ';
        p = dec2(p, t);
        p = scat(p, "   ");
        char nm[TLEN + 16];
        track_name(nm, t);
        int k = 0;
        for (; nm[k] && k < 50; k++) *p++ = nm[k];
        for (; k < 52; k++) *p++ = ' ';
        char tm[8];
        mmss(tm, track_secs(t));
        for (int j = slen(tm); j < 6; j++) *p++ = ' ';
        p = scat(p, tm);
        *p = 0;
        putn(3, LIST_Y + i, b, 74, a);
    }
    /* scroll marks */
    putc_at(77, LIST_Y, top > toc.first ? 0x1E : 0xBA, 0x1B);
    putc_at(77, LIST_Y + LIST_H - 1, top + LIST_H <= toc.first + n - 1 ? 0x1F : 0xBA, 0x1B);
}

static void full_help(void)
{
    fill(0, 24, cols, 1, ' ', A_BAR);
    if (msg_on()) { put(1, 24, status_msg, 0x74); return; }
    static const char *const h[] = { "1-9", "Track", "Enter", "Play", "Space", "Pause",
                                     "\x1B\x1A", "Prev/Next", "S", "Stop", "E", "Eject", "Esc", "Exit" };
    int x = 1;
    for (unsigned i = 0; i < sizeof h / sizeof h[0]; i++) {
        put(x, 24, h[i], i & 1 ? A_BAR : A_BARK);
        x += slen(h[i]) + (i & 1 ? 2 : 1);
    }
}

static void full_draw(void)
{
    full_lcd();
    full_info();
    for (int i = 0; i < NBTN; i++) draw_button(i);
    full_list();
    full_help();
}

/* ---- the pop-up window */

#define PX 13
#define PY 5
#define PW 54
#define PH 14
enum { P_WIN = 0x1F, P_TXT = 0x1B, P_KEY = 0x1E, P_DIM = 0x17 };

static void pop_static(void)
{
    fill(PX, PY, PW, PH, ' ', P_WIN);
    frame(PX, PY, PW, PH, P_WIN, 1);
    put(PX + (PW - 13) / 2, PY, " CD Player ", 0x1E);
    fill(PX + 2, PY + 1, PW - 4, 5, ' ', 0x00);
    shadow(PX, PY, PW, PH);
}
static void pop_draw(void)
{
    char trk[4], tim[8], b[80], *p;
    lcd_strings(trk, tim);
    lcd_text(PX + 4, PY + 2, trk, 4, 5);
    lcd_text(PX + 15, PY + 2, tim, 4, 5);
    putn(PX + 38, PY + 2, state_word(), 8, LCD_ON);
    putn(PX + 38, PY + 3, shuffle ? "SHUFFLE" : "", 8, LCD_ON);
    putn(PX + 38, PY + 4, repeat ? "REPEAT" : "", 8, LCD_ON);
    p = b;
    if (st.state == ST_PLAY || st.state == ST_PAUSE) {
        p = scat(p, "Track "); p = udec(p, st.track); p = scat(p, ": "); track_name(p, st.track);
    } else if (toc.ok) {
        p = scat(p, disc_title[0] ? disc_title : "Audio CD"); p = scat(p, ", "); p = udec(p, naudio()); scat(p, " audio tracks");
    } else scat(p, st.state == ST_OPEN ? "The drive is open." : "No disc in the drive.");
    putn(PX + 2, PY + 7, b, PW - 4, P_WIN);
    putn(PX + 2, PY + 9, "1-9 Track   P Play/Pause   S Stop   \x1B\x1A Prev/Next", PW - 4, P_TXT);
    putn(PX + 2, PY + 10, "E Eject/Load   H Shuffle   R Repeat", PW - 4, P_TXT);
    if (msg_on()) putn(PX + 2, PY + 12, status_msg, PW - 4, 0x1E);
    else putn(PX + 2, PY + 12, "Esc returns to your program; the music plays on.", PW - 4, P_DIM);
}

/* ---- keys, mouse, the loop */

static int key_ready(int *k)
{
    struct armregs r; clr(&r);
    r.r0 = 0x0100;
    _armdos_int16(&r);
    if (r.cpsr & ARM_CPSR_Z) return 0;
    clr(&r); r.r0 = 0x0000; _armdos_int16(&r);
    *k = r.r0 & 0xFFFF;
    return 1;
}

static int mouse;
static int mouse_click(int *x, int *y)
{
    struct armregs r; clr(&r);
    r.r0 = 5; r.r1 = 0;
    _armdos_int33(&r);
    if (!(r.r1 & 0xFFFF)) return 0;
    *x = (r.r2 & 0xFFFF) / 8; *y = (r.r3 & 0xFFFF) / 8;
    return 1;
}
static void mouse_show(int on) { if (mouse) { struct armregs r; clr(&r); r.r0 = on ? 1 : 2; _armdos_int33(&r); } }

/* 0 = go on, 1 = leave */
static int handle_key(int k, int full)
{
    int c = k & 0xFF, sc = k >> 8;
    if (c >= 'a' && c <= 'z') c -= 32;
    if (c == 27 || (full && (c == 'Q' || sc == 0x44))) return 1;
    if (c >= '1' && c <= '9') {
        int t = c - '0';
        if (toc.ok && t >= toc.first && t <= toc.last) { cmd_track(t); sel = t; }
        else say_status("There is no such track.");
        return 0;
    }
    if (c == 0 || c == 0xE0) {
        switch (sc) {
        case 0x4B: cmd_skip(-1); break;
        case 0x4D: cmd_skip(1); break;
        case 0x48: sel--; sel_touched = 90; break;
        case 0x50: sel++; sel_touched = 90; break;
        case 0x47: sel = toc.first; sel_touched = 90; break;
        case 0x4F: sel = toc.last; sel_touched = 90; break;
        }
        return 0;
    }
    switch (c) {
    case 13: if (full) cmd_track(sel); else cmd_play(); break;
    case ' ': cmd_playpause(); break;
    case 'P': cmd_playpause(); break;
    case 'A': cmd_pause(); break;
    case 'S': cmd_stop(); break;
    case 'N': case '.': case '>': cmd_skip(1); break;
    case 'V': case ',': case '<': cmd_skip(-1); break;
    case 'E': cmd_eject(); break;
    case 'H':
        shuffle = !shuffle;
        if (toc.ok && st.state == ST_PLAY && audio_track(st.track)) {
            /* go on from here, to the end of this track (shuffle) or of the audio */
            int t = st.track;
            if (shuffle) make_order(t);
            uint32_t end = shuffle ? track_end(t) : toc.leadout;
            if (!shuffle) for (int k = t + 1; k <= toc.last; k++) if (toc.data[k]) { end = toc.start[k]; break; }
            if (st.pos >= toc.start[t] && st.pos < end && !play_range(st.pos, end)) prog = t;
        }
        say_status(shuffle ? "Shuffle on: the tracks play in a random order." : "Shuffle off.");
        break;
    case 'R': repeat = !repeat; say_status(repeat ? "Repeat on." : "Repeat off."); break;
    }
    return 0;
}

static void click(int x, int y)
{
    if (y == BTN_Y) {
        for (int i = 0; i < NBTN; i++) {
            int w = slen(btns[i].label) + 2;
            if (x >= btns[i].x && x < btns[i].x + w) {
                static const uint16_t keys[NBTN] = { 'P', 'A', 'S', 'V', 'N', 'E', 'H', 'R' };
                if (i == 0) cmd_play(); else handle_key(keys[i], 1);
                return;
            }
        }
    }
    if (y >= LIST_Y && y < LIST_Y + LIST_H && x >= 3 && x < 77 && toc.ok) {
        int t = top + (y - LIST_Y);
        if (t <= toc.last) { sel = t; cmd_track(t); }
    }
}

static void run(int full)
{
    uint32_t last = ARMDOS_BIOS_TICKS - 100;
    int first = 1;
    for (;;) {
        uint32_t now = ARMDOS_BIOS_TICKS;
        int k, dirty = 0;
        if (key_ready(&k)) {
            if (handle_key(k, full)) return;
            dirty = 1;
        }
        int mx, my;
        if (full && mouse && mouse_click(&mx, &my)) { click(mx, my); dirty = 1; }
        if (first || dirty || now - last >= 4) {
            if (sel_touched) sel_touched = sel_touched > 4 ? sel_touched - 4 : 0;
            last = now;
            poll();
            advance();
            if (!sel_touched && (st.state == ST_PLAY || st.state == ST_PAUSE) && st.track) sel = st.track;
            if (full) { mouse_show(0); full_draw(); mouse_show(1); }
            else pop_draw();
            first = 0;
            continue;
        }
        armdos_halt();
    }
}

/* ------------------------------------------------------------- resident */

static armdos_vect_t old08, old09, old10, old13, old15, old21, old28, old2f;
static volatile uint8_t *indos;
static volatile int hot, active, busy10, busy13, busycd, conin, beep_ticks, bg_ticks;
static uint8_t *stack_top;
static uint32_t psp_addr;

void call_on_stack(void (*fn)(void), void *sp);     /* cdasm.S */

static void beep(void)
{
    armdos_outb(0x43, 0xB6);
    armdos_outb(0x42, 1356 & 0xFF);
    armdos_outb(0x42, 1356 >> 8);
    armdos_outb(0x61, armdos_inb(0x61) | 3);
    beep_ticks = 3;
}

/*
 * When may the pop-up run? Never during a critical error, a BIOS video or disk
 * call, or a call to the CD-ROM extensions (INT 2Fh AH=11h/15h: their sector
 * cache and the drive are not re-entrant). Otherwise: when DOS is not busy
 * (InDOS = 0), from INT 28h (DOS idle at a 01h-0Ch console wait), or while the
 * program waits for the keyboard inside DOS in 3Fh from handle 0 (how ZORK
 * reads its commands - DOS gives no INT 28h there). In that last case the
 * pop-up makes no DOS calls (dos_ok = 0): it only talks to the CD-ROM
 * extensions' device-request function, which does not enter DOS.
 * Returns 0 = not now, 1 = safe with DOS calls, 2 = safe without.
 */
static int safe_now(int from_idle)
{
    if (indos[-1] || busy10 || busy13 || busycd) return 0;
    if (from_idle || !indos[0]) return 1;
    if (conin && indos[0] == 1) return 2;
    return 0;
}

static uint16_t saved[80 * 25];
static uint16_t saved_cursor, saved_shape;

static void cursor_hide(void) { struct armregs r; clr(&r); r.r0 = 0x0100; r.r2 = 0x2000; _armdos_int10(&r); }
static void cursor_restore(void)
{
    struct armregs r; clr(&r);
    r.r0 = 0x0100; r.r2 = saved_shape; _armdos_int10(&r);
    clr(&r); r.r0 = 0x0200; r.r1 = ARMDOS_BDA[0x62] << 8; r.r3 = saved_cursor; _armdos_int10(&r);
}
static void screen_save(void)
{
    vram = (volatile uint16_t *)((ARMDOS_BDA[0x49] == 7 ? 0xB0000 : 0xB8000) + *(volatile uint16_t *)0x44E);
    cols = 80;
    for (int i = 0; i < 80 * 25; i++) saved[i] = vram[i];
    saved_cursor = *(volatile uint16_t *)(0x450 + 2 * ARMDOS_BDA[0x62]);
    saved_shape = *(volatile uint16_t *)0x460;
}
static void screen_restore(void) { for (int i = 0; i < 80 * 25; i++) vram[i] = saved[i]; cursor_restore(); }

static void popup_main(void)
{
    screen_save();
    cursor_hide();
    pop_static();
    run(0);
    screen_restore();
}

/* shuffle / repeat while the pop-up is closed */
static void bg_main(void) { poll(); advance(); }

static void enter(void (*fn)(void), int how)
{
    active = 1;
    dos_ok = how == 1;
    uint32_t cpsr;
    __asm__ volatile("mrs %0, cpsr" : "=r"(cpsr));
    armdos_enable();
    call_on_stack(fn, stack_top);
    __asm__ volatile("msr cpsr_c, %0" :: "r"(cpsr) : "memory");
    dos_ok = 1;
    active = 0;
}

static void try_popup(int from_idle)
{
    int how;
    if (active || !(how = safe_now(from_idle))) return;
    if (hot) {
        hot = 0;
        uint8_t m = ARMDOS_BDA[0x49];
        if (m != 2 && m != 3 && m != 7) { beep(); return; }     /* graphics or 40 columns: no */
        enter(popup_main, how);
        return;
    }
    if (prog && (shuffle || repeat) && bg_ticks >= 36) { bg_ticks = 0; enter(bg_main, how); }
}

static void int08(struct armregs *f)
{
    old08(f);
    if (beep_ticks && --beep_ticks == 0) armdos_outb(0x61, armdos_inb(0x61) & ~3);
    bg_ticks++;
    try_popup(0);
}
static void int09(struct armregs *f) { old09(f); try_popup(0); }
static void int15(struct armregs *f)
{
    if ((f->r0 & 0xFF00) == 0x4F00) {
        uint8_t sc = f->r0 & 0xFF;
        if (sc == 0x2E && (ARMDOS_BDA[0x17] & 0x0C) == 0x0C) {     /* C with Ctrl+Alt */
            hot = 1;
            f->cpsr &= ~ARM_CPSR_C;                 /* swallow it */
            return;
        }
    }
    old15(f);
}
static void int28(struct armregs *f) { old28(f); try_popup(1); }
static void int21(struct armregs *f)
{
    unsigned ah = (f->r0 >> 8) & 0xFF;
    /* a keyboard wait inside DOS (set at every entry, so an aborted call leaves nothing behind) */
    conin = ah == 0x3F && (f->r1 & 0xFFFF) == 0;      /* 01h-0Ch waits have INT 28h */
    old21(f);
    conin = 0;
}
static void int10(struct armregs *f) { busy10++; old10(f); busy10--; }
static void int13(struct armregs *f) { busy13++; old13(f); busy13--; }
static void int2f(struct armregs *f)
{
    unsigned ah = (f->r0 >> 8) & 0xFF;
    if (ah == MPLEX_ID) {
        if ((f->r0 & 0xFF) == 0x00) { f->r0 = (f->r0 & ~0xFFu) | 0xFF; f->r1 = psp_addr; f->r4 = SIG; }
        return;
    }
    if (ah == 0x11 || ah == 0x15) { busycd++; old2f(f); busycd--; return; }     /* the CD-ROM extensions */
    old2f(f);
}

/* ================================================================ transient */

static void say(const char *s)
{
    struct armregs r; clr(&r);
    r.r0 = 0x4000; r.r1 = 1; r.r2 = slen(s); r.r3 = (uint32_t)s;
    dos(&r);
}
__attribute__((noreturn)) static void leave(int code)
{
    struct armregs r; clr(&r);
    r.r0 = 0x4C00 | code;
    dos(&r);
    for (;;) ;
}
static armdos_vect_t getvect(int n) { struct armregs r; clr(&r); r.r0 = 0x3500 | n; dos(&r); return (armdos_vect_t)r.r1; }
static void setvect(int n, armdos_vect_t h) { struct armregs r; clr(&r); r.r0 = 0x2500 | n; r.r3 = (uint32_t)h; dos(&r); }

static int installed(uint32_t *psp)
{
    struct armregs r; clr(&r);
    r.r0 = MPLEX_ID << 8;
    _armdos_int2f(&r);
    if ((r.r0 & 0xFF) == 0xFF && r.r4 == SIG) { *psp = r.r1; return 1; }
    return 0;
}

static int uninstall(uint32_t psp)
{
    static const uint8_t nums[8] = { 0x08, 0x09, 0x10, 0x13, 0x15, 0x21, 0x28, 0x2F };
    armdos_vect_t ours[8] = { int08, int09, int10, int13, int15, int21, int28, int2f };
    armdos_vect_t *olds[8] = { &old08, &old09, &old10, &old13, &old15, &old21, &old28, &old2f };
    for (int i = 0; i < 8; i++)
        if ((uint32_t)getvect(nums[i]) != (uint32_t)ours[i] - psp_addr + psp) return -1;
    armdos_disable();
    for (int i = 0; i < 8; i++) setvect(nums[i], *(armdos_vect_t *)((uint8_t *)olds[i] - psp_addr + psp));
    armdos_enable();
    struct armregs r; clr(&r);
    r.r0 = 0x4900; r.r8 = psp >> 4;
    dos(&r);
    return 0;
}

/* CDPLAY.INI next to the program: its path is after the environment strings */
static void find_ini(struct psp *psp)
{
    if (!psp->envseg) return;
    const char *e = ARMDOS_SEG2PTR(psp->envseg);
    for (int n = 0; n < 32768 && (e[0] || e[1]); n++) e++;
    e += 2;
    if (rd16le(e) < 1) return;
    e += 2;
    int k = 0, slash = -1;
    for (; e[k] && k < (int)sizeof ini_path - 12; k++) { ini_path[k] = e[k]; if (e[k] == '\\' || e[k] == ':') slash = k; }
    if (slash < 0) { ini_path[0] = 0; return; }
    scat(ini_path + slash + 1, "CDPLAY.INI");
}

static const char help[] =
    "Plays audio compact discs in the CD-ROM drive.\r\n\r\n"
    "CDPLAY [/R | /U]\r\n\r\n"
    "  /R  installs the pop-up player: press Ctrl+Alt+C in any text-mode program.\r\n"
    "  /U  removes the pop-up player from memory.\r\n\r\n"
    "Without a switch, CDPLAY runs the full-screen player. The music keeps\r\n"
    "playing when you leave it. Track titles are read from CDPLAY.INI.\r\n";

void __armdos_start(struct psp *psp, uint8_t *base, uint8_t *blockend, uint8_t *stacktop)
{
    (void)base; (void)blockend;
    psp_addr = (uint32_t)psp;
    psp_seg = psp_addr >> 4;
    const uint8_t *tail = psp->cmdtail;
    int opt_r = 0, opt_u = 0;
    for (int i = 1; i <= tail[0]; i++) {
        if (tail[i] != '/') continue;
        uint8_t c = up(tail[i + 1]);
        if (c == 'R') opt_r = 1;
        else if (c == 'U') opt_u = 1;
        else if (c == '?') { say(help); leave(0); }
        else { say("Invalid switch\r\n"); leave(1); }
    }

    uint32_t other;
    if (opt_u) {
        if (!installed(&other)) { say("The CD Player pop-up is not installed.\r\n"); leave(1); }
        if (uninstall(other)) { say("The CD Player pop-up cannot be removed: another program has hooked its interrupts.\r\n"); leave(2); }
        say("The CD Player pop-up has been removed from memory.\r\n");
        leave(0);
    }

    struct armregs r; clr(&r);
    r.r0 = 0x1500; r.r1 = 0;
    _armdos_int2f(&r);
    if (!(r.r1 & 0xFFFF)) {
        say("CD-ROM extensions not installed. Load ARMCDEX.EXE first\r\n"
            "(in AUTOEXEC.BAT: C:\\DOS\\ARMCDEX /D:ARMCD001).\r\n");
        leave(1);
    }
    cdl = r.r2 & 0xFFFF;
    find_ini(psp);

    if (opt_r) {
        if (installed(&other)) { say("The CD Player pop-up is already installed. Press Ctrl+Alt+C.\r\n"); leave(1); }
        resident = 1;
        clr(&r); r.r0 = 0x3400; dos(&r);
        indos = (volatile uint8_t *)r.r1;
        stack_top = (uint8_t *)((uint32_t)stacktop & ~7u);
        old08 = getvect(0x08); old09 = getvect(0x09); old10 = getvect(0x10);
        old13 = getvect(0x13); old15 = getvect(0x15); old21 = getvect(0x21); old28 = getvect(0x28); old2f = getvect(0x2F);
        armdos_disable();
        setvect(0x10, int10); setvect(0x13, int13); setvect(0x15, int15); setvect(0x21, int21);
        setvect(0x28, int28); setvect(0x2F, int2f); setvect(0x09, int09); setvect(0x08, int08);
        armdos_enable();
        poll();                                     /* the disc and its titles, while DOS calls are fine */
        say("ARM-DOS CD Player installed. Press Ctrl+Alt+C to pop it up.\r\n");
        clr(&r); r.r0 = 0x4900; r.r8 = psp->envseg;
        if (psp->envseg && !dos(&r)) psp->envseg = 0;
        uint32_t paras = ((uint32_t)stacktop - (uint32_t)psp + 15) >> 4;
        clr(&r); r.r0 = 0x3100; r.r3 = paras;
        dos(&r);
        for (;;) ;
    }

    /* the full-screen player */
    uint8_t m = ARMDOS_BDA[0x49];
    if (m != 2 && m != 3 && m != 7) { clr(&r); r.r0 = 0x0003; _armdos_int10(&r); }
    screen_save();
    cursor_hide();
    clr(&r); r.r0 = 0x0000;
    if (ARMDOS_IVT[0x33]) { _armdos_int33(&r); mouse = (r.r0 & 0xFFFF) == 0xFFFF; }
    poll();
    if (st.state == ST_PLAY || st.state == ST_PAUSE) sel = st.track;
    else if (toc.ok) sel = first_audio() ? first_audio() : toc.first;
    full_static();
    run(1);
    mouse_show(0);
    screen_restore();
    leave(0);
}
