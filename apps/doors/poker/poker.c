/* poker.c - PIT POKER, a Jacks-or-Better video poker door for The ARM Pit.
 *
 * Five cards, hold any of them with 1-5, draw once, get paid from the 9/6
 * Jacks-or-Better pay table (the full-pay machine every casino hid in a
 * corner). The dealer is Parity, the sysop's cat.
 *
 * Bankroll: every new day a player is topped up to 100 credits (anything
 * above that is kept: winnings carry over). Go broke and the table is closed
 * to you until tomorrow. The leaderboard ranks the biggest bankroll ever held.
 *
 * Files in the door's directory:
 *   POKER.DAT   players, one per line:
 *               name|credits|day|peak|peakdate|besthand|besthanddate|hands|royals
 *   SCORES.TXT  the top five, rewritten when it changes (the BBS shows it)
 *   NEWS.TXT    headlines (door_news): royal flushes, straight flushes, fours, big days
 *
 * Launched by the BBS as PITPOKER.EXE <dir with DOOR.SYS>; PITPOKER /L plays locally.
 * Original program for ARM-DOS.
 */
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include "../lib/door.h"

#define DAILY_STAKE 100
#define MAXPLAYERS  100

struct player {
    char name[36];
    long credits, peak;
    int day;
    char peakdate[10];
    int besthand;
    char besthanddate[10];
    long hands;
    int royals;
};
static struct player all[MAXPLAYERS];
static int nplayers, me = -1;

enum { NOTHING, JACKS, TWOPAIR, TRIPS, STRAIGHT, FLUSH, FULLHOUSE, QUADS, STFLUSH, ROYAL };
static const char *const handname[] = { "Nothing", "Jacks or Better", "Two Pair", "Three of a Kind", "Straight",
    "Flush", "Full House", "Four of a Kind", "Straight Flush", "Royal Flush" };
static const int pays[] = { 0, 1, 2, 3, 4, 6, 9, 25, 50, 250 };      /* per credit; royal x5 = 4000 */

static int deck[52], dpos;
static int hand[5], held[5];
static int bet = 1;
static long session_start;

/* ------------------------------------------------------------ players */
static void commas(char *b, long v)
{
    char t[20]; int n = sprintf(t, "%ld", v), o = 0;
    for (int i = 0; i < n; i++) { if (i && (n - i) % 3 == 0 && t[i - 1] != '-') b[o++] = ','; b[o++] = t[i]; }
    b[o] = 0;
}

static void load_players(void)
{
    char line[160];
    nplayers = 0;
    FILE *f = fopen("POKER.DAT", "r");
    if (!f) return;
    while (nplayers < MAXPLAYERS && fgets(line, sizeof line, f)) {
        char *fld[9] = { 0 }; int n = 0; char *s = line;
        char *nl = strpbrk(line, "\r\n"); if (nl) *nl = 0;
        if (!line[0] || line[0] == ';') continue;
        while (n < 9) { fld[n++] = s; s = strchr(s, '|'); if (!s) break; *s++ = 0; }
        if (n < 9) continue;
        struct player *p = &all[nplayers++];
        memset(p, 0, sizeof *p);
        snprintf(p->name, sizeof p->name, "%s", fld[0]);
        p->credits = atol(fld[1]); p->day = atoi(fld[2]); p->peak = atol(fld[3]);
        snprintf(p->peakdate, sizeof p->peakdate, "%s", fld[4]);
        p->besthand = atoi(fld[5]);
        if (p->besthand < 0 || p->besthand > ROYAL) p->besthand = 0;
        snprintf(p->besthanddate, sizeof p->besthanddate, "%s", fld[6]);
        p->hands = atol(fld[7]); p->royals = atoi(fld[8]);
    }
    fclose(f);
}

static int by_peak(const void *a, const void *b)
{
    const struct player *x = *(const struct player *const *)a, *y = *(const struct player *const *)b;
    return y->peak > x->peak ? 1 : y->peak < x->peak ? -1 : 0;
}

