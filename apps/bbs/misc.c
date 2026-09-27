/* misc.c - bulletins, who's online, userlog, settings, doors. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <unistd.h>
#include <process.h>
#include <direct.h>
#include "../term/lib/comm.h"
#include "../term/lib/scr.h"
#include "bbs.h"

/* ------------------------------------------------------------ word wrap */
/* print text wrapped at width columns; continuation lines get indent2 spaces.
 * Returns 0 if the caller stopped at a More prompt. */
static int wrap_out(const char *color, int indent1, int indent2, int width, const char *text)
{
    const char *s = text;
    int first = 1;
    while (*s) {
        int ind = first ? indent1 : indent2, room = width - ind, n = (int)strlen(s);
        if (n > room) {
            n = room;
            while (n > 0 && s[n] != ' ') n--;
            if (n == 0) n = room;
        }
        sio_printf("%s%*s%.*s@X07\n", color, ind, "", n, s);
        if (!sio_line_done()) return 0;
        s += n;
        while (*s == ' ') s++;
        first = 0;
    }
    return 1;
}

/* ------------------------------------------------------------ last callers */
/* LASTCALL.TXT: "name|city|MM-DD-YY HH:MM|bps" per line, newest last, the
 * last 10 kept (seeded with the regulars' calls from November 1989). */
#define LASTCALL "LASTCALL.TXT"
static int read_lastcalls(char rows[10][96])
{
    char line[96];
    int n = 0;
    FILE *f = fopen(LASTCALL, "r");
    if (!f) return 0;
    while (fgets(line, sizeof line, f)) {
        char *nl = strpbrk(line, "\r\n"); if (nl) *nl = 0;
        if (!strchr(line, '|')) continue;
        if (n == 10) { memmove(rows[0], rows[1], sizeof rows[0] * 9); n = 9; }
        snprintf(rows[n++], 96, "%s", line);
    }
    fclose(f);
    return n;
}

void lastcall_add(void)
{
    static char rows[10][96];
    char d[20];
    int n = read_lastcalls(rows);
    if (n == 10) { memmove(rows[0], rows[1], sizeof rows[0] * 9); n = 9; }
    now_str(d);
    snprintf(rows[n++], 96, "%.35s|%.30s|%s|%ld", user.name, user.city, d, sio_remote ? session_baud : 0L);
    FILE *f = fopen(LASTCALL, "w");
    if (!f) return;
    for (int i = 0; i < n; i++) fprintf(f, "%s\n", rows[i]);
    fclose(f);
}

void last_callers(void)
{
    static char rows[10][96];
    int n = read_lastcalls(rows);
    sio_cls();
    sio_printf("\n@X1F  THE LAST %d CALLERS TO %s  @X07\n\n", n, cfg.name);
    sio_puts("@X0F  #  Name                      From                      When           Speed@X07\n");
    sio_puts("@X08 \xC4\xC4\xC4 \xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4 \xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4 \xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4 \xC4\xC4\xC4\xC4\xC4\xC4@X07\n");
    sio_linecount = 5;
    for (int i = n - 1; i >= 0; i--) {                /* newest first */
        char *fld[4] = { rows[i], "", "", "0" }, *s = rows[i];
        for (int k = 1; k < 4 && (s = strchr(s, '|')); k++) { *s++ = 0; fld[k] = s; }
        long bps = atol(fld[3]);
        char sp[12];
        if (bps) snprintf(sp, sizeof sp, "%ld", bps); else strcpy(sp, "local");
        sio_printf("@X0E%3d@X07  @X0F%-25.25s @X07%-25.25s @X0B%-14.14s @X0A%6s@X07\n", n - i, fld[0], fld[1], fld[2], sp);
        if (!sio_line_done()) break;
    }
    if (!n) sio_puts("@X0CNobody has called yet.  You could be first!@X07\n");
}

/* ------------------------------------------------------------ door news */
/* Every door keeps NEWS.TXT ("MM-DD-YY  headline") and SCORES.TXT (a short
 * table) in its directory; the RISC Dragon's news file is DRAGNEWS.TXT. */
