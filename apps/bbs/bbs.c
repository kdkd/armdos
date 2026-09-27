/* bbs.c - The ARM Pit BBS: main program, waiting-for-call screen, modem
 * handling, logon, new-user registration, main menu, goodbye.
 *
 * Original program. The look is an homage to Mustang Software's WildCat!
 * 2.x/3.x (1986-1990): the sysop's waiting-for-call screen, the "What is your
 * FIRST name?" logon, the new-user questionnaire, @X colour codes in display
 * files, hotkey menus with bracketed letters, "More [Y,n,=]?".
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include <dos.h>
#include <direct.h>
#include <armdos.h>
#include "../term/lib/comm.h"
#include "../term/lib/scr.h"
#include "bbs.h"

struct cfg cfg = { "The ARM Pit", "Europa Sysop", "Hollywood, FL", 2, 2400, 60, 10, "ATE0V1Q0S0=0&C1&D2", 1 };
struct area msgareas[MAXAREAS], fileareas[MAXAREAS], doors[MAXAREAS];
int nmsgareas, nfileareas, ndoors;
struct user user;
int usernum = -1;
long session_baud = 2400;
static uint32_t session_t0;
static char homedir[80];

/* stats (STATS.DAT) */
static struct { long calls; int today_calls; char today[10]; char lastname[36]; char lastcity[32]; char lasttime[16]; } st;

static char modemlog[5][40];

/* ================================================================ helpers */
void today_str(char *b)
{
    time_t t = time(NULL); struct tm *tm = localtime(&t);
    sprintf(b, "%02d-%02d-%02d", tm->tm_mon + 1, tm->tm_mday, tm->tm_year % 100);
}
void now_str(char *b)
{
    time_t t = time(NULL); struct tm *tm = localtime(&t);
    sprintf(b, "%02d-%02d-%02d %02d:%02d", tm->tm_mon + 1, tm->tm_mday, tm->tm_year % 100, tm->tm_hour, tm->tm_min);
}
void time_str(char *b)
{
    time_t t = time(NULL); struct tm *tm = localtime(&t);
    sprintf(b, "%02d:%02d:%02d", tm->tm_hour, tm->tm_min, tm->tm_sec);
}
void commas(char *b, long v)
{
    char t[20]; int n = sprintf(t, "%ld", v), o = 0;
    for (int i = 0; i < n; i++) { if (i && (n - i) % 3 == 0) b[o++] = ','; b[o++] = t[i]; }
    b[o] = 0;
}
void strupr_s(char *s) { for (; *s; s++) *s = (char)toupper((unsigned char)*s); }

void sysop_log(const char *fmt, ...)
{
    char b[160], d[20];
    va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    FILE *f = fopen("CALLERS.LOG", "a");
    if (!f) return;
    now_str(d);
    fprintf(f, "%s  %s\n", d, b);
    fclose(f);
}

static void load_stats(void)
{
    char line[80];
    memset(&st, 0, sizeof st);
    FILE *f = fopen("STATS.DAT", "r");
    if (!f) { st.calls = 1336; strcpy(st.lastname, "Karen Whitfield"); strcpy(st.lastcity, "Boca Raton, FL"); strcpy(st.lasttime, "11-04-89 20:47"); return; }
    while (fgets(line, sizeof line, f)) {
        char *e = strchr(line, '='); if (!e) continue;
        *e++ = 0; char *nl = strpbrk(e, "\r\n"); if (nl) *nl = 0;
        if (!strcmp(line, "CALLS")) st.calls = atol(e);
        else if (!strcmp(line, "TODAYCALLS")) st.today_calls = atoi(e);
        else if (!strcmp(line, "TODAY")) snprintf(st.today, sizeof st.today, "%s", e);
        else if (!strcmp(line, "LASTNAME")) snprintf(st.lastname, sizeof st.lastname, "%s", e);
        else if (!strcmp(line, "LASTCITY")) snprintf(st.lastcity, sizeof st.lastcity, "%s", e);
        else if (!strcmp(line, "LASTTIME")) snprintf(st.lasttime, sizeof st.lasttime, "%s", e);
    }
    fclose(f);
    char t[10]; today_str(t);
    if (strcmp(t, st.today)) { st.today_calls = 0; strcpy(st.today, t); }
}