static int sorted(const struct player **out)
{
    int n = 0;
    for (int i = 0; i < nplayers; i++) if (all[i].hands > 0) out[n++] = &all[i];
    qsort(out, n, sizeof out[0], by_peak);
    return n;
}

static char last_scores[600];

static void save_players(void)
{
    FILE *f = fopen("POKER.DAT", "w");
    if (f) {
        fprintf(f, "; Pit Poker players: name|credits|day|peak|peakdate|besthand|besthanddate|hands|royals\n");
        for (int i = 0; i < nplayers; i++) {
            const struct player *p = &all[i];
            fprintf(f, "%s|%ld|%d|%ld|%s|%d|%s|%ld|%d\n", p->name, p->credits, p->day, p->peak, p->peakdate,
                    p->besthand, p->besthanddate, p->hands, p->royals);
        }
        fclose(f);
    }
    /* SCORES.TXT: title + top five by the biggest bankroll ever; only when it changed */
    static const struct player *top[MAXPLAYERS];
    char b[600]; int o = 0;
    int n = sorted(top);
    o += snprintf(b + o, sizeof b - o, "Pit Poker - biggest bankrolls\n");
    for (int i = 0; i < n && i < 5 && o < (int)sizeof b - 80; i++) {
        char c[16]; commas(c, top[i]->peak);
        o += snprintf(b + o, sizeof b - o, "%2d. %-22.22s %7s  best hand: %s\n", i + 1, top[i]->name, c, handname[top[i]->besthand]);
    }
    if (!strcmp(b, last_scores)) return;
    snprintf(last_scores, sizeof last_scores, "%s", b);
    f = fopen("SCORES.TXT", "w");
    if (f) { fputs(b, f); fclose(f); }
}

static void find_me(void)
{
    for (int i = 0; i < nplayers; i++) if (!strcasecmp(all[i].name, door_user)) { me = i; return; }
    if (nplayers >= MAXPLAYERS) nplayers = MAXPLAYERS - 1;
    me = nplayers++;
    memset(&all[me], 0, sizeof all[me]);
    snprintf(all[me].name, sizeof all[me].name, "%s", door_user);
    all[me].day = -1;
}

/* ------------------------------------------------------------ cards */
static const char ranks[] = "23456789TJQKA";
static int rank(int c) { return c % 13; }          /* 0 = deuce .. 12 = ace */
static int suit(int c) { return c / 13; }          /* 0 hearts 1 diamonds 2 clubs 3 spades */
static const char suitch[] = { 3, 4, 5, 6 };

static void shuffle(void)
{
    for (int i = 0; i < 52; i++) deck[i] = i;
    for (int i = 51; i > 0; i--) { int j = door_rnd(i + 1), t = deck[i]; deck[i] = deck[j]; deck[j] = t; }
    dpos = 0;
}

static int evaluate(const int *h)
{
    int cnt[13] = { 0 }, fl = 1, pairs = 0, trips = 0, quads = 0, hipair = 0;
    for (int i = 0; i < 5; i++) { cnt[rank(h[i])]++; if (suit(h[i]) != suit(h[0])) fl = 0; }
    for (int r = 0; r < 13; r++) {
        if (cnt[r] == 2) { pairs++; if (r >= 9) hipair = 1; }
        if (cnt[r] == 3) trips++;
        if (cnt[r] == 4) quads++;
    }
    int st = 0, top = -1;
    if (!pairs && !trips && !quads) {
        int lo = 13, hi = -1;
        for (int r = 0; r < 13; r++) if (cnt[r]) { if (r < lo) lo = r; if (r > hi) hi = r; }
        if (hi - lo == 4) { st = 1; top = hi; }
        else if (cnt[12] && cnt[0] && cnt[1] && cnt[2] && cnt[3]) { st = 1; top = 3; }   /* A-2-3-4-5 */
    }
    if (st && fl) return top == 12 ? ROYAL : STFLUSH;
    if (quads) return QUADS;
    if (trips && pairs) return FULLHOUSE;
    if (fl) return FLUSH;
    if (st) return STRAIGHT;
    if (trips) return TRIPS;
    if (pairs == 2) return TWOPAIR;
    if (pairs == 1 && hipair) return JACKS;
    return NOTHING;
}