static int show_tail(const char *path, int max)
{
    static char lines[6][100];
    char b[100];
    int n = 0;
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    while (fgets(b, sizeof b, f)) {
        char *nl = strpbrk(b, "\r\n"); if (nl) *nl = 0;
        if (!b[0]) continue;
        if (n == max) { memmove(lines[0], lines[1], sizeof lines[0] * (max - 1)); n--; }
        snprintf(lines[n++], 100, "%s", b);
    }
    fclose(f);
    for (int i = 0; i < n; i++)
        if (!wrap_out("@X02", 3, 13, 78, lines[i])) return -1;
    return n;
}

static int show_scores(const char *path)
{
    char b[100];
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    for (int i = 0; i < 8 && fgets(b, sizeof b, f); i++) {
        char *nl = strpbrk(b, "\r\n"); if (nl) *nl = 0;
        sio_printf("%s   %.74s@X07\n", i ? "@X07" : "@X0B", b);
        if (!sio_line_done()) { fclose(f); return -1; }
    }
    fclose(f);
    return 1;
}

static void door_news(void)
{
    char p[96];
    sio_cls();
    sio_puts("\n@X1F  TODAY'S NEWS FROM THE DOORS  @X07\n");
    sio_linecount = 2;
    for (int i = 0; i < ndoors; i++) {
        sio_printf("\n@X0E%s@X07\n", doors[i].name);
        sio_linecount += 2;
        snprintf(p, sizeof p, "%s\\NEWS.TXT", doors[i].path);
        int r = show_tail(p, 3);
        if (!r) { snprintf(p, sizeof p, "%s\\DRAGNEWS.TXT", doors[i].path); r = show_tail(p, 3); }
        if (r < 0) return;
        if (!r) sio_puts("@X08   Quiet.  Too quiet.@X07\n");
        snprintf(p, sizeof p, "%s\\SCORES.TXT", doors[i].path);
        if (show_scores(p) < 0) return;
    }
}

/* ------------------------------------------------------------ bulletins */
/* BULLETn.ANS / .TXT; a file whose first line is "@@LASTCALLERS" or
 * "@@DOORNEWS" is generated on the spot. */
static int show_bulletin(int n)
{
    char p[48], first[20] = "";
    snprintf(p, sizeof p, "BULLETIN\\BULLET%d.TXT", n);
    FILE *f = fopen(p, "r");
    if (f) { if (!fgets(first, sizeof first, f)) first[0] = 0; fclose(f); }
    if (!strncmp(first, "@@LASTCALLERS", 13)) { last_callers(); return 1; }
    if (!strncmp(first, "@@DOORNEWS", 10)) { door_news(); return 1; }
    sio_cls();
    if (sio_ansi) { snprintf(p, sizeof p, "BULLETIN\\BULLET%d.ANS", n); if (sio_showfile(p)) return 1; }
    snprintf(p, sizeof p, "BULLETIN\\BULLET%d.TXT", n);
    return sio_showfile(p);
}

void bulletins(int at_logon)
{
    char b[8];
    if (at_logon) {
        if (!sio_yesno("\n@X0EView the bulletins", 1)) return;
    }
    for (;;) {
        sio_cls();
        if (!sio_showfile(sio_ansi ? "BULLETIN\\BULLET.ANS" : "BULLETIN\\BULLET.TXT")) {
            sio_puts("@X0CNo bulletins today.@X07\n");
            return;
        }
        sio_puts("\n@X0EBulletin # to read (Enter = quit): @X0F");
        sio_getline(b, 2, GL_DIGITS);
        if (!b[0]) return;
        if (!show_bulletin(atoi(b))) sio_printf("@X0CThere is no bulletin %s.@X07\n", b);
        sio_pause();
    }
}