static void save_stats(void)
{
    FILE *f = fopen("STATS.DAT", "w");
    if (!f) return;
    fprintf(f, "CALLS=%ld\nTODAYCALLS=%d\nTODAY=%s\nLASTNAME=%s\nLASTCITY=%s\nLASTTIME=%s\n",
            st.calls, st.today_calls, st.today, st.lastname, st.lastcity, st.lasttime);
    fclose(f);
}

static void load_config(void)
{
    char line[160];
    FILE *f = fopen("BBS.CFG", "r");
    if (f) {
        while (fgets(line, sizeof line, f)) {
            if (line[0] == ';') continue;
            char *e = strchr(line, '='); if (!e) continue;
            *e++ = 0; char *nl = strpbrk(e, "\r\n"); if (nl) *nl = 0;
            if (!strcmp(line, "NAME")) snprintf(cfg.name, sizeof cfg.name, "%s", e);
            else if (!strcmp(line, "SYSOP")) snprintf(cfg.sysop, sizeof cfg.sysop, "%s", e);
            else if (!strcmp(line, "LOCATION")) snprintf(cfg.location, sizeof cfg.location, "%s", e);
            else if (!strcmp(line, "PORT")) cfg.port = atoi(e);
            else if (!strcmp(line, "BAUD")) cfg.baud = atol(e);
            else if (!strcmp(line, "TIMELIMIT")) cfg.timelimit = atoi(e);
            else if (!strcmp(line, "NEWSEC")) cfg.newsec = atoi(e);
            else if (!strcmp(line, "INIT")) snprintf(cfg.init, sizeof cfg.init, "%s", e);
            else if (!strcmp(line, "NODE")) cfg.node = atoi(e);
        }
        fclose(f);
    }
    f = fopen("AREAS.CFG", "r");
    if (!f) return;
    struct area *tab = NULL; int *cnt = NULL;
    while (fgets(line, sizeof line, f)) {
        char *nl = strpbrk(line, "\r\n"); if (nl) *nl = 0;
        if (line[0] == ';' || !line[0]) continue;
        if (!strcmp(line, "[MSG]")) { tab = msgareas; cnt = &nmsgareas; continue; }
        if (!strcmp(line, "[FILE]")) { tab = fileareas; cnt = &nfileareas; continue; }
        if (!strcmp(line, "[DOOR]")) { tab = doors; cnt = &ndoors; continue; }
        if (!tab || *cnt >= MAXAREAS) continue;
        struct area *a = &tab[*cnt];
        memset(a, 0, sizeof *a);
        char *fld[5] = { 0 }; int n = 0; char *s = line;
        while (n < 5) { fld[n++] = s; s = strchr(s, '|'); if (!s) break; *s++ = 0; }
        if (n < 3) continue;
        a->num = atoi(fld[0]);
        snprintf(a->name, sizeof a->name, "%s", fld[1]);
        snprintf(a->path, sizeof a->path, "%s", fld[2]);
        if (n > 3) snprintf(a->extra, sizeof a->extra, "%s", fld[3]);
        if (n > 4) snprintf(a->desc, sizeof a->desc, "%s", fld[4]);
        (*cnt)++;
    }
    fclose(f);
}

/* ================================================================ local screen */
#define A_FRAME 0x1B
#define A_LABEL 0x1F
#define A_VALUE 0x1E
#define A_DIM   0x17

static void clock_line(void)
{
    char d[12], t[12];
    today_str(d); time_str(t);
    scr_printf(56, 2, A_VALUE, "%s  %s", d, t);
}

