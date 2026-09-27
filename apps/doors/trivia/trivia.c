/* trivia.c - BYTE-SIZED TRIVIA, a quiz door for The ARM Pit BBS.
 *
 * Ten multiple-choice questions a game about the computers, modems, games
 * and programming languages of the 1970s and 80s (plus a little "ARM Pit
 * trivia"), 100 points a right answer, up to 50 more for a fast one, a
 * streak bonus, three games a day per caller, and a hall of fame.
 *
 * Files in the door's directory:
 *   QUESTION.TXT  the questions (the sysop may add more, see README.md)
 *   TRIVIA.DAT     players, one per line: name|best|total|games|perfect|bestdate|day|today
 *   SCORES.TXT     the top five, rewritten after every game (the BBS shows it)
 *   NEWS.TXT       headlines (door_news)
 *
 * Launched by the BBS as TRIVIA.EXE <dir with DOOR.SYS>; TRIVIA /L plays locally.
 * Original program for ARM-DOS.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include "../lib/door.h"

#define GAMES_PER_DAY  3
#define PER_GAME       10
#define MAXQ           300
#define MAXPLAYERS     100
#define ANSWER_SECS    30

struct player {
    char name[36];
    long best, total;
    int games, perfect;
    char bestdate[10];
    int day, today;
};

static struct player all[MAXPLAYERS];
static int nplayers, me = -1;
static long qoffs[MAXQ];
static int nq;

/* the question being asked */
static char qtext[400];
static char choice[4][72];
static int correct;
static char fact[200];

/* ------------------------------------------------------------ players */
static void load_players(void)
{
    char line[160];
    nplayers = 0;
    FILE *f = fopen("TRIVIA.DAT", "r");
    if (!f) return;
    while (nplayers < MAXPLAYERS && fgets(line, sizeof line, f)) {
        char *fld[8] = { 0 }; int n = 0; char *s = line;
        char *nl = strpbrk(line, "\r\n"); if (nl) *nl = 0;
        if (!line[0] || line[0] == ';') continue;
        while (n < 8) { fld[n++] = s; s = strchr(s, '|'); if (!s) break; *s++ = 0; }
        if (n < 8) continue;
        struct player *p = &all[nplayers++];
        memset(p, 0, sizeof *p);
        snprintf(p->name, sizeof p->name, "%s", fld[0]);
        p->best = atol(fld[1]); p->total = atol(fld[2]);
        p->games = atoi(fld[3]); p->perfect = atoi(fld[4]);
        snprintf(p->bestdate, sizeof p->bestdate, "%s", fld[5]);
        p->day = atoi(fld[6]); p->today = atoi(fld[7]);
    }
    fclose(f);
}

static void commas(char *b, long v)
{
    char t[20]; int n = sprintf(t, "%ld", v), o = 0;
    for (int i = 0; i < n; i++) { if (i && (n - i) % 3 == 0 && t[i - 1] != '-') b[o++] = ','; b[o++] = t[i]; }
    b[o] = 0;
}

static int by_best(const void *a, const void *b)
{
    const struct player *x = *(const struct player *const *)a, *y = *(const struct player *const *)b;
    return y->best > x->best ? 1 : y->best < x->best ? -1 : 0;
}

static int sorted(const struct player **out)
{
    int n = 0;
    for (int i = 0; i < nplayers; i++) if (all[i].games > 0) out[n++] = &all[i];
    qsort(out, n, sizeof out[0], by_best);
    return n;
}

static void save_players(void)
{
    FILE *f = fopen("TRIVIA.DAT", "w");
    if (f) {
        fprintf(f, "; Byte-Sized Trivia players: name|best|total|games|perfect|bestdate|day|today\n");
        for (int i = 0; i < nplayers; i++) {
            const struct player *p = &all[i];
            fprintf(f, "%s|%ld|%ld|%d|%d|%s|%d|%d\n", p->name, p->best, p->total, p->games, p->perfect, p->bestdate, p->day, p->today);
        }
        fclose(f);
    }
    /* SCORES.TXT for the BBS's news bulletin: a title and the top five */
    static const struct player *top[MAXPLAYERS];
    int n = sorted(top);
    f = fopen("SCORES.TXT", "w");
    if (!f) return;
    fprintf(f, "Byte-Sized Trivia - best games\n");
    for (int i = 0; i < n && i < 5; i++) {
        char c[16]; commas(c, top[i]->best);
        fprintf(f, "%2d. %-24.24s %6s  (%s)\n", i + 1, top[i]->name, c, top[i]->bestdate);
    }
    const struct player *most = NULL;
    for (int i = 0; i < nplayers; i++) if (!most || all[i].total > most->total) most = &all[i];
    if (most && most->total > 0) {
        char c[16]; commas(c, most->total);
        fprintf(f, "Most points all-time: %s, %s in %d games\n", most->name, c, most->games);
    }
    fclose(f);
}

