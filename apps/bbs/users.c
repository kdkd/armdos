/* users.c - USERS.DAT: fixed 256-byte records. */
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "bbs.h"

#define USERFILE "USERS.DAT"

int user_count(void)
{
    FILE *f = fopen(USERFILE, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long n = ftell(f) / (long)sizeof(union userrec);
    fclose(f);
    return (int)n;
}

int user_get(int n, struct user *u)
{
    union userrec r;
    FILE *f = fopen(USERFILE, "rb");
    if (!f) return -1;
    fseek(f, (long)n * (long)sizeof r, SEEK_SET);
    int ok = fread(&r, sizeof r, 1, f) == 1;
    fclose(f);
    if (!ok) return -1;
    *u = r.u;
    return 0;
}

int user_find(const char *name, struct user *u)
{
    union userrec r;
    FILE *f = fopen(USERFILE, "rb");
    if (!f) return -1;
    for (int n = 0; fread(&r, sizeof r, 1, f) == 1; n++) {
        if (!r.u.deleted && !strcasecmp(r.u.name, name)) { fclose(f); *u = r.u; return n; }
    }
    fclose(f);
    return -1;
}

int user_save(int n, const struct user *u)
{
    union userrec r;
    memset(&r, 0, sizeof r);
    r.u = *u;
    FILE *f = fopen(USERFILE, "r+b");
    if (!f) f = fopen(USERFILE, "w+b");
    if (!f) return -1;
    if (n < 0) { fseek(f, 0, SEEK_END); n = (int)(ftell(f) / (long)sizeof r); }
    else fseek(f, (long)n * (long)sizeof r, SEEK_SET);
    fwrite(&r, sizeof r, 1, f);
    fclose(f);
    return n;
}

/* A fresh board still has its regulars from 1989. */
void user_seed(void)
{
    static const struct { const char *name, *city, *comp, *last; int calls, msgs, sec; } seed[] = {
        { "Europa Sysop",     "Hollywood, FL",       "ARM/AT 100 MHz, 40 MB",  "11-04-89 18:02", 412, 61, 255 },
        { "Dave Morgan",      "Springfield, IL",     "ARM/AT, Lotus 1-2-3",    "11-04-89 17:05", 37, 12, 20 },
        { "Karen Whitfield",  "Boca Raton, FL",      "ARM/AT, scope, soldering iron", "11-04-89 20:47", 158, 44, 30 },
        { "Bill Tran",        "Sunnyvale, CA",       "ARM/AT + 2400 baud!",    "11-03-89 01:15", 97, 23, 20 },
        { "Rick Lambert",     "Pompano Beach, FL",   "Still on a PCjr",        "11-04-89 15:31", 45, 14, 20 },
        { "Susan Oyelaran",   "Atlanta, GA",         "ARM/AT, EGA, 2 floppies", "11-04-89 19:48", 131, 38, 30 },
        { "Marcus Feld",      "Coral Springs, FL",   "ARM Portable (luggable)", "11-02-89 16:44", 52, 11, 20 },
        { "The Byte Bandit",  "Unknown",             "Won't say",              "10-15-89 03:33", 3, 1, 5 },
        { "Gordon Kessler",   "Plantation, FL",      "386 clone, 33 MHz (!!!)", "11-04-89 16:20", 88, 57, 20 },
        { "Lenny Szabo",      "Fort Lauderdale, FL", "ARM/AT, Infocom shelf",  "11-04-89 13:55", 76, 26, 20 },
        { "Maria Delgado",    "Miami, FL",           "Whatever's on the bench", "11-04-89 09:40", 64, 31, 30 },
        { "Harold Pruitt",    "Dania Beach, FL",     "ARM/AT, gift from grandkids", "11-04-89 08:12", 41, 9, 20 },
        { "Doc Mantissa",     "Hallandale, FL",      "ARM/AT + math coprocessor", "11-01-89 22:10", 12, 6, 20 },
        { "Guest",            "Visiting",            "Just looking",           "11-04-89 12:00", 0, 0, 10 },
    };
    if (user_count() > 0) {
        /* boards set up before the guest account existed get it now */
        struct user g;
        if (user_find("Guest", &g) < 0) {
            memset(&g, 0, sizeof g);
            snprintf(g.name, sizeof g.name, "Guest");
            snprintf(g.city, sizeof g.city, "Visiting");
            snprintf(g.computer, sizeof g.computer, "Just looking");
            snprintf(g.password, sizeof g.password, "GUEST");
            snprintf(g.first, sizeof g.first, "11-04-89");
            g.sec = 10; g.lines = 24; g.ansi = 1;
            user_save(-1, &g);
        }
        return;
    }
    for (unsigned i = 0; i < sizeof seed / sizeof seed[0]; i++) {
        struct user u;
        memset(&u, 0, sizeof u);
        snprintf(u.name, sizeof u.name, "%s", seed[i].name);
        snprintf(u.city, sizeof u.city, "%s", seed[i].city);
        snprintf(u.computer, sizeof u.computer, "%s", seed[i].comp);
        snprintf(u.last, sizeof u.last, "%s", seed[i].last);
        snprintf(u.first, sizeof u.first, "%s", "06-17-88");
        snprintf(u.password, sizeof u.password, "%s", i == 0 ? "ACORN" : !strcmp(seed[i].name, "Guest") ? "GUEST" : "PASSWORD");
        u.calls = (uint16_t)seed[i].calls;
        u.msgs = (uint16_t)seed[i].msgs;
        u.sec = (uint16_t)seed[i].sec;
        u.lines = 24;
        u.ansi = 1;
        user_save(-1, &u);
    }
}