static void modem_logline(const char *s)
{
    memmove(modemlog[0], modemlog[1], sizeof modemlog[0] * 4);
    snprintf(modemlog[4], sizeof modemlog[4], "%s", s);
    for (int i = 0; i < 5; i++) scr_printf(44, 13 + i, A_VALUE, "%-32.32s", modemlog[i]);
}

static void waiting_screen(const char *state)
{
    char b[80], c[20];
    scr_cursor_on(0);
    scr_fill(0, 0, 80, 25, ' ', 0x17);
    scr_box(0, 0, 80, 25, A_FRAME, 1, NULL);
    snprintf(b, sizeof b, " %s BBS  \xB3  Node %d  \xB3  v%s ", cfg.name, cfg.node, BBS_VERSION);
    scr_puts((80 - (int)strlen(b)) / 2, 0, 0x1E, b);
    scr_puts(3, 2, A_LABEL, "ARM-DOS Bulletin Board System");
    clock_line();
    for (int x = 1; x < 79; x++) scr_putc(x, 3, 0xC4, A_FRAME);
    scr_puts(3, 5, A_LABEL, "Last caller:");
    scr_printf(17, 5, A_VALUE, "%-28.28s", st.lastname);
    scr_printf(17, 6, A_DIM, "%-28.28s", st.lastcity);
    scr_puts(3, 7, A_LABEL, "Last on:");     scr_printf(17, 7, A_VALUE, "%s", st.lasttime);
    scr_puts(3, 9, A_LABEL, "Calls today:"); scr_printf(17, 9, A_VALUE, "%-6d", st.today_calls);
    commas(c, st.calls);
    scr_puts(3, 10, A_LABEL, "Total calls:"); scr_printf(17, 10, A_VALUE, "%-10s", c);
    scr_puts(44, 5, A_LABEL, "Users:");      scr_printf(58, 5, A_VALUE, "%d", user_count());
    int m = 0; for (int i = 0; i < nmsgareas; i++) m += msg_count(i);
    scr_puts(44, 6, A_LABEL, "Messages:");   scr_printf(58, 6, A_VALUE, "%d", m);
    scr_puts(44, 7, A_LABEL, "Files:");      scr_printf(58, 7, A_VALUE, "%d", file_count());
    scr_puts(44, 9, A_LABEL, "Port:");       scr_printf(58, 9, A_VALUE, "COM%d  %ld baud", cfg.port, cfg.baud);
    for (int x = 1; x < 79; x++) scr_putc(x, 11, 0xC4, A_FRAME);
    scr_puts(44, 12, A_LABEL, "Modem:");
    for (int i = 0; i < 5; i++) scr_printf(44, 13 + i, A_VALUE, "%-32.32s", modemlog[i]);
    for (int x = 1; x < 79; x++) scr_putc(x, 19, 0xC4, A_FRAME);
    scr_fill(3, 15, 36, 1, ' ', 0x17);
    scr_printf(3 + (36 - (int)strlen(state)) / 2, 15, 0x9F, "%s", state);
    scr_puts(3, 21, 0x1E, "F1");  scr_puts(6, 21, A_LABEL, "Local logon");
    scr_puts(22, 21, 0x1E, "F2"); scr_puts(25, 21, A_LABEL, "Answer now");
    scr_puts(40, 21, 0x1E, "Alt-X"); scr_puts(46, 21, A_LABEL, "Exit to DOS");
    scr_puts(3, 23, A_DIM, "The ARM Pit BBS - original software for ARM-DOS, a WildCat!-style homage");
}

void local_status(void)
{
    char b[96], t[16];
    uint32_t on = (uint32_t)((uint64_t)(TICKS() - session_t0) * 10 / 182);
    snprintf(t, sizeof t, "%02lu:%02lu:%02lu", (unsigned long)(on / 3600), (unsigned long)(on / 60 % 60), (unsigned long)(on % 60));
    scr_fill(0, 23, 80, 2, ' ', 0x70);
    snprintf(b, sizeof b, " %-24.24s %-24.24s Sec %-3d Calls %-4d %s", user.name[0] ? user.name : "(logging on)",
             user.city, user.sec, user.calls, sio_ansi ? "ANSI" : "TTY");
    scr_puts(0, 23, 0x70, b);
    snprintf(b, sizeof b, " %s %ld  Time left %3lu  Online %s  \xB3 Alt-H hang up caller",
             sio_remote ? "Remote" : "Local", session_baud, (unsigned long)sio_minutes_left(), t);
    scr_puts(0, 24, 0x70, b);
    scr_cursor(sio_vt.x, sio_vt.y);
}