static void find_me(void)
{
    for (int i = 0; i < nplayers; i++) if (!strcasecmp(all[i].name, door_user)) { me = i; return; }
    if (nplayers >= MAXPLAYERS) nplayers = MAXPLAYERS - 1;     /* the board is full: reuse the last slot */
    me = nplayers++;
    memset(&all[me], 0, sizeof all[me]);
    snprintf(all[me].name, sizeof all[me].name, "%s", door_user);
}

/* ------------------------------------------------------------ questions */
/* Robust indexing: a question is a run of "Q " lines; index the first of each run. */
static void index_q(void)
{
    char line[200];
    long pos = 0;
    int inq = 0;
    nq = 0;
    FILE *f = fopen("QUESTION.TXT", "rb");
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        int isq = line[0] == 'Q' && line[1] == ' ';
        if (isq && !inq && nq < MAXQ) qoffs[nq++] = pos;
        inq = isq;
        pos = ftell(f);
    }
    fclose(f);
}

static int load_q(int i)
{
    char line[200];
    int nch = 0, cor = -1;
    qtext[0] = 0; fact[0] = 0;
    FILE *f = fopen("QUESTION.TXT", "rb");
    if (!f) return -1;
    fseek(f, qoffs[i], SEEK_SET);
    while (fgets(line, sizeof line, f)) {
        char *nl = strpbrk(line, "\r\n"); if (nl) *nl = 0;
        if (line[0] == 'Q' && line[1] == ' ') {
            if (nch) break;                          /* the next question */
            if (qtext[0]) strncat(qtext, " ", sizeof qtext - strlen(qtext) - 1);
            strncat(qtext, line + 2, sizeof qtext - strlen(qtext) - 1);
        } else if ((line[0] == '*' || line[0] == '-') && line[1] == ' ' && nch < 4) {
            if (line[0] == '*') cor = nch;
            snprintf(choice[nch++], sizeof choice[0], "%s", line + 2);
        } else if (line[0] == '!' && line[1] == ' ') {
            if (fact[0]) strncat(fact, " ", sizeof fact - strlen(fact) - 1);
            strncat(fact, line + 2, sizeof fact - strlen(fact) - 1);
        } else if (!line[0] && nch == 4) break;
    }
    fclose(f);
    if (nch != 4 || cor < 0) return -1;
    /* shuffle the choices */
    for (int k = 3; k > 0; k--) {
        int j = door_rnd(k + 1);
        char t[72];
        memcpy(t, choice[k], sizeof t); memcpy(choice[k], choice[j], sizeof t); memcpy(choice[j], t, sizeof t);
        if (cor == k) cor = j; else if (cor == j) cor = k;
    }
    correct = cor;
    return 0;
}

/* print text word-wrapped at width, each line indented */
static void wrap(const char *s, int width, const char *indent, const char *color)
{
    char line[100];
    while (*s) {
        while (*s == ' ') s++;
        int n = (int)strlen(s);
        if (n > width) {
            n = width;
            while (n > 0 && s[n] != ' ') n--;
            if (n == 0) n = width;
        }
        snprintf(line, sizeof line, "%.*s", n, s);
        sio_printf("%s%s%s\n", color, indent, line);
        s += n;
    }
}

/* ------------------------------------------------------------ screens */
static long game_score;
static int game_q;

static void bar(void)
{
    char a[96], b[96];
    const struct player *p = me >= 0 ? &all[me] : NULL;
    snprintf(a, sizeof a, " BYTE-SIZED TRIVIA   %-24.24s  Best %-6ld  Games today %d of %d",
             door_user, p ? p->best : 0L, p ? p->today : 0, GAMES_PER_DAY);
    if (game_q) snprintf(b, sizeof b, " Question %d of %d   Score %-6ld                   Time left %lu min",
                         game_q, PER_GAME, game_score, (unsigned long)sio_minutes_left());
    else snprintf(b, sizeof b, " Total points %-8ld                               Time left %lu min",
                  p ? p->total : 0L, (unsigned long)sio_minutes_left());
    door_bar(0x5F, a, b);
}

static void title(void)
{
    sio_cls();
    sio_puts("\n@X0D   \xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\xDC\n");
    sio_puts("@X05   \xDB@X5F  B Y T E - S I Z E D    T R I V I A  @X05\xDB@X5E  1970s & 80s computing quiz @X05\xDB@X07\n");
    sio_puts("@X05   \xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF@X07\n\n");
}

