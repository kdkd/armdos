/* msgs.c - message bases: MSGS\*.MSG text files, one record per message:
 *   @@<number>
 *   From: ...
 *   To: ...
 *   Subj: ...
 *   Date: MM-DD-YY HH:MM
 *   <blank line>
 *   <body lines>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include "bbs.h"

#define MAXMSGS 400
#define MAXLINES 60

struct msg { int num; char from[36], to[36], subj[60], date[20]; int nlines; char body[MAXLINES][80]; };
static struct msg m;
static long offs[MAXMSGS];
static int cur_area;

static int index_area(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    char line[160];
    int n = 0;
    long pos = 0;
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '@' && line[1] == '@' && n < MAXMSGS) offs[n++] = pos;
        pos = ftell(f);
    }
    fclose(f);
    return n;
}

int msg_count(int area)
{
    if (area < 0 || area >= nmsgareas) return 0;
    return index_area(msgareas[area].path);
}

static void chomp(char *s) { char *p = strpbrk(s, "\r\n"); if (p) *p = 0; }

static int load_msg(const char *path, int idx)
{
    int n = index_area(path);
    if (idx < 0 || idx >= n) return -1;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, offs[idx], SEEK_SET);
    char line[160];
    memset(&m, 0, sizeof m);
    if (fgets(line, sizeof line, f)) m.num = atoi(line + 2);
    int inbody = 0;
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '@' && line[1] == '@') break;
        chomp(line);
        if (!inbody) {
            if (!line[0]) { inbody = 1; continue; }
            if (!strncmp(line, "From: ", 6)) snprintf(m.from, sizeof m.from, "%s", line + 6);
            else if (!strncmp(line, "To: ", 4)) snprintf(m.to, sizeof m.to, "%s", line + 4);
            else if (!strncmp(line, "Subj: ", 6)) snprintf(m.subj, sizeof m.subj, "%s", line + 6);
            else if (!strncmp(line, "Date: ", 6)) snprintf(m.date, sizeof m.date, "%s", line + 6);
            continue;
        }
        if (m.nlines < MAXLINES) snprintf(m.body[m.nlines++], 80, "%s", line);
    }
    fclose(f);
    while (m.nlines && !m.body[m.nlines - 1][0]) m.nlines--;
    return 0;
}

int msg_post(int area, const char *from, const char *to, const char *subj, char lines[][80], int n)
{
    const char *path = area >= 0 ? msgareas[area].path : "MSGS\\COMMENTS.MSG";
    int cnt = index_area(path), num = 1;
    if (cnt > 0) {
        FILE *f = fopen(path, "rb");
        if (f) { char line[40]; fseek(f, offs[cnt - 1], SEEK_SET); if (fgets(line, sizeof line, f)) num = atoi(line + 2) + 1; fclose(f); }
    }
    FILE *f = fopen(path, "ab");
    if (!f) return -1;
    char d[20]; now_str(d);
    fprintf(f, "@@%d\r\nFrom: %s\r\nTo: %s\r\nSubj: %s\r\nDate: %s\r\n\r\n", num, from, to, subj, d);
    for (int i = 0; i < n; i++) fprintf(f, "%s\r\n", lines[i]);
    fclose(f);
    return num;
}

static void area_line(void)
{
    int n = msg_count(cur_area);
    sio_printf("@X0BCurrent area: @X0F%d - %s@X0B  (%d messages, last read #%d)@X07\n",
               msgareas[cur_area].num, msgareas[cur_area].name, n, user.lastread[cur_area]);
}

static void show_msg(int idx, int total)
{
    sio_cls();
    sio_printf("@X1F Msg #%d of %d @X07  @X0B%s@X07\n", m.num, total, msgareas[cur_area].name);
    sio_printf("@X0EFrom: @X0F%-30s @X0EDate: @X0F%s\n", m.from, m.date);
    sio_printf("@X0E  To: @X0F%s\n", m.to);
    sio_printf("@X0ESubj: @X0F%s\n", m.subj);
    sio_puts("@X08\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4@X07\n");
    sio_linecount = 5;
    for (int i = 0; i < m.nlines; i++) {
        /* quoted lines in another colour */
        sio_puts(m.body[i][0] == '>' || (m.body[i][0] && m.body[i][1] == '>') ? "@X0A" : "@X07");
        sio_puts(m.body[i]);
        sio_puts("\n");
        if (!sio_line_done()) break;
    }
    if (idx + 1 > user.lastread[cur_area]) user.lastread[cur_area] = (uint16_t)(idx + 1);
}

/* line editor; returns number of lines or 0 on abort */
static int editor(char lines[][80], int max)
{
    int n = 0;
    sio_printf("\n@X0BEnter your message, up to %d lines.  On a line by itself:@X07\n", max);
    sio_puts("  @X0F/S@X07 = save   @X0F/A@X07 = abort   @X0F/L@X07 = list what you have\n\n");
    for (;;) {
        char b[80];
        sio_printf("@X0E%2d:@X07 ", n + 1);
        sio_getline(b, 72, 0);
        if (b[0] == '/' && b[2] == 0) {
            int c = toupper((unsigned char)b[1]);
            if (c == 'S') { if (n) return n; sio_puts("@X0CNothing to save.@X07\n"); continue; }
            if (c == 'A') { if (sio_yesno("Abort this message", 0)) return 0; continue; }
            if (c == 'L') { for (int i = 0; i < n; i++) sio_printf("@X0E%2d:@X07 %s\n", i + 1, lines[i]); continue; }
        }
        if (n < max) snprintf(lines[n++], 80, "%s", b);
        if (n == max) { sio_puts("@X0CThat's the limit. /S to save.@X07\n"); }
    }
}