/* ------------------------------------------------------------ who's online */
void whos_online(void)
{
    sio_cls();
    sio_puts("\n@X1F  WHO'S ONLINE  @X07\n\n");
    sio_puts("@X0FNode  User                      Location                  Doing@X07\n");
    sio_puts("@X08\xC4\xC4\xC4\xC4  \xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4  \xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4  \xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4@X07\n");
    sio_printf("@X0E %2d@X07   %-25.25s %-25.25s Who's online\n", cfg.node, user.name, user.city);
    sio_puts("@X0E  2@X07   @X08(down - the sysop borrowed the modem for the Amiga)@X07\n");
    sio_printf("\n@X07One line, one caller at a time, run from a spare bedroom. You are on at %ld bps.\n", sio_remote ? session_baud : cfg.baud);
    sio_pause();
}

/* ------------------------------------------------------------ userlog */
void userlog(void)
{
    struct user u;
    int n = user_count();
    sio_cls();
    sio_printf("\n@X1F  USERLOG - %d users  @X07\n\n", n);
    sio_puts("@X0FName                      From                      Last on         Calls@X07\n");
    sio_linecount = 4;
    for (int i = 0; i < n; i++) {
        if (user_get(i, &u) < 0 || u.deleted) continue;
        sio_printf("@X0E%-25.25s @X07%-25.25s @X0B%-14.14s @X0F%5d@X07\n", u.name, u.city, u.last[0] ? u.last : "never", u.calls);
        if (!sio_line_done()) break;
    }
    sio_pause();
}

/* ------------------------------------------------------------ settings */
void settings(void)
{
    char b[20];
    for (;;) {
        sio_cls();
        sio_puts("\n@X1F  YOUR SETTINGS  @X07\n\n");
        sio_printf("  @X0F[@X0EA@X0F]@X07 ANSI color ........ @X0F%s\n", user.ansi ? "On" : "Off");
        sio_printf("  @X0F[@X0EL@X0F]@X07 Screen lines ...... @X0F%d\n", user.lines);
        sio_printf("  @X0F[@X0EE@X0F]@X07 Expert mode ....... @X0F%s\n", user.expert ? "On (menus hidden)" : "Off");
        sio_printf("  @X0F[@X0EC@X0F]@X07 City .............. @X0F%s\n", user.city);
        sio_printf("  @X0F[@X0EP@X0F]@X07 Password .......... @X0F(hidden)\n");
        sio_printf("\n  @X07Calls: @X0F%d@X07  Messages: @X0F%d@X07  Uploads: @X0F%d@X07  Downloads: @X0F%d@X07 (%luK)\n",
                   user.calls, user.msgs, user.ups, user.downs, (unsigned long)user.downk);
        sio_puts("\n@X0BChange which @X0F[@X0EA L E C P Q@X0F]@X0B: @X0F");
        int k = sio_hotkey("ALECPQ\r");
        if (k == 'Q' || k == '\r') break;
        if (k == 'A') { user.ansi ^= 1; sio_ansi = user.ansi; }
        if (k == 'E') user.expert ^= 1;
        if (k == 'L') { sio_puts("@X0ELines (10-60): @X0F"); sio_getline(b, 2, GL_DIGITS); int v = atoi(b); if (v >= 10 && v <= 60) { user.lines = (uint16_t)v; sio_lines = v; } }
        if (k == 'C') { sio_puts("@X0ECity, State: @X0F"); sio_getline(b, 19, GL_NAME); if (b[0]) snprintf(user.city, sizeof user.city, "%s", b); }
        if (k == 'P') {
            char p1[20], p2[20];
            sio_puts("@X0ENew password: @X0F"); sio_getline(p1, 15, GL_PASSWORD | GL_UPPER);
            sio_puts("@X0EAgain: @X0F"); sio_getline(p2, 15, GL_PASSWORD | GL_UPPER);
            if (strlen(p1) >= 4 && !strcmp(p1, p2)) { snprintf(user.password, sizeof user.password, "%s", p1); sio_puts("@X0APassword changed.@X07\n"); }
            else sio_puts("@X0CNot changed.@X07\n");
            sio_pause();
        }
        user_save(usernum, &user);
    }
    user_save(usernum, &user);
}

/* ------------------------------------------------------------ doors */
static void split_name(const char *name, char *first, char *last)
{
    const char *sp = strchr(name, ' ');
    if (!sp) { strcpy(first, name); strcpy(last, ""); return; }
    snprintf(first, 24, "%.*s", (int)(sp - name), name);
    snprintf(last, 24, "%s", sp + 1);
    strupr_s(first); strupr_s(last);
}