static void rules(void)
{
    sio_cls();
    sio_puts("\n@X0D  HOW TO PLAY@X07\n\n");
    sio_printf("@X07  Each game is @X0F%d questions@X07.  Press @X0FA@X07, @X0FB@X07, @X0FC@X07 or @X0FD@X07 to answer.\n\n", PER_GAME);
    sio_puts("  @X0E100@X07 points for every right answer, plus a @X0Espeed bonus@X07 of up to @X0E50@X07:\n");
    sio_puts("  full marks inside 3 seconds, nothing after 20.  Line noise is not an excuse,\n");
    sio_puts("  but it is a very good story.\n\n");
    sio_puts("  Three right in a row starts a @X0Estreak@X07: @X0E+25@X07 for every right answer after that.\n");
    sio_puts("  A perfect game (all ten) earns @X0E250@X07 more.\n\n");
    sio_printf("  You have @X0F%d seconds@X07 a question and @X0F%d games@X07 a day.\n\n", ANSWER_SECS, GAMES_PER_DAY);
    sio_puts("  The sysop adds new questions now and then.  Questions marked @X0B[ARM Pit]@X07\n");
    sio_puts("  are about this board: read the bulletins, it pays.\n\n");
    sio_pause();
}

static void hall(void)
{
    static const struct player *top[MAXPLAYERS];
    int n = sorted(top);
    sio_cls();
    sio_puts("\n@X0D  THE HALL OF FAME@X07   @X05(best single game)@X07\n\n");
    sio_puts("@X0F   Name                        Best game   Date      Games  Perfect  Total@X07\n");
    sio_linecount = 4;
    for (int i = 0; i < n && i < 15; i++) {
        char b[16], t[16]; commas(b, top[i]->best); commas(t, top[i]->total);
        sio_printf("%s%2d@X07 %-27.27s %9s   %-8s  %5d  %7d  %s\n", i == 0 ? "@X0E" : "@X0D", i + 1, top[i]->name, b,
                   top[i]->bestdate, top[i]->games, top[i]->perfect, t);
        if (!sio_line_done()) break;
    }
    if (!n) sio_puts("@X05  Nobody has played yet.  Be the first!@X07\n");
    sio_puts("\n");
    sio_pause();
}

static const char *const cheers[] = { "Correct!", "Right!", "That's it!", "Spot on!", "Bingo!", "You got it!" };
static const char *const groans[] = { "Sorry.", "Nope.", "Not quite.", "Bzzzt.", "Alas, no." };

/* one game; returns the score */
static long play(void)
{
    int order[MAXQ];
    for (int i = 0; i < nq; i++) order[i] = i;
    for (int i = nq - 1; i > 0; i--) { int j = door_rnd(i + 1), t = order[i]; order[i] = order[j]; order[j] = t; }
    int asked = 0, right = 0, streak = 0, k = 0;
    game_score = 0;
    struct player *p = &all[me];
    p->today++;                                   /* counts from the moment it starts */
    save_players();
    while (asked < PER_GAME && k < nq) {
        if (load_q(order[k++]) < 0) continue;
        asked++;
        game_q = asked;
        bar();
        sio_cls();
        sio_printf("\n@X0D  Question %d of %d@X05   \xFA   @X07Score @X0F%ld", asked, PER_GAME, game_score);
        if (streak >= 3) sio_printf("@X05   \xFA   @X0Estreak %d!", streak);
        sio_puts("@X07\n\n");
        wrap(qtext, 72, "  ", "@X0F");
        sio_puts("\n");
        for (int c = 0; c < 4; c++) sio_printf("    @X0D(@X0E%c@X0D)@X07 %s\n", 'A' + c, choice[c]);
        sio_puts("\n@X0DYour answer @X0F[@X0EA B C D@X0F]@X0D: @X0F");
        uint32_t t0 = TICKS();
        int ans = -1;
        for (;;) {
            uint32_t el = TICKS() - t0;
            uint32_t lim = (uint32_t)ANSWER_SECS * 182 / 10;
            if (el >= lim) break;
            int c = sio_key_timeout((int)((lim - el) * 10000 / 182) + 1);
            if (c < 0) continue;
            c = toupper(c);
            if (c >= 'A' && c <= 'D') { ans = c - 'A'; sio_putc(c); sio_puts("\n"); break; }
        }
        uint32_t el = TICKS() - t0;
        sio_puts("\n");
        if (ans < 0) {
            sio_printf("@X0CTime's up!@X07  The answer was @X0E%c@X07: @X0F%s@X07.\n", 'A' + correct, choice[correct]);
            streak = 0;
        } else if (ans == correct) {
            int bonus = el <= 55 ? 50 : el >= 364 ? 0 : (int)(50 * (364 - el) / (364 - 55));
            int sb = streak >= 2 ? 25 : 0;
            streak++; right++;
            game_score += 100 + bonus + sb;
            sio_printf("@X0A%s@X07  @X0F+100@X07", cheers[door_rnd(6)]);
            if (bonus) sio_printf("  @X0E+%d@X07 speed", bonus);
            if (sb) sio_printf("  @X0E+%d@X07 streak", sb);
            sio_puts("\n");
        } else {
            sio_printf("@X0C%s@X07  The answer was @X0E%c@X07: @X0F%s@X07.\n", groans[door_rnd(5)], 'A' + correct, choice[correct]);
            streak = 0;
        }
        if (fact[0]) { sio_puts("\n"); wrap(fact, 70, "  ", "@X03"); }
        sio_puts("\n");
        bar();
        sio_pause();
    }
    game_q = 0;
    sio_cls();
    sio_printf("\n@X0D  GAME OVER@X07\n\n  You answered @X0F%d@X07 of @X0F%d@X07 correctly.\n", right, asked);
    if (asked == PER_GAME && right == PER_GAME) {
        game_score += 250;
        p->perfect++;
        sio_puts("\n@X0E  *** A PERFECT GAME! ***  +250 points@X07\n");
    }
    char c[16]; commas(c, game_score);
    sio_printf("\n  Final score: @X0E%s@X07 points.\n", c);
    static const struct player *top[MAXPLAYERS];
    long board_best = 0;
    int n = sorted(top);
    if (n) board_best = top[0]->best;
    p->games++;
    p->total += game_score;
    if (game_score > p->best) {
        int record = game_score > board_best;
        p->best = game_score;
        door_today(p->bestdate);
        sio_printf("\n@X0A  That's a new personal best!@X07\n");
        if (record) {
            sio_puts("@X0E  ... and a new ARM Pit RECORD!  Your name goes at the top of the hall.@X07\n");
            door_news("%s set a new Byte-Sized Trivia record: %s points!", p->name, c);
        } else if (right == PER_GAME) door_news("%s played a perfect game of Byte-Sized Trivia (%s points).", p->name, c);
    } else if (right == PER_GAME) door_news("%s played a perfect game of Byte-Sized Trivia (%s points).", p->name, c);
    save_players();
    sio_printf("\n  Games left today: @X0F%d@X07\n\n", GAMES_PER_DAY - p->today > 0 ? GAMES_PER_DAY - p->today : 0);
    bar();
    sio_pause();
    return game_score;
}