/* ================================================================ modem */
static int modem_line(char *buf, int max, int ms)
{
    int n = 0;
    uint32_t t0 = TICKS(), lim = ms2ticks((uint32_t)ms);
    for (;;) {
        int c = com_getc();
        if (c < 0) {
            if (TICKS() - t0 >= lim) return -1;
            idle();
            continue;
        }
        if (c == '\r' || c == '\n') { if (n) { buf[n] = 0; return n; } continue; }
        if (n < max - 1 && c >= 32) buf[n++] = (char)c;
    }
}

static int modem_cmd(const char *cmd, int ms)
{
    char line[64];
    /* a bare CR first: whatever half-line is in the modem's command buffer (line
       noise, the tail of a file that was still queued when the call ended) is
       answered with ERROR and forgotten, and does not end up in front of cmd */
    com_puts("\r");
    delay_ms(150);
    com_rxpurge();
    com_puts(cmd); com_puts("\r");
    modem_logline(cmd);
    uint32_t t0 = TICKS();
    while (TICKS() - t0 < ms2ticks((uint32_t)ms)) {
        if (modem_line(line, sizeof line, ms) < 0) break;
        if (!strcmp(line, cmd)) continue;
        modem_logline(line);
        if (!strcmp(line, "OK") || !strcmp(line, "0")) return 0;
        if (!strcmp(line, "ERROR") || !strcmp(line, "4")) return -1;
    }
    return -1;
}

static int modem_init(void)
{
    com_dtr(1);
    delay_ms(200);
    int ok = modem_cmd("ATZ", 3000) == 0;
    if (!ok) {                                 /* still online after all? escape, then try again */
        delay_ms(1100); com_puts("+++"); delay_ms(1100);
        modem_cmd("ATH0", 2000);
        ok = modem_cmd("ATZ", 3000) == 0;
    }
    delay_ms(100);
    if (modem_cmd(cfg.init, 3000) < 0) ok = 0;
    return ok;
}

static void hangup(void)
{
    com_dtr(0);
    delay_ms(1500);
    com_dtr(1);
    if (com_carrier()) {                       /* the modem ignored DTR */
        delay_ms(1100); com_puts("+++"); delay_ms(1100);
        modem_cmd("ATH0", 3000);
    }
    com_rxpurge();
}

/* ================================================================ logon */
static const char *first_word(const char *s, char *b, int n)
{
    int i = 0;
    while (*s == ' ') s++;
    while (*s && *s != ' ' && i < n - 1) b[i++] = *s++;
    b[i] = 0;
    return b;
}

static void detect_ansi(void)
{
    if (!sio_remote) { sio_ansi = 1; return; }
    /* ask the terminal where its cursor is: an ANSI terminal answers */
    com_rxpurge();
    com_puts("\r\n\033[6n");
    sio_ansi = 0;
    uint32_t t0 = TICKS();
    int st = 0;
    while (TICKS() - t0 < 50) {                 /* ~2.7 s */
        int c = com_getc();
        if (c < 0) { if (!com_carrier()) return; idle(); continue; }
        if (c == 27) st = 1;
        else if (st == 1 && c == '[') st = 2;
        else if (st == 2 && c == 'R') { sio_ansi = 1; break; }
    }
    delay_ms(100);
    com_rxpurge();
}