static void write_dropfiles(const char *home)
{
    char p[96], first[24], last[24], sf[24], sl[24], d[12];
    split_name(user.name, first, last);
    split_name(cfg.sysop, sf, sl);
    uint32_t left = sio_minutes_left();
    today_str(d);
    /* DORINFO1.DEF (RBBS-PC / QuickBBS style) */
    snprintf(p, sizeof p, "%s\\DORINFO1.DEF", home);
    FILE *f = fopen(p, "w");
    if (f) {
        fprintf(f, "%s\n%s\n%s\nCOM%d\n%ld BAUD,N,8,1\n0\n%s\n%s\n%s\n%d\n%d\n%lu\n",
                cfg.name, sf, sl, sio_remote ? cfg.port : 0, sio_remote ? session_baud : 0L,
                first, last, user.city, sio_ansi ? 1 : 0, user.sec, (unsigned long)left);
        fclose(f);
    }
    /* DOOR.SYS (the 52-line GAP / WildCat! format) */
    snprintf(p, sizeof p, "%s\\DOOR.SYS", home);
    f = fopen(p, "w");
    if (f) {
        fprintf(f, "COM%d:\n%ld\n8\n%d\n%ld\nY\nN\nY\nY\n", sio_remote ? cfg.port : 0, sio_remote ? session_baud : 0L, cfg.node,
                sio_remote ? session_baud : 0L);
        fprintf(f, "%s\n%s\n%s\n%s\n%s\n%d\n%d\n%s\n%lu\n%lu\n%s\n%d\nY\n1,2,3\n1\n12-31-99\n%d\nZ\n%d\n%d\n0\n9999\n%s\n",
                user.name, user.city, user.phone, user.phone, user.password, user.sec, user.calls, user.last,
                (unsigned long)left * 60, (unsigned long)left, sio_ansi ? "GR" : "NG", sio_lines, usernum + 1,
                user.ups, user.downs, user.birth);
        fprintf(f, "%s\\USERS.DAT\n%s\\\n%s\n%s\n00:00\nY\n%s\nN\n7\n0\n%s\n00:00\n00:00\n9999\n0\n%lu\n%lu\n%s\n0\n%d\n",
                home, home, cfg.sysop, user.name, sio_ansi ? "Y" : "N", d, (unsigned long)user.upk, (unsigned long)user.downk,
                user.computer, user.msgs);
        fclose(f);
    }
}

void door_menu(void)
{
    char b[8], home[80], exe[120], dir[100];
    if (!ndoors) { sio_puts("@X0CNo doors are open tonight.@X07\n"); return; }
    for (;;) {
        sio_cls();
        if (!sio_showfile(sio_ansi ? "DISPLAY\\DOORS.ANS" : "DISPLAY\\DOORS.TXT")) {
            sio_puts("\n@X1F  DOORS - ONLINE GAMES  @X07\n\n");
            for (int i = 0; i < ndoors; i++)
                sio_printf("  @X0F[@X0E%d@X0F] @X0F%-26s @X03%s@X07\n", doors[i].num, doors[i].name, doors[i].desc);
            sio_puts("\n  @X08Scores and headlines from every door: bulletin 9.@X07\n");
        }
        sio_puts("\n@X0EDoor # to open (Enter = quit): @X0F");
        sio_getline(b, 2, GL_DIGITS);
        if (!b[0]) return;
        int d = -1;
        for (int i = 0; i < ndoors; i++) if (atoi(b) == doors[i].num) d = i;
        if (d < 0) continue;
        sio_printf("\n@X0AOpening door: @X0F%s@X0A.  Please wait...@X07\n", doors[d].name);
        sio_flush();
        getcwd(home, sizeof home);
        write_dropfiles(home);
        snprintf(dir, sizeof dir, "%s\\%s", home, doors[d].path);
        snprintf(exe, sizeof exe, "%s\\%s", dir, doors[d].extra);
        sysop_log("%s opened door %s", user.name, doors[d].name);
        /* hand the port to the door: keep DTR up (don't hang up!) */
        com_close(1);
        chdir(dir);
        int rc = spawnl(P_WAIT, exe, doors[d].extra, home, NULL);
        chdir(home);
        com_open(cfg.port, cfg.baud);
        scr_init();
        scr_cursor_on(1);
        vt_reset(&sio_vt);
        local_status();
        if (rc < 0) sio_printf("@X0CThe door is stuck (%s can't be run).@X07\n", exe);
        sio_printf("\n@X0BWelcome back to %s!@X07\n", cfg.name);
        if (sio_remote && !com_carrier()) longjmp(sio_drop, SIO_CARRIER);
        sio_pause();
    }
}