static long payout(int h, int b) { return h == ROYAL && b == 5 ? 4000 : (long)pays[h] * b; }

/* ------------------------------------------------------------ screen */
static void at(int row, int col)
{
    if (sio_ansi) sio_printf("\033[%d;%dH", row, col);
}

static void bar(void)
{
    char a[96], b[96], c[16];
    const struct player *p = me >= 0 ? &all[me] : NULL;
    commas(c, p ? p->credits : 0);
    snprintf(a, sizeof a, " PIT POKER   %-24.24s   Credits %-9s  Bet %d", door_user, c, bet);
    snprintf(b, sizeof b, " Jacks or Better 9/6  -  dealer: Parity the cat               Time left %lu min",
             (unsigned long)sio_minutes_left());
    door_bar(0x2F, a, b);
}

static void border(int l, int m, int r)
{
    char b[80]; int o = 0;
    b[o++] = (char)l;
    for (int i = 0; i < 18; i++) b[o++] = (char)0xC4;
    for (int c = 0; c < 5; c++) { b[o++] = (char)m; for (int i = 0; i < 6; i++) b[o++] = (char)0xC4; }
    b[o++] = (char)r; b[o] = 0;
    sio_printf("@X02  %s@X07\n", b);
}

static void paytable(int highlight)
{
    at(2, 1);
    border(0xDA, 0xC2, 0xBF);
    for (int h = ROYAL; h >= JACKS; h--) {
        sio_printf("@X02  \xB3%s %-17s@X02\xB3", h == highlight ? "@X2F" : "@X0A", handname[h]);
        for (int b = 1; b <= 5; b++)
            sio_printf("%s%5ld @X02\xB3", h == highlight && b == bet ? "@X2F" : b == bet ? "@X0E" : "@X06", payout(h, b));
        sio_puts("@X07\n");
    }
    border(0xC0, 0xC1, 0xD9);
}

static void card_row(int line, int c)
{
    char r[3] = { ranks[rank(c)], 0, 0 };
    if (r[0] == 'T') { r[0] = '1'; r[1] = '0'; }
    const char *col = suit(c) < 2 ? "@X74" : "@X70";
    switch (line) {
    case 0: sio_puts("@X70\xDA\xC4\xC4\xC4\xC4\xC4\xBF"); break;
    case 1: sio_printf("@X70\xB3%s%-2s   @X70\xB3", col, r); break;
    case 2: sio_printf("@X70\xB3%s  %c  @X70\xB3", col, suitch[suit(c)]); break;
    case 3: sio_printf("@X70\xB3%s   %2s@X70\xB3", col, r); break;
    case 4: sio_puts("@X70\xC0\xC4\xC4\xC4\xC4\xC4\xD9"); break;
    }
}

static void cards(void)
{
    at(13, 1);
    for (int line = 0; line < 5; line++) {
        sio_puts("        ");
        for (int i = 0; i < 5; i++) { card_row(line, hand[i]); sio_puts("@X07   "); }
        sio_puts("\n");
    }
    sio_puts("        ");
    for (int i = 0; i < 5; i++) sio_printf("  @X02[@X0A%d@X02]@X07    ", i + 1);
    sio_puts("\n");
}

static void holds(void)
{
    if (sio_ansi) {
        at(19, 1);
        sio_puts("        ");
        for (int i = 0; i < 5; i++) sio_puts(held[i] ? "@X1E  HELD @X07   " : "@X07         ");
        sio_puts("@X07\n");
    } else {
        sio_puts("Holding:");
        int any = 0;
        for (int i = 0; i < 5; i++) if (held[i]) { sio_printf(" %d", i + 1); any = 1; }
        sio_puts(any ? "\n" : " nothing\n");
    }
}

static void header(void)
{
    char c[16]; commas(c, all[me].credits);
    at(1, 1);
    sio_printf("@X0A  P I T   P O K E R@X02   \xFA  @X07Jacks or Better  @X02\xFA  @X07Credits @X0E%-9s @X07Bet @X0E%d@X07\033[K\n", c, bet);
}