static int new_user(const char *name)
{
    char b[80], pw1[20], pw2[20];
    sio_puts("\n");
    if (!sio_showfile("DISPLAY\\NEWUSER.TXT"))
        sio_puts("@X0FWelcome, new user!@X07\n");
    memset(&user, 0, sizeof user);
    snprintf(user.name, sizeof user.name, "%s", name);
    user.sec = (uint16_t)cfg.newsec;
    user.lines = 24;
    user.ansi = (uint8_t)sio_ansi;
    for (;;) {
        sio_puts("\n@X0BPlease answer the following questions.@X07\n\n");
        sio_puts("@X0EWhat City and State are you calling from?@X07\n: ");
        sio_getline(b, 30, GL_NAME);
        snprintf(user.city, sizeof user.city, "%s", b[0] ? b : "Somewhere, USA");
        sio_puts("@X0EYour phone number (###-###-####)?@X07\n: ");
        sio_getline(b, 14, GL_DIGITS);
        snprintf(user.phone, sizeof user.phone, "%s", b);
        sio_puts("@X0EYour birthdate (MM-DD-YY)?@X07\n: ");
        sio_getline(b, 8, GL_DIGITS);
        snprintf(user.birth, sizeof user.birth, "%s", b);
        sio_puts("@X0EWhat kind of computer do you use?@X07\n: ");
        sio_getline(b, 30, 0);
        snprintf(user.computer, sizeof user.computer, "%s", b[0] ? b : "An ARM PC, of course");
        sio_puts("@X0EHow many lines does your screen have (Enter = 24)?@X07\n: ");
        sio_getline(b, 2, GL_DIGITS);
        user.lines = (uint16_t)(atoi(b) >= 10 ? atoi(b) : 24);
        for (;;) {
            sio_puts("@X0EEnter a password you will use to log on (4-15 characters)@X07\n: ");
            sio_getline(pw1, 15, GL_PASSWORD | GL_UPPER);
            if (strlen(pw1) < 4) { sio_puts("@X0CToo short.@X07\n"); continue; }
            sio_puts("@X0EEnter it again to verify@X07\n: ");
            sio_getline(pw2, 15, GL_PASSWORD | GL_UPPER);
            if (!strcmp(pw1, pw2)) break;
            sio_puts("@X0CThose don't match. Try again.@X07\n");
        }
        snprintf(user.password, sizeof user.password, "%s", pw1);
        sio_puts("\n@X0B  Name: @X0F"); sio_puts(user.name);
        sio_printf("\n@X0B  City: @X0F%s\n@X0B Phone: @X0F%s\n@X0B Birth: @X0F%s\n@X0BSystem: @X0F%s\n@X0B Lines: @X0F%d@X07\n\n",
                   user.city, user.phone, user.birth, user.computer, user.lines);
        if (sio_yesno("Is this information correct", 1)) break;
    }
    today_str(user.first);
    usernum = user_save(-1, &user);
    sysop_log("NEW USER: %s of %s", user.name, user.city);
    sio_printf("\n@X0AThank you, %s! You have been given access level %d.@X07\n", first_word(user.name, b, 20), user.sec);
    sio_pause();
    return 1;
}