/* ------------------------------------------------------------ welcome back */
/* A note from the sysop on repeat calls, and what's new since the last one. */
void sysop_note(void)
{
    static const char *const notes[] = {
        "Good to see you again, %s.  Parity says hi.  (She's asleep on the modem.)",
        "%s!  The coffee's on.  Well, mine is.",
        "Welcome back, %s.  The board clock is only two seconds off today.  Progress!",
        "Hi %s.  If your screen hiccups around 2:10 AM, that's the freight train, not you.",
        "%s, you're back!  The Flame Pit has been busy.  Bring a fire extinguisher.",
        "Hello again, %s.  Clear skies tonight; if I'm slow to answer, I'm on the roof.",
        "%s!  Node 2 is still down.  The Amiga still has the modem.  Some things never change.",
        "Welcome back, %s.  Have you tried all four doors yet?  Parity likes the poker one.",
        "%s, good timing.  I just finished answering comments.  Leave me another one.",
        "Hey %s.  Reminder: SFARM meets the second Tuesday at Sal's Pizza.  Bring five dollars.",
        "%s!  The Byte Bandit is still on probation, in case you were wondering.",
        "Welcome back, %s.  Somebody left a good one in Programmers' Corner.  Go look.",
    };
    static const int nnotes = sizeof notes / sizeof notes[0];
    char first[24], b[96];
    const char *sp = strchr(user.name, ' ');
    snprintf(first, sizeof first, "%.*s", sp ? (int)(sp - user.name) : 20, user.name);
    if (user.calls == 10 || user.calls == 25 || user.calls == 50 || user.calls == 100 || user.calls == 1000)
        snprintf(b, sizeof b, "Call number %d, %s!  That calls for a celebration.  Parity will allow one (1) cheer.", user.calls, first);
    else if (!strcasecmp(user.name, "Guest"))
        snprintf(b, sizeof b, "Hello again, mystery guest.  One of these days you'll tell me your name.");
    else
        snprintf(b, sizeof b, notes[(user.calls + (unsigned)TICKS()) % nnotes], first);
    char sy[24];
    const char *ss = strchr(cfg.sysop, ' ');
    snprintf(sy, sizeof sy, "%.*s", ss ? (int)(ss - cfg.sysop) : 20, cfg.sysop);
    char q[110];
    snprintf(q, sizeof q, "\"%s\"", b);
    sio_printf("  @X0DA note from %s, your sysop:@X07\n", sy);
    wrap_out("@X0F", 2, 3, 76, q);
    sio_puts("\n");
    /* new messages since the last call */
    int total = 0, shown = 0;
    for (int i = 0; i < nmsgareas; i++) {
        int c = msg_count(i), nw = c - user.lastread[i];
        if (nw <= 0) continue;
        if (!total) sio_puts("  @X0BNew since your last call:@X07\n");
        total += nw;
        if (shown < 4) { sio_printf("    @X0E%3d@X07 in %s\n", nw, msgareas[i].name); shown++; }
    }
    if (total) {
        int more = 0;
        for (int i = 0, k = 0; i < nmsgareas; i++) if (msg_count(i) > user.lastread[i] && ++k > 4) more++;
        if (more) sio_printf("    @X08and new messages in %d more area%s@X07\n", more, more == 1 ? "" : "s");
        sio_puts("\n");
    } else sio_puts("  @X0BYou're all caught up on messages.@X07\n\n");
}
