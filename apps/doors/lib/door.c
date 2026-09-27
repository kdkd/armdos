/* door.c - the door library, see door.h. */
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <dos.h>
#include <armdos.h>
#include "door.h"

char door_user[36] = "Local Player";
char door_bbs[40] = "The ARM Pit";
int  door_port, door_ansi = 1, door_minutes = 60, door_sec = 10, door_local;
long door_baud;

static void chomp(char *s) { char *p = strpbrk(s, "\r\n"); if (p) *p = 0; }

static int read_doorsys(const char *dir)
{
    char p[100], line[96];
    snprintf(p, sizeof p, "%s\\DOOR.SYS", dir);
    FILE *f = fopen(p, "r");
    if (!f) return 0;
    for (int i = 1; fgets(line, sizeof line, f); i++) {
        chomp(line);
        switch (i) {
        case 1: door_port = (!strncasecmp(line, "COM", 3) && line[3] >= '0' && line[3] <= '9') ? line[3] - '0' : 0; break;
        case 2: door_baud = atol(line); break;
        case 10: if (line[0]) snprintf(door_user, sizeof door_user, "%s", line); break;
        case 15: door_sec = atoi(line); break;
        case 19: door_minutes = atoi(line); break;
        case 20: door_ansi = !strcasecmp(line, "GR"); break;
        case 36: /* sysop name */ break;
        }
    }
    fclose(f);
    return 1;
}

static int read_dorinfo(const char *dir)
{
    char p[100], line[96], first[24] = "", last[24] = "";
    snprintf(p, sizeof p, "%s\\DORINFO1.DEF", dir);
    FILE *f = fopen(p, "r");
    if (!f) return 0;
    for (int i = 1; fgets(line, sizeof line, f); i++) {
        chomp(line);
        switch (i) {
        case 1: snprintf(door_bbs, sizeof door_bbs, "%s", line); break;
        case 4: door_port = atoi(line + 3); break;
        case 5: door_baud = atol(line); break;
        case 7: snprintf(first, sizeof first, "%s", line); break;
        case 8: snprintf(last, sizeof last, "%s", line); break;
        case 10: door_ansi = atoi(line) != 0; break;
        case 11: door_sec = atoi(line); break;
        case 12: door_minutes = atoi(line); break;
        }
    }
    fclose(f);
    /* DORINFO names are upper case: "ADA" "LOVELACE" -> "Ada Lovelace" */
    char name[48];
    snprintf(name, sizeof name, "%s%s%s", first, last[0] ? " " : "", last);
    for (int i = 0, up = 1; name[i]; i++) {
        char c = name[i];
        name[i] = up ? (char)(c >= 'a' && c <= 'z' ? c - 32 : c) : (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
        up = c == ' ' || c == '-' || c == '\'';
    }
    snprintf(door_user, sizeof door_user, "%s", name);
    return 1;
}

static void int23(struct armregs *f) { f->cpsr &= ~ARM_CPSR_C; }

void door_init(int argc, char **argv, void (*status)(void))
{
    const char *dir = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcasecmp(argv[i], "/L")) door_local = 1;
        else dir = argv[i];
    }
    _dos_setvect(0x23, int23);
    srand((unsigned)time(NULL) ^ (unsigned)TICKS());
    if (!door_local && dir && !read_doorsys(dir) && !read_dorinfo(dir)) door_local = 1;
    if (!dir) door_local = 1;
    if (door_local) door_port = 0;
    if (door_port < 1 || door_port > 4) door_port = 0;
    scr_init();
    int remote = 0;
    if (door_port && com_open(door_port, door_baud ? door_baud : 2400) == 0) remote = com_carrier();
    sio_begin(remote, remote ? door_ansi : 1, 23);
    sio_deadline = TICKS() + (uint32_t)(door_minutes > 0 ? door_minutes : 1) * 1092 - 1;   /* shows "60", not "61" */
    sio_idle_ticks = 5u * 1092;
    sio_status_hook = status;
    vt_clear(&sio_vt);
    if (status) status();
}

void door_exit(void)
{
    sio_status_hook = NULL;
    if (setjmp(sio_drop) == 0) sio_flush();
    if (door_port) com_close(1);                 /* never drop DTR in a door */
}

void door_today(char *b)
{
    time_t t = time(NULL); struct tm *tm = localtime(&t);
    sprintf(b, "%02d-%02d-%02d", tm->tm_mon + 1, tm->tm_mday, tm->tm_year % 100);
}

int door_daynum(void)
{
    time_t t = time(NULL); struct tm *tm = localtime(&t);
    return (tm->tm_year % 100) * 400 + tm->tm_yday;
}

int door_rnd(int n) { return n > 0 ? rand() % n : 0; }

void door_news(const char *fmt, ...)
{
    char b[160], d[12];
    va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    FILE *f = fopen("NEWS.TXT", "a");
    if (!f) return;
    door_today(d);
    fprintf(f, "%s  %s\n", d, b);
    fclose(f);
}

void door_bar(int attr, const char *l1, const char *l2)
{
    scr_fill(0, 23, 80, 2, ' ', attr);
    scr_puts(0, 23, attr, l1);
    scr_puts(0, 24, attr, l2);
    scr_cursor(sio_vt.x, sio_vt.y);
}

const char *door_first(const char *name, char *b, int n)
{
    int i = 0;
    while (*name == ' ') name++;
    while (*name && *name != ' ' && i < n - 1) b[i++] = *name++;
    b[i] = 0;
    return b;
}