static void table(int highlight)
{
    sio_cls();
    header();
    paytable(highlight);
    cards();
    holds();
}

static void message(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void message(const char *fmt, ...)
{
    char b[200];
    va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    if (sio_ansi) { at(20, 1); sio_puts("\033[K"); }
    sio_puts(b);
    sio_puts("@X07\n");
    if (sio_ansi) sio_puts("\033[K");
}

static const char *const purrs[] = {
    "Parity yawns and pushes the cards toward you with one paw.",
    "Parity flicks her tail.  The deck is shuffled.  Probably.",
    "Parity is sitting on the warm modem.  She deals from there.",
    "Parity watches your chips the way she watches the goldfish.",
    "Somewhere a freight train rumbles by.  Parity doesn't blink.",
    "Parity licks a paw and deals.  House rules: no peeking.",
};
static const char *const meows[] = {
    "Parity purrs.", "Parity looks mildly impressed.", "Parity's ears go up.", "Parity slow-blinks at you.",
};

/* ------------------------------------------------------------ play */
static void scores(void)
{
    static const struct player *top[MAXPLAYERS];
    int n = sorted(top);
    sio_cls();
    sio_puts("\n@X0A  HIGH ROLLERS OF THE PIT@X07   @X02(biggest bankroll ever)@X07\n\n");
    sio_puts("@X0F   Name                      Peak     Date      Best hand         Hands  Royals@X07\n");
    sio_linecount = 4;
    for (int i = 0; i < n && i < 15; i++) {
        char c[16]; commas(c, top[i]->peak);
        sio_printf("%s%2d@X07 %-24.24s %6s   %-8s  %-16s %6ld  %5d\n", i == 0 ? "@X0E" : "@X0A", i + 1, top[i]->name, c,
                   top[i]->peakdate, handname[top[i]->besthand], top[i]->hands, top[i]->royals);
        if (!sio_line_done()) break;
    }
    if (!n) sio_puts("@X02  Nobody has sat down at the table yet.@X07\n");
    sio_puts("\n");
    sio_pause();
}

/* one hand; the bet is already chosen */
static void play_hand(void)
{
    struct player *p = &all[me];
    p->credits -= bet;
    p->hands++;
    shuffle();
    for (int i = 0; i < 5; i++) { hand[i] = deck[dpos++]; held[i] = 0; }
    bar();
    table(-1);
    int pre = evaluate(hand);
    if (pre > NOTHING) message("@X0A  You're holding @X0F%s@X0A.", handname[pre]);
    else message("@X02  %s", purrs[door_rnd(6)]);
    for (;;) {
        if (sio_ansi) at(21, 1);
        sio_puts("@X02Hold @X0F[@X0A1-5@X0F]@X02, @X0F[@X0AEnter@X0F]@X02 to draw: @X07\033[K");
        int k = sio_hotkey("12345D\r");
        if (k == '\r' || k == 'D') break;
        held[k - '1'] ^= 1;
        holds();
    }
    for (int i = 0; i < 5; i++) if (!held[i]) hand[i] = deck[dpos++];
    int h = evaluate(hand);
    long win = payout(h, bet);
    p->credits += win;
    char d[12]; door_today(d);
    if (h > p->besthand) { p->besthand = h; snprintf(p->besthanddate, sizeof p->besthanddate, "%s", d); }
    if (p->credits > p->peak) { p->peak = p->credits; snprintf(p->peakdate, sizeof p->peakdate, "%s", d); }
    if (h == ROYAL) p->royals++;
    char c[16]; commas(c, win);
    if (h == ROYAL) door_news("%s hit a ROYAL FLUSH at Pit Poker and won %s credits!  Parity fell off the modem.", p->name, c);
    else if (h == STFLUSH) door_news("%s drew a straight flush at Pit Poker (%s credits).", p->name, c);
    else if (h == QUADS) door_news("%s made four of a kind at Pit Poker (%s credits).", p->name, c);
    save_players();
    bar();
    table(h > NOTHING ? h : -1);
    if (win) message("@X0E  %s!  @X0AYou win @X0F%s@X0A credit%s.  @X02%s", handname[h], c, win == 1 ? "" : "s", meows[door_rnd(4)]);
    else message("@X02  No win this time.  Parity pretends not to notice.");
}

static void leave(void)
{
    struct player *p = &all[me];
    long gain = p->credits - session_start;
    if (gain >= 200 && p->credits >= 300) {
        char c[16], g[16]; commas(c, p->credits); commas(g, gain);
        door_news("%s left the Pit Poker table up %s credits (%s in the bank).", p->name, g, c);
    }
    save_players();
}

static void game(void)
{
    struct player *p = &all[me];
    bar();
    table(-1);
    message("@X02  Parity is dealing tonight.  She takes her job very seriously.");
    for (;;) {
        char c[16]; commas(c, p->credits);
        if (p->credits <= 0) {
            if (sio_ansi) at(21, 1);
            sio_puts("@X0C  You're broke!  @X02Parity sweeps your last chip off the table.  Come back tomorrow.@X07\n");
            sio_pause();
            return;
        }
        if (bet > p->credits) bet = (int)p->credits;
        if (sio_ansi) at(21, 1);
        sio_printf("@X0F[@X0AEnter@X0F]@X02 deal (bet %d)  @X0F[@X0A1-5@X0F]@X02 bet & deal  @X0F[@X0AS@X0F]@X02cores  @X0F[@X0AQ@X0F]@X02uit: @X07\033[K", bet);
        int k = sio_hotkey("12345SQ\r");
        if (k == 'Q') return;
        if (k == 'S') { scores(); bar(); table(-1); message("@X02  %s", purrs[door_rnd(6)]); continue; }
        if (k >= '1' && k <= '5') {
            if (k - '0' > p->credits) { message("@X0C  You only have %s credit%s.", c, p->credits == 1 ? "" : "s"); continue; }
            bet = k - '0';
        }
        play_hand();
    }
}

int main(int argc, char **argv)
{
    door_init(argc, argv, bar);
    load_players();
    if (setjmp(sio_drop) == 0) {
        find_me();
        struct player *p = &all[me];
        int today = door_daynum(), topped = 0, isnew = p->day < 0;
        if (p->day != today) {
            p->day = today;
            if (p->credits < DAILY_STAKE) { p->credits = DAILY_STAKE; topped = 1; }
            if (p->credits > p->peak) { p->peak = p->credits; door_today(p->peakdate); }
        }
        session_start = p->credits;
        bet = p->credits >= 5 ? 5 : 1;
        sio_cls();
        sio_puts("\n@X0A   \xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\n");
        sio_puts("@X0A   \xDB@X2F  P I T   P O K E R  @X0A\xDB@X2E  Jacks or Better, dealt by Parity  @X0A\xDB@X07\n");
        sio_puts("@X0A   \xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF@X07\n\n");
        sio_puts("@X07   A card table in the corner of the sysop's spare room, lit by the glow of\n");
        sio_puts("   the modem lights.  A grey cat sits on the far side, very still, watching you.\n\n");
        char c[16]; commas(c, p->credits);
        if (isnew) sio_printf("@X0A   A new player!  Parity slides @X0F%d@X0A credits across the felt to you.@X07\n", DAILY_STAKE);
        else if (topped) sio_printf("@X0A   A new day: Parity tops your stack back up to @X0F%d@X0A credits.@X07\n", DAILY_STAKE);
        else sio_printf("@X0A   Welcome back.  You have @X0F%s@X0A credits.@X07\n", c);
        sio_puts("@X02   Every day you get 100 credits if you have less.  Winnings are yours to keep.@X07\n\n");
        save_players();
        sio_pause();
        game();
        leave();
        sio_cls();
        commas(c, p->credits);
        sio_printf("\n@X0AYou cash out with @X0F%s@X0A credits.  Parity is already asleep on the modem.@X07\n", c);
    } else {
        if (me >= 0) save_players();
    }
    door_exit();
    return 0;
}