static int logon(void)
{
    char first[24], last[24], name[48], b[48];
    detect_ansi();
    sio_cls();
    if (!sio_showfile(sio_ansi ? "DISPLAY\\WELCOME.ANS" : "DISPLAY\\WELCOME.TXT"))
        sio_printf("\n  Welcome to %s BBS!\n\n", cfg.name);
    sio_printf("@X07\n  ARM-DOS BBS v%s.  Node %d.  You are connected at %ld baud%s.\n\n", BBS_VERSION, cfg.node,
               session_baud, sio_ansi ? " with ANSI color" : "");
    sio_puts("@X0B  Just looking?  Log on as @X0FGUEST@X0B (leave the last name blank), password @X0FGUEST@X0B.@X07\n\n");
    for (int tries = 0; tries < 5; tries++) {
        sio_puts("@X0EWhat is your FIRST name? @X0F");
        sio_getline(first, 20, GL_NAME);
        if (!first[0]) continue;
        sio_puts("@X0EWhat is your LAST name? @X0F");
        sio_getline(last, 22, GL_NAME);
        /* the guest account has no last name, but GUEST GUEST is what people type */
        if (!strcasecmp(first, "GUEST") && !strcasecmp(last, "GUEST")) last[0] = 0;
        snprintf(name, sizeof name, "%s%s%s", first, last[0] ? " " : "", last);
        sio_printf("@X07Searching for @X0F%s@X07 in the user file...\n", name);
        int n = user_find(name, &user);
        if (n >= 0) {
            usernum = n;
            for (int p = 0; p < 3; p++) {
                sio_puts("@X0EPassword? @X0F");
                sio_getline(b, 15, GL_PASSWORD | GL_UPPER);
                if (!strcasecmp(b, user.password)) {
                    sio_ansi = user.ansi ? sio_ansi : 0;
                    sio_lines = user.lines ? user.lines : 24;
                    return 1;
                }
                sio_puts("@X0CIncorrect password.@X07\n");
                sysop_log("Bad password for %s", user.name);
            }
            sio_puts("@X0CToo many tries. Goodbye.@X07\n");
            return 0;
        }
        sio_printf("\n@X0F%s@X07 was not found in the user file.\n", name);
        if (sio_yesno("Did you enter your name correctly", 1)) return new_user(name);
    }
    return 0;
}

static void welcome_back(void)
{
    char b[24], c[20];
    char today[10]; today_str(today);
    st.calls++;
    st.today_calls++;
    if (strcmp(user.today, today)) { user.mins_today = 0; strcpy(user.today, today); }
    user.calls++;
    commas(c, st.calls);
    sio_cls();
    sio_printf("\n@X0BWelcome%s, @X0F%s@X0B!@X07\n\n", user.calls > 1 ? " back" : "", first_word(user.name, b, 20));
    sio_printf("  You are caller number @X0E%s@X07 to %s.\n", c, cfg.name);
    if (user.calls > 1) sio_printf("  You last called on @X0E%s@X07.  This is call number @X0E%d@X07.\n", user.last, user.calls);
    sio_printf("  The last caller was @X0E%s@X07 of %s.\n", st.lastname, st.lastcity);
    sio_printf("  You have @X0E%d@X07 minutes for this call.\n\n", cfg.timelimit);
    if (user.calls > 1) sysop_note();
    sio_deadline = TICKS() + (uint32_t)cfg.timelimit * 1092;
    snprintf(st.lastname, sizeof st.lastname, "%s", user.name);
    snprintf(st.lastcity, sizeof st.lastcity, "%s", user.city);
    now_str(st.lasttime);
    save_stats();
    now_str(user.last);
    user_save(usernum, &user);
    lastcall_add();
    sysop_log("%s of %s logged on (%s, %ld baud)", user.name, user.city, sio_remote ? "remote" : "local", session_baud);
    sio_pause();
}

/* ================================================================ menus */
static void main_menu_text(void)
{
    sio_cls();
    if (!user.expert && sio_showfile(sio_ansi ? "DISPLAY\\MAIN.ANS" : "DISPLAY\\MAIN.TXT")) return;
    if (user.expert) return;
    sio_puts("\n@X1F  MAIN MENU  @X07\n\n");
    sio_puts("  @X0F[@X0EM@X0F]@X07 Message Menu          @X0F[@X0EF@X0F]@X07 File Menu\n");
    sio_puts("  @X0F[@X0EB@X0F]@X07 Bulletins             @X0F[@X0ED@X0F]@X07 Doors (games)\n");
    sio_puts("  @X0F[@X0EW@X0F]@X07 Who's online          @X0F[@X0EU@X0F]@X07 Userlog\n");
    sio_puts("  @X0F[@X0EC@X0F]@X07 Comment to sysop      @X0F[@X0ES@X0F]@X07 Your settings\n");
    sio_puts("  @X0F[@X0EL@X0F]@X07 Last callers          @X0F[@X0EG@X0F]@X07 Goodbye (log off)\n");
}