static void menu(void)
{
    for (;;) {
        struct player *p = &all[me];
        bar();
        title();
        char b[16], t[16]; commas(b, p->best); commas(t, p->total);
        sio_printf("  @X07Player: @X0F%s@X07    Best game: @X0E%s@X07    Total: @X0E%s@X07\n", p->name, b, t);
        sio_printf("  @X07Questions in the box: @X0F%d@X07    Games left today: @X0F%d@X07\n\n",
                   nq, GAMES_PER_DAY - p->today > 0 ? GAMES_PER_DAY - p->today : 0);
        sio_puts("  @X05(@X0DP@X05)@X07lay a game        @X05(@X0DH@X05)@X07all of fame\n");
        sio_puts("  @X05(@X0DR@X05)@X07ules              @X05(@X0DQ@X05)@X07uit to the BBS\n");
        sio_printf("\n@X0DTrivia @X0F(%lu min left)@X0D [@X0FP H R Q@X0D]: @X0F", (unsigned long)sio_minutes_left());
        int k = sio_hotkey("PHRQ");
        if (k == 'Q') return;
        if (k == 'H') hall();
        if (k == 'R') rules();
        if (k == 'P') {
            if (p->today >= GAMES_PER_DAY) {
                sio_puts("\n@X0CThat's three games today.  The question box is locked until tomorrow!@X07\n");
                sio_pause();
            } else if (nq < PER_GAME) {
                sio_puts("\n@X0CThe sysop lost the question box (QUESTION.TXT).  Try again later.@X07\n");
                sio_pause();
            } else play();
        }
    }
}

int main(int argc, char **argv)
{
    door_init(argc, argv, bar);
    load_players();
    index_q();
    if (setjmp(sio_drop) == 0) {
        find_me();
        int today = door_daynum();
        if (all[me].day != today) { all[me].day = today; all[me].today = 0; }
        title();
        char f[16];
        if (all[me].games) sio_printf("  @X0DWelcome back, @X0F%s@X0D.  The question box is shuffled and ready.@X07\n\n", door_first(door_user, f, sizeof f));
        else sio_printf("  @X0DA new contestant!  Welcome, @X0F%s@X0D.@X07\n\n", door_user);
        sio_pause();
        menu();
        sio_puts("\n@X0DThanks for playing Byte-Sized Trivia!  Back to the BBS...@X07\n");
    } else {
        /* carrier lost, time up or idle: the game in progress counts as played */
        save_players();
    }
    door_exit();
    return 0;
}