static void enter_msg(const char *to0, const char *subj0, int area)
{
    static char lines[40][80];
    char to[36], subj[60];
    sio_printf("\n@X0BPosting in @X0F%s@X07\n", area >= 0 ? msgareas[area].name : "Comments to the sysop");
    if (to0) { snprintf(to, sizeof to, "%s", to0); sio_printf("@X0E     To: @X0F%s\n", to); }
    else {
        sio_puts("@X0E     To (Enter = All): @X0F");
        sio_getline(to, 30, GL_NAME);
        if (!to[0]) strcpy(to, "All");
    }
    if (subj0) { snprintf(subj, sizeof subj, "%s", subj0); sio_printf("@X0ESubject: @X0F%s\n", subj); }
    else {
        sio_puts("@X0ESubject: @X0F");
        sio_getline(subj, 50, 0);
        if (!subj[0]) { sio_puts("@X0CNo subject - message aborted.@X07\n"); return; }
    }
    int n = editor(lines, 40);
    if (!n) { sio_puts("@X0CMessage aborted.@X07\n"); return; }
    int num = msg_post(area, user.name, to, subj, lines, n);
    if (num > 0) {
        user.msgs++;
        user_save(usernum, &user);
        sio_printf("\n@X0ASaving message #%d...  Done!@X07\n", num);
        sysop_log("%s posted message #%d in %s", user.name, num, area >= 0 ? msgareas[area].name : "COMMENTS");
    } else sio_puts("@X0CCan't write the message file!@X07\n");
}

static void read_msgs(void)
{
    char b[16];
    int total = msg_count(cur_area);
    if (!total) { sio_puts("@X0CNo messages in this area.@X07\n"); return; }
    int first_new = user.lastread[cur_area] < total ? user.lastread[cur_area] + 1 : total;
    sio_printf("@X0ERead from message # (1-%d, Enter = %d): @X0F", total, first_new);
    sio_getline(b, 5, GL_DIGITS);
    int i = b[0] ? atoi(b) - 1 : first_new - 1;
    if (i < 0) i = 0;
    if (i >= total) i = total - 1;
    for (;;) {
        if (load_msg(msgareas[cur_area].path, i) < 0) break;
        show_msg(i, total);
        sio_puts("\n@X0B[@X0EN@X0B]ext, [@X0EP@X0B]revious, [@X0ER@X0B]eply, [@X0EA@X0B]gain, [@X0EQ@X0B]uit: @X0F");
        int k = sio_hotkey("NPRAQ\r");
        if (k == 'Q') break;
        if (k == 'N' || k == '\r') { if (++i >= total) { sio_puts("@X0BNo more messages in this area.@X07\n"); break; } }
        else if (k == 'P') { if (i > 0) i--; }
        else if (k == 'R') {
            char subj[60];
            snprintf(subj, sizeof subj, "%s%.55s", strncasecmp(m.subj, "Re: ", 4) ? "Re: " : "", m.subj);
            char to[36]; snprintf(to, sizeof to, "%s", m.from);
            enter_msg(to, subj, cur_area);
            total = msg_count(cur_area);
            sio_pause();
        }
    }
    user_save(usernum, &user);
}

static void scan_msgs(void)
{
    int total = msg_count(cur_area);
    sio_printf("\n@X0F  #   From                 To                   Subject@X07\n");
    sio_linecount = 1;
    for (int i = 0; i < total; i++) {
        if (load_msg(msgareas[cur_area].path, i) < 0) break;
        sio_printf("@X0E%3d@X07  %-20.20s %-20.20s @X0B%-.32s@X07\n", m.num, m.from, m.to, m.subj);
        if (!sio_line_done()) break;
    }
}

static void change_area(void)
{
    char b[8];
    sio_puts("\n@X0FMessage areas:@X07\n");
    for (int i = 0; i < nmsgareas; i++)
        sio_printf("  @X0E%d@X07  %-30s @X08(%d msgs)@X07\n", msgareas[i].num, msgareas[i].name, msg_count(i));
    sio_puts("@X0EArea # (Enter = no change): @X0F");
    sio_getline(b, 3, GL_DIGITS);
    for (int i = 0; i < nmsgareas; i++) if (b[0] && atoi(b) == msgareas[i].num) cur_area = i;
}

void msg_menu(void)
{
    if (!nmsgareas) { sio_puts("@X0CNo message areas are set up.@X07\n"); return; }
    for (;;) {
        sio_cls();
        sio_puts("\n@X1F  MESSAGE MENU  @X07\n\n");
        sio_puts("  @X0F[@X0ER@X0F]@X07 Read messages        @X0F[@X0EE@X0F]@X07 Enter a message\n");
        sio_puts("  @X0F[@X0ES@X0F]@X07 Scan message list    @X0F[@X0EA@X0F]@X07 Change area\n");
        sio_puts("  @X0F[@X0EQ@X0F]@X07 Quit to main menu\n\n");
        area_line();
        sio_printf("\n@X0BMessage Menu @X0F[@X0ER E S A Q@X0F]@X0B (%lu min left): @X0F", (unsigned long)sio_minutes_left());
        int k = sio_hotkey("RESAQ");
        switch (k) {
        case 'R': read_msgs(); break;
        case 'E': enter_msg(NULL, NULL, cur_area); sio_pause(); break;
        case 'S': scan_msgs(); sio_pause(); break;
        case 'A': change_area(); break;
        case 'Q': return;
        }
    }
}

void comment_to_sysop(void)
{
    sio_printf("\n@X0BLeave a private comment for @X0F%s@X0B.@X07\n", cfg.sysop);
    enter_msg(cfg.sysop, NULL, -1);
    sio_pause();
}