static int goodbye(void)
{
    if (!sio_yesno("\n@X0ELog off now", 0)) return 0;
    sio_puts("\n");
    if (!sio_showfile(sio_ansi ? "DISPLAY\\GOODBYE.ANS" : "DISPLAY\\GOODBYE.TXT"))
        sio_printf("@X0FThank you for calling %s!@X07\n", cfg.name);
    return 1;
}

static void main_menu(void)
{
    int show = 1;
    for (;;) {
        if (show) main_menu_text();
        show = !user.expert;
        sio_printf("\n@X0BMain Menu @X0F[@X0EM F B D W U C S L G ?@X0F]@X0B (%lu min left): @X0F", (unsigned long)sio_minutes_left());
        int k = sio_hotkey("MFBDWUCSLG?\r");
        switch (k) {
        case 'M': msg_menu(); break;
        case 'F': file_menu(); break;
        case 'B': bulletins(0); break;
        case 'D': door_menu(); break;
        case 'W': whos_online(); break;
        case 'U': userlog(); break;
        case 'C': comment_to_sysop(); break;
        case 'S': settings(); break;
        case 'L': last_callers(); sio_pause(); break;
        case 'G': if (goodbye()) return; break;
        case '?': show = 1; user.expert = 0; break;
        default: show = 1; break;
        }
    }
}

/* ================================================================ session */
static void session(int remote, long baud)
{
    session_baud = baud;
    session_t0 = TICKS();
    sio_begin(remote, 1, 23);
    sio_deadline = 0;
    sio_idle_ticks = 5u * 1092;                /* 5 minutes of silence */
    sio_status_hook = local_status;
    memset(&user, 0, sizeof user);
    usernum = -1;
    sio_lines = 24;
    vt_clear(&sio_vt);
    scr_cursor_on(1);
    local_status();
    volatile int why = setjmp(sio_drop);
    if (why == 0) {
        if (logon()) {
            welcome_back();
            bulletins(1);
            main_menu();
            sio_flush();
        } else sio_flush();
    } else {
        /* close whatever was open when the line dropped */
        for (int h = 5; h < 20; h++) { union REGS r; r.x.ax = 0x3E00; r.x.bx = (unsigned)h; intdos(&r, &r); }
        sio_deadline = 0;
        sio_idle_ticks = 0;
        if (why == SIO_TIMEUP) {
            if (setjmp(sio_drop) == 0) { sio_puts("\n\n@X0CSorry, your time is up for this call. Please call again!@X07\n"); sio_flush(); }
        } else if (why == SIO_IDLE) {
            if (setjmp(sio_drop) == 0) { sio_puts("\n\n@X0CNo input for 5 minutes - disconnecting.@X07\n"); sio_flush(); }
        } else if (why == SIO_HANGUP) {
            if (setjmp(sio_drop) == 0) { sio_puts("\n\n@X0CThe sysop has disconnected you. Goodbye!@X07\n"); sio_flush(); }
        }
        sysop_log("%s: %s", user.name[0] ? user.name : "(caller)",
                  why == SIO_CARRIER ? "carrier lost" : why == SIO_TIMEUP ? "time expired" : why == SIO_IDLE ? "inactivity" : "sysop hung up");
    }
    sio_deadline = 0;
    sio_idle_ticks = 0;
    sio_status_hook = NULL;
    if (usernum >= 0) {
        user.mins_today += (uint16_t)((TICKS() - session_t0) / 1092);
        user_save(usernum, &user);
        sysop_log("%s logged off", user.name);
    }
    if (remote) { com_txpurge(); hangup(); }   /* (what was still queued for the caller is not sent to the modem after the call) */
}

static void int23(struct armregs *f) { f->cpsr &= ~ARM_CPSR_C; }

int main(int argc, char **argv)
{
    /* run from the BBS directory (where BBS.EXE lives) */
    const char *self = _armdos_progpath && *_armdos_progpath ? _armdos_progpath : (argc > 0 ? argv[0] : "");
    snprintf(homedir, sizeof homedir, "%s", self);
    char *bs = strrchr(homedir, '\\');
    if (bs) { *bs = 0; if (homedir[1] == ':' && homedir[0]) _chdrive(toupper((unsigned char)homedir[0]) - 'A' + 1); chdir(homedir[2] ? homedir : "\\"); }
    int local_once = 0;
    for (int i = 1; i < argc; i++) if (!strcasecmp(argv[i], "/L")) local_once = 1;
    _dos_setvect(0x23, int23);
    load_config();
    load_stats();
    user_seed();
    scr_init();

    if (local_once) { session(0, 0); return 0; }

    int have_port = com_open(cfg.port, cfg.baud) == 0;
    for (;;) {
        waiting_screen(have_port ? "Initializing modem..." : "No serial port!");
        int modem_ok = have_port && modem_init();
        waiting_screen(modem_ok ? "Waiting for call..." : "Modem not responding");
        uint32_t last_clock = 0, last_try = TICKS();
        char line[64];
        int n = 0, answered = 0;
        long baud = 0;
        for (;;) {
            if (TICKS() - last_clock >= 9) { last_clock = TICKS(); clock_line(); }
            if (key_ready()) {
                int k = key_get();
                if (k == K_ALT(ALT_X) || k == K_ESC) {
                    scr_fill(3, 15, 36, 1, ' ', 0x17);
                    scr_puts(8, 15, 0x9F, "Exit to DOS (Y/N)?");
                    int y = toupper(key_get());
                    if (y == 'Y') {
                        com_close(0);
                        union REGS r; r.x.ax = 0x0003; int86(0x10, &r, &r);
                        printf("%s BBS shut down by the sysop.\n", cfg.name);
                        return 1;
                    }
                    waiting_screen("Waiting for call...");
                }
                if (k == K_F(1) || toupper(k) == 'L') {
                    com_puts("ATH1\r");        /* busy out the line while the sysop is on */
                    delay_ms(300);
                    session(0, 0);
                    break;
                }
                if (k == K_F(2) && have_port) { com_puts("ATA\r"); modem_logline("ATA"); answered = 1; }
            }
            if (!have_port) { idle(); continue; }
            if (!modem_ok && TICKS() - last_try > 5 * 18) break;       /* retry the init */
            int c = com_getc();
            if (c < 0) {
                if (com_carrier() && !answered) { baud = cfg.baud; goto connected; }   /* auto-answered */
                idle();
                continue;
            }
            if (c == '\r' || c == '\n') {
                if (!n) continue;
                line[n] = 0; n = 0;
                modem_logline(line);
                if (!strcmp(line, "RING") || !strcmp(line, "2")) {
                    scr_fill(3, 15, 36, 1, ' ', 0x17);
                    scr_puts(15, 15, 0x9E, "RING!");
                    com_puts("ATA\r");
                    modem_logline("ATA");
                    answered = 1;
                } else if (!strncmp(line, "CONNECT", 7) || !strcmp(line, "1") || !strcmp(line, "10")) {
                    baud = atol(line + 7);
                    if (!baud) baud = !strcmp(line, "10") ? 2400 : 300;
                    goto connected;
                } else if (!strcmp(line, "NO CARRIER") || !strcmp(line, "3")) {
                    answered = 0;
                    waiting_screen("Waiting for call...");
                }
                continue;
            }
            if (n < (int)sizeof line - 1 && c >= 32) line[n++] = (char)c;
            continue;
        connected:
            scr_fill(3, 15, 36, 1, ' ', 0x17);
            scr_printf(10, 15, 0x9E, "CONNECT %ld", baud);
            delay_ms(500);
            /* wait for carrier to settle */
            for (int i = 0; i < 20 && !com_carrier(); i++) delay_ms(100);
            if (com_carrier()) session(1, baud);
            break;
        }
    }
}
