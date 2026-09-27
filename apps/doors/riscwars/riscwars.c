/* riscwars.c - RISC WARS, a space trading door for The ARM Pit BBS.
 *
 * Original game in the tradition of the multi-player space trading doors of
 * the late 1980s: a universe of numbered sectors joined by warp lanes, ports
 * that buy and sell three goods (Silicon, Firmware, Coffee) at prices that
 * follow their stock, cargo holds, fighters bought at the Fab and left in
 * sectors to claim them, a daily turn limit, and a shared universe in which
 * the board's regulars fly their own ships (they take their turns once a day,
 * when the first caller of the day opens the door). All names and text are new.
 *
 * Files in the door's directory:
 *   UNIVERSE.DAT  sectors, warps, ports, deployed fighters, the last day played
 *   PLAYERS.DAT   every trader (callers and regulars), 192-byte records
 *   NEWS.TXT      "MM-DD-YY  headline" (door_news), shown in the BBS bulletin
 *   SCORES.TXT    top traders by net worth (rewritten when it changes)
 *
 * Launched by BBS.EXE as  RISCWARS.EXE <drop-file dir>;  RISCWARS /L plays locally.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include "../lib/door.h"

#define NSECT       120         /* sectors 1..NSECT */
#define NPORT       40
#define MAXWARP     6
#define MAXPL       40
#define TURNS       150
#define ACORN_SPACE 10          /* sectors 1..10: protected, no fighters, no hazards */
#define NEB_LO      61          /* the Barrel Nebula */
#define NEB_HI      75
#define X2_SECTOR   117         /* the derelict prototype, serial 00017 */
#define MAXHOLDS    150
#define FIG_PRICE   60
#define SCAN_PRICE  3000
#define CATCHUP     5           /* days of regulars' moves simulated at most */

static const char *const goods[3] = { "Silicon", "Firmware", "Coffee" };
static const int baseprice[3] = { 18, 40, 9 };

struct port {
    uint8_t sector, type;       /* type bit g set = the port BUYS good g */
    uint8_t name, pad;
    int16_t stock[3], max[3];
};
struct sector {
    uint8_t warp[MAXWARP], nwarps;
    int8_t port;                /* index into ports, -1 = none */
    int8_t figowner;            /* player index, -1 = none */
    uint8_t pad;
    uint16_t figs;
};
struct univ {
    char magic[8];
    int32_t lastday;
    struct sector s[NSECT + 1];
    struct port p[NPORT];
};
static struct univ U;

struct player {
    char name[36];
    char ship[24];
    int32_t credits, fighters, day, bounty;
    int16_t sector, prev, holds, cargo[3], turns, kills, losses;
    uint8_t bot, style, scanner, x2, dead;
    char msg[72];               /* "while you were away" */
};
union prec { struct player p; char pad[192]; };
static struct player P[MAXPL];
static int np, me = -1;

static const char *const portnames[NPORT] = {
    "Port Thumb", "Jazelle Junction", "Barrel Shifter Depot", "Pipeline Station", "Cache Line Outpost",
    "Register Row", "Carry Flag Point", "Link Register Landing", "Stack Pointer Station", "Nibble Harbor",
    "Parity Point", "Vector Table Terminal", "Opcode Oasis", "Mnemonic Market", "Branch Delay",
    "Conditional Cove", "Coprocessor Quay", "Zero Flag Flats", "Overflow Outpost", "Byte Lane",
    "Halfword Haven", "Word Boundary", "Rising Edge", "Falling Edge", "Solder Sound",
    "Wafer Wharf", "Die Shrink Dock", "Silicon Shoals", "Burn-In Bay", "Clock Tree Crossing",
    "Writeback Wharf", "Fetch-Decode Fort", "Saturation Station", "Load Multiple Landing", "Immediate Isle",
    "Rotate Right Reach", "Supervisor Stop", "Undefined Instruction", "Endian Exchange", "Big Blue's Last Stand",
};

/* the regulars: they fly while the callers sleep */
static const struct { const char *name, *ship; int style; int32_t credits; int holds, figs; } regulars[] = {
    { "Karen Whitfield", "Big Endian",        0, 184000, 90, 400 },
    { "Gordon Kessler",  "Protected Mode",    1, 120000, 70, 900 },
    { "Susan Oyelaran",  "Stray Pointer II",  0, 142500, 75, 250 },
    { "Maria Delgado",   "Barrel Shifter",    0,  67000, 50, 150 },
    { "Bill Tran",       "Carrier Detect",    0,  96200, 60, 120 },
    { "Lenny Szabo",     "Grue Repellent",    0,  44000, 40,  80 },
    { "Dave Morgan",     "Linda Said Yes",    0,  31500, 35,  40 },
    { "Marcus Feld",     "Mom's Extension",   2,  22800, 30,  60 },
    { "The Byte Bandit", "K00L Ship",         1,  12000, 25, 300 },
    { "Rick Lambert",    "PCjr Forever",      3,   9800, 20,  10 },
};
#define NREG ((int)(sizeof regulars / sizeof regulars[0]))

/* ------------------------------------------------------------ helpers */
static const char *num(long v)
{
    static char buf[4][16];
    static int k;
    char t[16], *b = buf[k++ & 3];
    int neg = v < 0; if (neg) v = -v;
    int n = sprintf(t, "%ld", v), o = 0;
    if (neg) b[o++] = '-';
    for (int i = 0; i < n; i++) { if (i && (n - i) % 3 == 0) b[o++] = ','; b[o++] = t[i]; }
    b[o] = 0;
    return b;
}

static uint32_t lcg_state;
static int lrnd(int n) { lcg_state = lcg_state * 1103515245u + 12345u; return n > 0 ? (int)((lcg_state >> 8) % (uint32_t)n) : 0; }

static int cargo_total(const struct player *p) { return p->cargo[0] + p->cargo[1] + p->cargo[2]; }
static long networth(const struct player *p)
{
    long v = p->credits + (long)p->fighters * 50 + (long)p->holds * 150;
    for (int g = 0; g < 3; g++) v += (long)p->cargo[g] * baseprice[g];
    for (int s = 1; s <= NSECT; s++) if (U.s[s].figowner == (int8_t)(p - P)) v += (long)U.s[s].figs * 50;
    return v;
}
static int in_nebula(int s) { return s >= NEB_LO && s <= NEB_HI; }
static int adjacent(int from, int to)
{
    for (int i = 0; i < U.s[from].nwarps; i++) if (U.s[from].warp[i] == to) return 1;
    return 0;
}

/* prices: a port selling charges more as its stock runs low (70-120% of base);
 * a port buying pays more the more it still wants (90-140%) */
static int sell_price(const struct port *p, int g) { return baseprice[g] * (120 - 50 * p->stock[g] / (p->max[g] ? p->max[g] : 1)) / 100 + 1; }
static int buy_price(const struct port *p, int g) { return baseprice[g] * (90 + 50 * p->stock[g] / (p->max[g] ? p->max[g] : 1)) / 100; }

static void port_class(const struct port *p, char *b)
{
    for (int g = 0; g < 3; g++) b[g] = (p->type >> g & 1) ? 'B' : 'S';
    b[3] = 0;
}

/* ------------------------------------------------------------ files */
static void save_univ(void)
{
    FILE *f = fopen("UNIVERSE.DAT", "wb");
    if (!f) return;
    fwrite(&U, sizeof U, 1, f);
    fclose(f);
}

static void save_players(void)
{
    union prec r;
    FILE *f = fopen("PLAYERS.DAT", "wb");
    if (!f) return;
    for (int i = 0; i < np; i++) { memset(&r, 0, sizeof r); r.p = P[i]; fwrite(&r, sizeof r, 1, f); }
    fclose(f);
}

static int cmp_worth(const void *a, const void *b)
{
    long x = networth(&P[*(const int *)a]), y = networth(&P[*(const int *)b]);
    return y > x ? 1 : y < x ? -1 : 0;
}
static int ranking(int *idx)
{
    for (int i = 0; i < np; i++) idx[i] = i;
    qsort(idx, np, sizeof idx[0], cmp_worth);
    return np;
}

static void write_scores(void)
{
    int idx[MAXPL], n = ranking(idx);
    FILE *f = fopen("SCORES.TXT", "w");
    if (!f) return;
    fprintf(f, "RISC Wars - top traders by net worth\n");
    for (int i = 0; i < n && i < 5; i++)
        fprintf(f, " %d. %-20.20s %-18.18s %13s cr\n", i + 1, P[idx[i]].name, P[idx[i]].ship, num(networth(&P[idx[i]])));
    fclose(f);
}

static void save_all(void) { save_univ(); save_players(); write_scores(); }

static void load_players(void)
{
    union prec r;
    np = 0;
    FILE *f = fopen("PLAYERS.DAT", "rb");
    if (!f) return;
    while (np < MAXPL && fread(&r, sizeof r, 1, f) == 1) P[np++] = r.p;
    fclose(f);
}

static void link2(int a, int b)
{
    if (a == b || adjacent(a, b)) return;
    if (U.s[a].nwarps < MAXWARP) U.s[a].warp[U.s[a].nwarps++] = (uint8_t)b;
    if (U.s[b].nwarps < MAXWARP && !adjacent(b, a)) U.s[b].warp[U.s[b].nwarps++] = (uint8_t)a;
}

/* the Big Bang: deterministic, a few milliseconds */
static void generate(void)
{
    memset(&U, 0, sizeof U);
    memcpy(U.magic, "RISCWAR1", 8);
    lcg_state = 1989;
    for (int s = 0; s <= NSECT; s++) { U.s[s].port = -1; U.s[s].figowner = -1; }
    /* Acorn Space is a little chain around the station */
    for (int s = 2; s <= ACORN_SPACE; s++) link2(s, s <= 5 ? 1 : s - 3);
    /* a spanning tree, then shortcuts (a few one-way) */
    for (int s = ACORN_SPACE + 1; s <= NSECT; s++) {
        int t;
        do t = s - 1 - lrnd(s - 1 > 12 ? 12 : s - 1); while (t < 1 || U.s[t].nwarps >= MAXWARP - 1);
        link2(s, t);
    }
    for (int k = 0; k < 110; k++) {
        int a = 1 + lrnd(NSECT), b = 1 + lrnd(NSECT);
        if (a <= ACORN_SPACE || b <= ACORN_SPACE) continue;
        if (U.s[a].nwarps >= MAXWARP || adjacent(a, b) || a == b) continue;
        if (lrnd(6) == 0) U.s[a].warp[U.s[a].nwarps++] = (uint8_t)b;        /* one-way lane */
        else link2(a, b);
    }
    for (int s = 1; s <= NSECT; s++) {         /* sorted warp lists read better */
        struct sector *x = &U.s[s];
        for (int i = 0; i < x->nwarps; i++) for (int j = i + 1; j < x->nwarps; j++)
            if (x->warp[j] < x->warp[i]) { uint8_t t = x->warp[i]; x->warp[i] = x->warp[j]; x->warp[j] = t; }
    }
    /* ports: none in sector 1 (the station and the Fab) or at the derelict */
    for (int i = 0; i < NPORT; i++) {
        int s;
        do s = 2 + lrnd(NSECT - 1); while (U.s[s].port >= 0 || s == X2_SECTOR);
        if (i == 0) s = 3;                     /* a port and a buyer next to home */
        if (i == 1) s = 5;
        if (U.s[s].port >= 0) continue;
        struct port *p = &U.p[i];
        p->sector = (uint8_t)s;
        p->name = (uint8_t)i;
        p->type = (uint8_t)(1 + lrnd(6));
        if (i == 0) p->type = 6;               /* sells Silicon, buys Firmware and Coffee */
        if (i == 1) p->type = 1;               /* buys Silicon */
        for (int g = 0; g < 3; g++) { p->max[g] = (int16_t)(500 + 250 * lrnd(11)); p->stock[g] = (int16_t)(p->max[g] * (60 + lrnd(41)) / 100); }
        U.s[s].port = (int8_t)i;
    }
}

static void add_regulars(void)
{
    lcg_state = 17;
    for (int i = 0; i < NREG && np < MAXPL; i++) {
        struct player *p = &P[np];
        memset(p, 0, sizeof *p);
        snprintf(p->name, sizeof p->name, "%s", regulars[i].name);
        snprintf(p->ship, sizeof p->ship, "%s", regulars[i].ship);
        p->credits = regulars[i].credits;
        p->holds = (int16_t)regulars[i].holds;
        p->fighters = regulars[i].figs / 2;
        p->bot = 1;
        p->style = (uint8_t)regulars[i].style;
        p->sector = (int16_t)(ACORN_SPACE + 1 + lrnd(NSECT - ACORN_SPACE));
        p->turns = TURNS;
        /* the other half of their fighters guard a sector of their own */
        int s;
        do s = ACORN_SPACE + 1 + lrnd(NSECT - ACORN_SPACE); while (U.s[s].figowner >= 0 || s == X2_SECTOR);
        U.s[s].figowner = (int8_t)np;
        U.s[s].figs = (uint16_t)(regulars[i].figs - p->fighters);
        np++;
    }
}

static int load_univ(void)
{
    FILE *f = fopen("UNIVERSE.DAT", "rb");
    if (!f) return 0;
    int ok = fread(&U, sizeof U, 1, f) == 1 && !memcmp(U.magic, "RISCWAR1", 8);
    fclose(f);
    return ok;
}

/* ------------------------------------------------------------ the regulars' day */
static void kill_ship(struct player *p, const char *by, int where)
{
    p->cargo[0] = p->cargo[1] = p->cargo[2] = 0;
    p->fighters = 0;
    p->losses++;
    p->sector = 1;
    if (!p->bot) snprintf(p->msg, sizeof p->msg, "%s destroyed your ship in sector %d.", by, where);
}

/* fighters vs fighters: returns 1 if the attacker wins; both counts updated */
static int battle(int32_t *att, int32_t *def)
{
    while (*att > 0 && *def > 0) {
        int32_t n = (*att < *def ? *att : *def) / 4 + 1;
        for (int32_t i = 0; i < n && *att > 0 && *def > 0; i++) { if (door_rnd(100) < 52) (*def)--; else (*att)--; }
    }
    return *def == 0;
}

static int random_open_sector(void)
{
    for (int k = 0; k < 40; k++) {
        int s = ACORN_SPACE + 1 + door_rnd(NSECT - ACORN_SPACE);
        if (s != X2_SECTOR) return s;
    }
    return ACORN_SPACE + 1;
}

static void regular_day(int i, int headline)
{
    struct player *p = &P[i];
    int trips = 3 + door_rnd(4), per = 6 + door_rnd(10);
    if (p->style == 3) { trips = 1 + door_rnd(2); per = 3 + door_rnd(4); }     /* the PCjr is not fast */
    long profit = (long)trips * p->holds * per;
    p->credits += profit;
    int a = door_rnd(NPORT), b = door_rnd(NPORT), g = door_rnd(3);
    if (headline && profit > 0)
        door_news("%s hauled %s from %s to %s and cleared %s credits.", p->name, goods[g],
                  portnames[U.p[a].name], portnames[U.p[b].name], num(profit));
    /* spend: holds first, then fighters */
    while (p->holds < MAXHOLDS && p->credits > 60000 && p->style != 1) { p->credits -= 200 + 10 * p->holds; p->holds++; }
    int32_t buy = p->style == 1 ? p->credits / 3 / FIG_PRICE : p->credits / 20 / FIG_PRICE;
    if (buy > 400) buy = 400;
    p->fighters += buy;
    p->credits -= buy * FIG_PRICE;
    /* claim or raid a sector */
    if (p->fighters > 60 && door_rnd(p->style == 1 ? 1 : 3) == 0) {
        int s = random_open_sector();
        struct sector *x = &U.s[s];
        int32_t send = p->fighters / 2;
        if (x->figowner >= 0 && x->figowner != i) {
            if (p->style != 1 && door_rnd(2)) goto park;
            int32_t def = x->figs, before = def;
            struct player *o = &P[(int)x->figowner];
            p->fighters -= send;
            if (battle(&send, &def)) {
                door_news("%s's fighters wiped out %s of %s's fighters in sector %d.", p->name, num(before), o->name, s);
                if (!o->bot) snprintf(o->msg, sizeof o->msg, "%s destroyed your %s fighters in sector %d.", p->name, num(before), s);
                x->figowner = (int8_t)i; x->figs = (uint16_t)(send > 60000 ? 60000 : send);
            } else {
                x->figs = (uint16_t)def;
                if (headline) door_news("%s lost %s fighters attacking %s's sector %d.", p->name, num(p->fighters / 2 + 1), o->name, s);
            }
        } else if (x->figowner == i || x->figowner < 0) {
            p->fighters -= send;
            long t = (long)x->figs + send; x->figs = (uint16_t)(t > 60000 ? 60000 : t);
            x->figowner = (int8_t)i;
        }
    }
park:
    p->sector = (int16_t)random_open_sector();
    /* pirates look for parked ships */
    if (p->style == 1 && door_rnd(3) == 0) {
        for (int k = 0; k < np; k++) {
            struct player *o = &P[k];
            if (k == i || o->sector <= ACORN_SPACE || o->fighters >= p->fighters / 2) continue;
            int32_t att = p->fighters, def = o->fighters;
            if (battle(&att, &def)) {
                p->fighters = att;
                int32_t loot = o->credits / 10;
                o->credits -= loot; p->credits += loot; p->kills++;
                door_news("%s destroyed %s's ship \"%s\" in sector %d!", p->name, o->name, o->ship, o->sector);
                kill_ship(o, p->name, o->sector);
            } else { p->fighters = att; o->fighters = def; }
            break;
        }
    }
    p->turns = TURNS;
}

static void new_day(int today)
{
    int days = U.lastday ? today - U.lastday : 1;
    if (days < 1 || days > CATCHUP) days = days < 1 ? 1 : CATCHUP;
    for (int d = 0; d < days; d++) {
        /* ports restock a third of the way back */
        for (int i = 0; i < NPORT; i++) for (int g = 0; g < 3; g++) {
            struct port *p = &U.p[i];
            p->stock[g] = (int16_t)(p->stock[g] + (p->max[g] - p->stock[g]) / 3);
        }
        int heads = 0, star = 0;
        for (int k = 0; k < 50; k++) { star = door_rnd(np); if (P[star].bot) break; }
        for (int i = 0; i < np; i++) if (P[i].bot) regular_day(i, i == star || (heads < 2 && door_rnd(3) == 0) ? (heads++, 1) : 0);
    }
    U.lastday = today;
    save_all();
}

/* ------------------------------------------------------------ screen bits */
static void bar(void)
{
    char a[96], b[96];
    if (me < 0) { door_bar(0x1F, " RISC WARS  -  a space trading door for The ARM Pit", ""); return; }
    struct player *p = &P[me];
    snprintf(a, sizeof a, " RISC WARS   %-22.22s Sector %-4d Turns %-4d Credits %s", p->name, p->sector, p->turns, num(p->credits));
    snprintf(b, sizeof b, " Holds %d/%d  Si %d Fw %d Co %d  Fighters %s   Time left %lu", cargo_total(p), p->holds,
             p->cargo[0], p->cargo[1], p->cargo[2], num(p->fighters), (unsigned long)sio_minutes_left());
    door_bar(0x1F, a, b);
}

static void title(void)
{
    sio_cls();
    sio_puts("\n@X09        .       *            .          +         .        *        .\n");
    sio_puts("   @X0F\xDB\xDB\xDB\xDB  \xDB\xDB\xDB  \xDB\xDB\xDB\xDB  \xDB\xDB\xDB\xDB    \xDB   \xDB  \xDB\xDB\xDB  \xDB\xDB\xDB\xDB   \xDB\xDB\xDB\xDB\n");
    sio_puts("   @X0B\xDB   \xDB  \xDB  \xDB     \xDB     @X0E   \xDB   \xDB \xDB   \xDB \xDB   \xDB \xDB\n");
    sio_puts("   @X0B\xDB\xDB\xDB\xDB   \xDB   \xDB\xDB\xDB  \xDB     @X0E   \xDB \xDB \xDB \xDB\xDB\xDB\xDB\xDB \xDB\xDB\xDB\xDB   \xDB\xDB\xDB\n");
    sio_puts("   @X03\xDB  \xDB   \xDB      \xDB \xDB     @X06   \xDB \xDB \xDB \xDB   \xDB \xDB  \xDB      \xDB\n");
    sio_puts("   @X03\xDB   \xDB \xDB\xDB\xDB \xDB\xDB\xDB\xDB   \xDB\xDB\xDB\xDB @X06    \xDB \xDB  \xDB   \xDB \xDB   \xDB \xDB\xDB\xDB\xDB\n");
    sio_puts("@X09      *        .        +      .          .      *         .      +\n\n");
    sio_puts("@X07   The year is 2089.  The old instruction sets have gone to war, and the lanes\n");
    sio_puts("   between the stars belong to whoever can keep a trader's holds full.  Buy\n");
    sio_puts("   @X0FSilicon@X07, @X0FFirmware@X07 and @X0FCoffee@X07 cheap, sell them dear, and leave fighters\n");
    sio_puts("   behind to tell everyone whose sector this is.\n\n");
    sio_puts("@X08   RISC Wars 1.0  \xFA  original door software for The ARM Pit BBS@X07\n");
}

static void show_sector(void)
{
    struct player *p = &P[me];
    struct sector *x = &U.s[p->sector];
    char cls[4];
    sio_printf("\n@X0ESector  @X0F: @X0B%d@X07", p->sector);
    if (p->sector == 1) sio_puts(" @X0Ain Acorn Space - @X0FAcorn Station@X0A and @X0FThe Fab@X07");
    else if (p->sector <= ACORN_SPACE) sio_puts(" @X0Ain Acorn Space (protected)@X07");
    else if (in_nebula(p->sector)) sio_puts(" @X0Din the Barrel Nebula@X07");
    sio_puts("\n");
    if (x->port >= 0) {
        struct port *pt = &U.p[(int)x->port];
        port_class(pt, cls);
        sio_printf("@X0EPort    @X0F: @X0B%s@X07, class @X0F%s@X07 (", portnames[pt->name], cls);
        for (int b = 1; b >= 0; b--) {
            int n = 0;
            for (int g = 0; g < 3; g++) if ((pt->type >> g & 1) == b) sio_printf("%s@X0F%s@X07", n++ ? ", " : b ? "buys " : "; sells " + (pt->type == 0 ? 2 : 0), goods[g]);
        }
        sio_puts(")\n");
    }
    if (p->sector == X2_SECTOR)
        sio_puts("@X0EDerelict@X0F: @X0Da tumbling ARM/AT motherboard the size of a moon, marked \"REV X2\"@X07\n");
    int ships = 0;
    for (int i = 0; i < np; i++) {
        if (i == me || P[i].sector != p->sector) continue;
        sio_printf("%s@X0F%s@X07's @X0B\"%s\"@X07 (%s fighters)\n", ships++ ? "          " : "@X0EShips   @X0F: ", P[i].name, P[i].ship, num(P[i].fighters));
    }
    if (x->figs && x->figowner >= 0)
        sio_printf("@X0EFighters@X0F: @X0C%s@X07 (%s)\n", num(x->figs), x->figowner == me ? "yours" : P[(int)x->figowner].name);
    sio_puts("@X0EWarps to@X0F: ");
    for (int i = 0; i < x->nwarps; i++) sio_printf("%s@X0B%d", i ? " @X07- " : "", x->warp[i]);
    sio_puts("@X07\n");
    bar();
}

/* the command prompt: a letter, or a sector number typed straight away (-> 1000+n) */
static int command(const char *valid)
{
    struct player *p = &P[me];
    sio_printf("\n@X0DCommand @X0F[@X0ETL=%d@X0F]@X0D:@X0B[%d]@X0D (?=Help)? @X0F", p->turns, p->sector);
    for (;;) {
        int c = sio_key();
        if (c >= '0' && c <= '9') {
            char b[6]; int n = 0;
            b[n++] = (char)c; sio_putc(c);
            for (;;) {
                int d = sio_key();
                if (d == '\r') break;
                if ((d == 8 || d == 127) && n) { n--; sio_puts("\b \b"); if (!n) break; continue; }
                if (d >= '0' && d <= '9' && n < 4) { b[n++] = (char)d; sio_putc(d); }
            }
            sio_puts("\n");
            if (!n) { sio_printf("@X0DCommand? @X0F"); continue; }
            b[n] = 0;
            return 1000 + atoi(b);
        }
        if (c == '\r') { sio_puts("\n"); return 'D'; }
        if (c < 32 || c > 126) continue;
        c = toupper(c);
        if (strchr(valid, c)) { sio_putc(c); sio_puts("\n"); sio_linecount = 0; return c; }
    }
}

static long ask_number(const char *q, long def, long max)
{
    char b[12];
    sio_printf("%s @X0F[%s]@X07? @X0F", q, num(def));
    sio_getline(b, 9, GL_DIGITS);
    long v = b[0] ? atol(b) : def;
    if (v < 0) v = 0;
    if (v > max) v = max;
    return v;
}

/* ------------------------------------------------------------ moving */
static int arrive(int to);

static int enemy_fighters(void)
{
    struct player *p = &P[me];
    struct sector *x = &U.s[p->sector];
    if (!x->figs || x->figowner < 0 || x->figowner == me) return 1;
    struct player *o = &P[(int)x->figowner];
    sio_printf("\n@X0C%s fighters belonging to %s block the warp lane!@X07\n", num(x->figs), o->name);
    sio_puts("@X0E(A)@X07ttack them  @X0E(R)@X07etreat to where you came from: @X0F");
    int k = sio_hotkey("AR");
    if (k == 'R' || p->fighters <= 0) {
        if (k == 'A') sio_puts("@X0CYou have no fighters to attack with!  You retreat.@X07\n");
        int back = p->prev ? p->prev : 1;
        sio_printf("@X0AYou back away to sector %d.@X07\n", back);
        p->sector = (int16_t)back;
        return 0;
    }
    int32_t att = p->fighters, def = x->figs, a0 = att, d0 = def;
    int won = battle(&att, &def);
    p->fighters = att;
    sio_printf("@X0AYou lost @X0F%s@X0A fighters; %s lost @X0F%s@X0A.@X07\n", num(a0 - att), o->name, num(d0 - def));
    if (won) {
        x->figs = 0; x->figowner = -1;
        sio_printf("@X0E%s's fighters are gone.  The sector is open.@X07\n", o->name);
        door_news("%s destroyed %s of %s's fighters in sector %d.", p->name, num(d0), o->name, p->sector);
        if (!o->bot) { char m[72]; snprintf(m, sizeof m, "%s destroyed your %s fighters in sector %d.", p->name, num(d0), p->sector); memcpy(o->msg, m, sizeof m); }
        p->kills++;
        save_all();
        return 1;
    }
    x->figs = (uint16_t)def;
    sio_puts("@X0CYour last fighter goes up in a puff of magic smoke, and the swarm turns on your\n");
    sio_puts("hull.  The escape pod fires and carries you home to Acorn Station.@X07\n");
    door_news("%s's ship \"%s\" was destroyed by %s's fighters in sector %d.", p->name, p->ship, o->name, p->sector);
    kill_ship(p, o->name, p->sector);
    p->msg[0] = 0;
    p->turns = p->turns > 20 ? p->turns - 20 : 0;
    save_all();
    sio_pause();
    return 0;
}

static void hazard(void)
{
    struct player *p = &P[me];
    int free = p->holds - cargo_total(p);
    switch (door_rnd(5)) {
    case 0: {
        int32_t n = 5 + door_rnd(20 + p->sector / 3), n0 = n;
        sio_printf("\n@X0CA swarm of %d Rogue Interrupts drops out of hyperspace!@X07\n", (int)n);
        if (p->fighters > 0) {
            int32_t f0 = p->fighters;
            battle(&p->fighters, &n);
            sio_printf("@X0AYour fighters fight them off.  You lost %s fighters.@X07\n", num(f0 - p->fighters));
            if (n > 0) goto steal;
            (void)n0;
            break;
        }
    steal:
        for (int g = 0; g < 3; g++) p->cargo[g] = (int16_t)(p->cargo[g] / 2);
        sio_puts("@X0CThey swarm your holds and make off with half your cargo.@X07\n");
        break;
    }
    case 1: {
        int g = door_rnd(3), n = 3 + door_rnd(10);
        if (n > free) n = free;
        if (n <= 0) { sio_puts("\n@X0BYou pass a drifting cargo pod, but your holds are full.@X07\n"); break; }
        p->cargo[g] = (int16_t)(p->cargo[g] + n);
        sio_printf("\n@X0BA drifting cargo pod!  You scoop up %d units of %s.@X07\n", n, goods[g]);
        break;
    }
    case 2:
        sio_puts("\n@X0DA line noise storm: {@#$%&*!  Your navicomputer reboots.  You lose 3 turns.@X07\n");
        p->turns = p->turns > 3 ? p->turns - 3 : 0;
        break;
    case 3:
        if (p->cargo[2] > 1) {
            sio_printf("\n@X0CByte Weevils got into the Coffee!  You lose %d units.@X07\n", p->cargo[2] / 2);
            p->cargo[2] = (int16_t)(p->cargo[2] / 2);
        } else sio_puts("\n@X08Something skitters across the hull.  Byte Weevils, looking for Coffee.  You have none.@X07\n");
        break;
    default: {
        static const char *const rumours[] = {
            "\"Ports restock overnight.  Buy low at dawn.\"",
            "\"Somebody saw a derelict out past sector 100.  Big as a moon.  Says REV X2 on it.\"",
            "\"Gordon Kessler is out hunting again.  Keep your fighters close.\"",
            "\"The Barrel Nebula scrambles long-range scanners.  Fly it by hand.\"",
            "\"Never type +++ in the navicomputer.  Just trust me.\"",
        };
        sio_printf("\n@X0BA passing trader hails you: %s@X07\n", rumours[door_rnd(5)]);
        break;
    }
    }
    sio_pause();
}

static void derelict(void)
{
    struct player *p = &P[me];
    if (p->x2) { sio_puts("@X08The derelict is quiet.  You already took what it had to give.@X07\n"); return; }
    sio_puts("\n@X0DYou match its tumble and drift in through a gap between two SIMM sockets.\n");
    sio_puts("Silkscreen letters as tall as a house read: @X0F\"EUROPA MICRO SYSTEMS  ENGINEERING\n");
    sio_puts("SAMPLE  REV X2  S/N 00017  NOT FOR SALE\"@X0D.  A keyboard is missing.  Somebody\n");
    sio_puts("kept it.  In the BIOS ROM you find a stash of credits from 1987.@X07\n");
    p->x2 = 1;
    p->credits += 17000;
    sio_puts("@X0EYou gain 17,000 credits!@X07\n");
    door_news("%s found the derelict prototype and 17,000 credits in sector %d.", p->name, X2_SECTOR);
    save_all();
    sio_pause();
}

static int arrive(int to)
{
    struct player *p = &P[me];
    if (p->turns <= 0) { sio_puts("@X0CYou are out of turns for today.  Come back tomorrow!@X07\n"); return 0; }
    p->turns--;
    p->prev = p->sector;
    p->sector = (int16_t)to;
    sio_printf("@X08<Warping to sector %d>@X07\n", to);
    if (to > ACORN_SPACE && door_rnd(14) == 0) hazard();
    if (!enemy_fighters()) { save_players(); return 0; }
    if (to == X2_SECTOR) derelict();
    save_players();
    return 1;
}

static int bfs(int from, int to, uint8_t *path)
{
    uint8_t prev[NSECT + 1], q[NSECT + 1];
    int h = 0, t = 0;
    memset(prev, 0, sizeof prev);
    prev[from] = (uint8_t)from; q[t++] = (uint8_t)from;
    while (h < t) {
        int s = q[h++];
        if (s == to) break;
        for (int i = 0; i < U.s[s].nwarps; i++) {
            int w = U.s[s].warp[i];
            if (!prev[w]) { prev[w] = (uint8_t)s; q[t++] = (uint8_t)w; }
        }
    }
    if (!prev[to]) return 0;
    int n = 0;
    for (int s = to; s != from; s = prev[s]) path[n++] = (uint8_t)s;
    for (int i = 0; i < n / 2; i++) { uint8_t c = path[i]; path[i] = path[n - 1 - i]; path[n - 1 - i] = c; }
    return n;
}

static void autopilot(int to)
{
    uint8_t path[NSECT];
    struct player *p = &P[me];
    int n = bfs(p->sector, to, path);
    if (!n) { sio_printf("@X0CThe navicomputer finds no route to sector %d.@X07\n", to); return; }
    sio_printf("@X0AThe shortest route (%d hops): @X0F%d", n, p->sector);
    for (int i = 0; i < n; i++) sio_printf(" @X07> @X0F%d", path[i]);
    sio_puts("@X07\n");
    if (!sio_yesno("Engage the autopilot", 1)) return;
    for (int i = 0; i < n; i++) {
        if (!arrive(path[i])) break;
        if (U.s[path[i]].port >= 0 && i < n - 1) sio_printf("@X08  (passing %s)@X07\n", portnames[U.p[(int)U.s[path[i]].port].name]);
    }
    show_sector();
}

static void move_to(int to)
{
    struct player *p = &P[me];
    if (to == 1989) {
        sio_puts("@X0DThe navicomputer dials 555-1989 instead.  It gets a busy signal: somebody is\n");
        sio_puts("already on the board, playing RISC Wars.@X07\n");
        return;
    }
    if (to < 1 || to > NSECT) { sio_printf("@X0CThere is no sector %d.  The universe ends at %d.@X07\n", to, NSECT); return; }
    if (to == p->sector) { show_sector(); return; }
    if (!adjacent(p->sector, to)) {
        sio_printf("@X0ESector %d is not next door.@X07  ", to);
        autopilot(to);
        return;
    }
    if (arrive(to)) show_sector(); else show_sector();
}

/* ------------------------------------------------------------ trading */
static void trade(void)
{
    struct player *p = &P[me];
    struct sector *x = &U.s[p->sector];
    if (x->port < 0) { sio_puts("@X0CThere is no port in this sector.@X07\n"); return; }
    if (p->turns <= 0) { sio_puts("@X0CYou are out of turns for today.@X07\n"); return; }
    struct port *pt = &U.p[(int)x->port];
    p->turns--;
    sio_cls();
    sio_printf("\n@X0EDocking at @X0F%s@X0E.@X07  A cheerful voice: \"Welcome, %s!\"\n\n", portnames[pt->name], p->name);
    sio_puts("@X0F Item       Status    Units    Price@X07\n");
    for (int g = 0; g < 3; g++) {
        int buys = pt->type >> g & 1;
        sio_printf(" @X0B%-9s @X0E%-8s @X0F%6d  @X0A%5d@X07  @X08(you have %d)@X07\n", goods[g], buys ? "Buying" : "Selling",
                   pt->stock[g], buys ? buy_price(pt, g) : sell_price(pt, g), p->cargo[g]);
    }
    sio_puts("\n");
    long gained = 0;
    /* sell first, then buy */
    for (int g = 0; g < 3; g++) {
        if (!(pt->type >> g & 1) || !p->cargo[g] || !pt->stock[g]) continue;
        int price = buy_price(pt, g);
        long max = p->cargo[g] < pt->stock[g] ? p->cargo[g] : pt->stock[g];
        sio_printf("@X07We are buying up to @X0F%d@X07 %s at @X0A%d@X07 each.  ", pt->stock[g], goods[g], price);
        long n = ask_number("How many do you sell", max, max);
        if (!n) continue;
        p->cargo[g] = (int16_t)(p->cargo[g] - n);
        pt->stock[g] = (int16_t)(pt->stock[g] - n);
        p->credits += n * price;
        gained += n * price;
        sio_printf("@X0AYou sell %s units for %s credits.@X07\n", num(n), num(n * price));
    }
    for (int g = 0; g < 3; g++) {
        if ((pt->type >> g & 1) || !pt->stock[g]) continue;
        int price = sell_price(pt, g), free = p->holds - cargo_total(p);
        long afford = p->credits / price, max = pt->stock[g];
        if (max > free) max = free;
        if (max > afford) max = afford;
        sio_printf("@X07We are selling up to @X0F%d@X07 %s at @X0A%d@X07 each.  ", pt->stock[g], goods[g], price);
        if (max <= 0) { sio_puts(free <= 0 ? "@X08(your holds are full)@X07\n" : "@X08(you can't afford any)@X07\n"); continue; }
        long n = ask_number("How many do you buy", max, max);
        if (!n) continue;
        p->cargo[g] = (int16_t)(p->cargo[g] + n);
        pt->stock[g] = (int16_t)(pt->stock[g] - n);
        p->credits -= n * price;
        sio_printf("@X0AYou load %s units for %s credits.@X07\n", num(n), num(n * price));
    }
    if (gained >= 20000) door_news("%s sold a hold full of goods at %s for %s credits.", p->name, portnames[pt->name], num(gained));
    save_all();
    bar();
    sio_puts("\n");
    sio_pause();
    show_sector();
}

/* ------------------------------------------------------------ the Fab */
static void fab(void)
{
    struct player *p = &P[me];
    if (p->sector != 1) { sio_puts("@X0CThe Fab is at Acorn Station, sector 1.@X07\n"); return; }
    for (;;) {
        sio_cls();
        sio_puts("\n@X0E  THE FAB  @X06- Acorn Station's shipyard, clean room and all-night diner@X07\n");
        sio_puts("@X08  On the airlock: \"NO FOOD, NO DRINK, NO STATIC.  THE COFFEE IS FOR SALE.\"@X07\n\n");
        long hold = 200 + 10L * p->holds;
        sio_printf("  @X0E(H)@X07 Cargo hold ............ @X0F%s@X07 credits each  (you have %d of %d max)\n", num(hold), p->holds, MAXHOLDS);
        sio_printf("  @X0E(F)@X07 Fighters .............. @X0F%d@X07 credits each  (you have %s)\n", FIG_PRICE, num(p->fighters));
        sio_printf("  @X0E(S)@X07 Long-range scanner .... @X0F%s@X07 credits      %s\n", num(SCAN_PRICE), p->scanner ? "@X0A(installed)@X07" : "");
        sio_printf("  @X0E(N)@X07 Rename your ship ...... free (now @X0B\"%s\"@X07)\n", p->ship);
        sio_puts("  @X0E(L)@X07 Leave the Fab\n");
        sio_printf("\n@X07You have @X0F%s@X07 credits.  Your choice: @X0F", num(p->credits));
        int k = sio_hotkey("HFSNL\r");
        if (k == 'L' || k == '\r') break;
        if (k == 'H') {
            long n = 0, c = 0;
            while (p->holds + n < MAXHOLDS && c + 200 + 10L * (p->holds + n) <= p->credits) { c += 200 + 10L * (p->holds + n); n++; }
            if (!n) { sio_puts("@X0CYou can't afford another hold (or you're at the maximum).@X07\n"); sio_pause(); continue; }
            long want = ask_number("How many holds", n > 5 ? 5 : n, n);
            long cost = 0;
            for (long i = 0; i < want; i++) cost += 200 + 10L * (p->holds + i);
            p->holds = (int16_t)(p->holds + want); p->credits -= cost;
            if (want) sio_printf("@X0AThe yard crew bolts on %s hold%s for %s credits.@X07\n", num(want), want == 1 ? "" : "s", num(cost));
        }
        if (k == 'F') {
            long max = p->credits / FIG_PRICE;
            if (max > 5000) max = 5000;
            long n = ask_number("How many fighters", max > 20 ? 20 : max, max);
            p->fighters += n; p->credits -= n * FIG_PRICE;
            if (n) sio_printf("@X0A%s fighters roll off the line.@X07\n", num(n));
        }
        if (k == 'S') {
            if (p->scanner) sio_puts("@X0BYou already have one.  It's the shiny thing on the roof.@X07\n");
            else if (p->credits < SCAN_PRICE) sio_puts("@X0CNot enough credits.@X07\n");
            else { p->credits -= SCAN_PRICE; p->scanner = 1; sio_puts("@X0AScanner installed.  Press S in space to use it.@X07\n"); }
        }
        if (k == 'N') {
            char b[24];
            sio_puts("@X0EShip name: @X0F");
            sio_getline(b, 22, 0);
            if (b[0]) snprintf(p->ship, sizeof p->ship, "%s", b);
        }
        save_all();
        bar();
        sio_pause();
    }
    show_sector();
}

/* ------------------------------------------------------------ other commands */
static void deploy(void)
{
    struct player *p = &P[me];
    struct sector *x = &U.s[p->sector];
    if (p->sector <= ACORN_SPACE) { sio_puts("@X0CAcorn Space is protected.  No fighters may be left here.@X07\n"); return; }
    if (x->figs && x->figowner != me) { sio_puts("@X0CSomebody else's fighters hold this sector.  Clear them first.@X07\n"); return; }
    sio_printf("@X07You have @X0F%s@X07 fighters aboard and @X0F%s@X07 in this sector.\n", num(p->fighters), num(x->figowner == me ? x->figs : 0));
    long have = p->fighters + (x->figowner == me ? x->figs : 0), max = have > 60000 ? 60000 : have;
    long n = ask_number("How many fighters should guard this sector", max, max);
    p->fighters = (int32_t)(have - n);
    x->figs = (uint16_t)n;
    x->figowner = n ? (int8_t)me : -1;
    if (n) sio_printf("@X0A%s fighters now guard sector %d in your name.@X07\n", num(n), p->sector);
    else sio_puts("@X0AYou take all your fighters back aboard.@X07\n");
    save_all();
    bar();
}

static void attack_ship(void)
{
    struct player *p = &P[me];
    int list[MAXPL], n = 0;
    for (int i = 0; i < np; i++) if (i != me && P[i].sector == p->sector) list[n++] = i;
    if (!n) { sio_puts("@X0CThere is nobody here to attack.@X07\n"); return; }
    if (p->sector <= ACORN_SPACE) { sio_puts("@X0CAcorn Station security frowns on that sort of thing.@X07\n"); return; }
    if (p->fighters <= 0) { sio_puts("@X0CAttack with what?  You have no fighters.@X07\n"); return; }
    if (p->turns <= 0) { sio_puts("@X0CYou are out of turns for today.@X07\n"); return; }
    for (int i = 0; i < n; i++) sio_printf("  @X0E%d@X07  %s's \"%s\" (%s fighters)\n", i + 1, P[list[i]].name, P[list[i]].ship, num(P[list[i]].fighters));
    long k = ask_number("Attack which (0 = none)", 0, n);
    if (!k) return;
    struct player *o = &P[list[k - 1]];
    p->turns--;
    int32_t a0 = p->fighters, d0 = o->fighters;
    int won = battle(&p->fighters, &o->fighters);
    sio_printf("@X0AYou lost @X0F%s@X0A fighters; %s lost @X0F%s@X0A.@X07\n", num(a0 - p->fighters), o->name, num(d0 - o->fighters));
    if (won) {
        int32_t loot = o->credits / 10;
        o->credits -= loot; p->credits += loot; p->kills++;
        sio_printf("@X0E\"%s\" breaks apart!  You salvage %s credits from the wreck.@X07\n", o->ship, num(loot));
        door_news("%s destroyed %s's ship \"%s\" in sector %d!", p->name, o->name, o->ship, p->sector);
        kill_ship(o, p->name, p->sector);
    } else {
        sio_puts("@X0CYour fighters are all gone, and theirs chase you off.@X07\n");
        if (!o->bot) { char m[72]; snprintf(m, sizeof m, "%s attacked you in sector %d, and lost.", p->name, p->sector); memcpy(o->msg, m, sizeof m); }
    }
    save_all();
    bar();
}

static void scan(void)
{
    struct player *p = &P[me];
    struct sector *x = &U.s[p->sector];
    if (!p->scanner) { sio_puts("@X0CYou have no long-range scanner.  The Fab sells them.@X07\n"); return; }
    if (in_nebula(p->sector)) { sio_puts("@X0DThe Barrel Nebula fills the scanner with snow.@X07\n"); return; }
    sio_puts("\n@X0ELong-range scan:@X07\n");
    for (int i = 0; i < x->nwarps; i++) {
        struct sector *y = &U.s[x->warp[i]];
        char cls[4] = "---";
        if (y->port >= 0) port_class(&U.p[(int)y->port], cls);
        sio_printf("  @X0BSector %3d@X07  port @X0F%s@X07  warps %d", x->warp[i], cls, y->nwarps);
        if (y->figs && y->figowner >= 0) sio_printf("  @X0C%s fighters (%s)@X07", num(y->figs), y->figowner == me ? "yours" : P[(int)y->figowner].name);
        sio_puts("\n");
    }
}

static void computer(void)
{
    char b[8];
    sio_puts("@X0EPlot a course to which sector? @X0F");
    sio_getline(b, 4, GL_DIGITS);
    if (!b[0]) return;
    int to = atoi(b);
    if (to < 1 || to > NSECT) { sio_puts("@X0CNo such sector.@X07\n"); return; }
    if (to == P[me].sector) { sio_puts("@X0AYou are already there.@X07\n"); return; }
    autopilot(to);
}

static void info(void)
{
    struct player *p = &P[me];
    sio_cls();
    sio_printf("\n@X0E  %s@X07, captain of the @X0B\"%s\"@X07\n\n", p->name, p->ship);
    sio_printf("  Sector ........ @X0F%d@X07\n", p->sector);
    sio_printf("  Turns left .... @X0F%d@X07 of %d today\n", p->turns, TURNS);
    sio_printf("  Credits ....... @X0F%s@X07\n", num(p->credits));
    sio_printf("  Holds ......... @X0F%d@X07 (%d used: %d Silicon, %d Firmware, %d Coffee)\n", p->holds, cargo_total(p), p->cargo[0], p->cargo[1], p->cargo[2]);
    sio_printf("  Fighters ...... @X0F%s@X07 aboard\n", num(p->fighters));
    int ns = 0; long nf = 0;
    for (int s = 1; s <= NSECT; s++) if (U.s[s].figowner == me) { ns++; nf += U.s[s].figs; }
    sio_printf("  Deployed ...... @X0F%s@X07 fighters in @X0F%d@X07 sector%s\n", num(nf), ns, ns == 1 ? "" : "s");
    sio_printf("  Scanner ....... @X0F%s@X07\n", p->scanner ? "yes" : "no");
    sio_printf("  Ships destroyed @X0F%d@X07, lost @X0F%d@X07\n", p->kills, p->losses);
    sio_printf("  Net worth ..... @X0F%s@X07 credits\n", num(networth(p)));
    sio_pause();
}

static void rankings(void)
{
    int idx[MAXPL], n = ranking(idx);
    sio_cls();
    sio_puts("\n@X0E  THE TRADERS OF THE KNOWN LANES@X07\n\n");
    sio_puts("@X0F Rank  Trader                Ship                    Net worth   Fighters@X07\n");
    sio_linecount = 4;
    for (int i = 0; i < n; i++) {
        struct player *p = &P[idx[i]];
        sio_printf("%s %3d.  %-21.21s %-20.20s %12s %10s@X07\n", idx[i] == me ? "@X0E" : "@X07", i + 1, p->name, p->ship, num(networth(p)), num(p->fighters));
        if (!sio_line_done()) break;
    }
    sio_pause();
}

static void news(void)
{
    static char lines[15][100];
    int n = 0;
    FILE *f = fopen("NEWS.TXT", "r");
    sio_cls();
    sio_puts("\n@X0E  THE GALACTIC BUS - news from the lanes@X07\n\n");
    if (f) {
        char b[100];
        while (fgets(b, sizeof b, f)) {
            char *nl = strpbrk(b, "\r\n"); if (nl) *nl = 0;
            if (!b[0]) continue;
            memmove(lines[0], lines[1], sizeof lines[0] * 14);
            snprintf(lines[14], sizeof lines[14], "%s", b);
            n++;
        }
        fclose(f);
    }
    if (n > 15) n = 15;
    for (int i = 15 - n; i < 15; i++) sio_printf("@X03 %.78s@X07\n", lines[i]);
    if (!n) sio_puts("@X03 All quiet in the lanes.@X07\n");
    sio_pause();
}

static void help(void)
{
    sio_cls();
    sio_puts("\n@X0E  RISC WARS - COMMANDS@X07\n\n");
    sio_puts("  @X0E<number>@X07  warp to that sector (the autopilot plots longer trips)\n");
    sio_puts("  @X0EM@X07  move           @X0EP@X07  trade at the port     @X0ED@X07  display the sector\n");
    sio_puts("  @X0EC@X07  course plotter @X0EF@X07  deploy fighters       @X0EA@X07  attack a ship\n");
    sio_puts("  @X0ES@X07  long-range scan @X0EB@X07  the Fab (sector 1)   @X0EI@X07  your ship\n");
    sio_puts("  @X0ER@X07  rankings       @X0EN@X07  the news              @X0EQ@X07  quit to the BBS\n\n");
    sio_puts("@X07  Buy where a port @X0FSells@X07, sell where a port @X0FBuys@X07.  Prices follow the stock:\n");
    sio_puts("  a port with plenty to sell sells cheap, a port that wants a lot pays well.\n");
    sio_printf("  Ports restock overnight.  You get %d turns a day; a warp or a docking costs one.\n", TURNS);
    sio_puts("  Fighters you leave in a sector guard it: others must fight their way through.\n");
    sio_puts("  Acorn Space (sectors 1-10) is protected.  The regulars fly while you sleep.\n");
    sio_pause();
}

/* ------------------------------------------------------------ main */
static void play(void)
{
    struct player *p = &P[me];
    show_sector();
    for (;;) {
        int k = command("MPDCFASBIRNQ?");
        if (k >= 1000) { move_to(k - 1000); continue; }
        switch (k) {
        case 'M': {
            char b[8];
            sio_puts("@X0EWarp to which sector? @X0F");
            sio_getline(b, 4, GL_DIGITS);
            if (b[0]) move_to(atoi(b));
            break;
        }
        case 'P': trade(); break;
        case 'D': show_sector(); break;
        case 'C': computer(); break;
        case 'F': deploy(); break;
        case 'A': attack_ship(); break;
        case 'S': scan(); break;
        case 'B': fab(); break;
        case 'I': info(); break;
        case 'R': rankings(); break;
        case 'N': news(); break;
        case '?': help(); break;
        case 'Q':
            if (sio_yesno("@X0EReturn to the BBS", 1)) return;
            break;
        }
        (void)p;
    }
}

static int enter(void)
{
    char b[24];
    for (int i = 0; i < np; i++) {
        if (strcasecmp(P[i].name, door_user)) continue;
        me = i;
        if (P[i].bot) {
            P[i].bot = 0;
            sio_printf("\n@X0BYour ship \"%s\" has been flying on autopilot for a while.\n", P[i].ship);
            sio_printf("Welcome back to the helm, @X0F%s@X0B.@X07\n", door_first(P[i].name, b, sizeof b));
        }
        return 0;
    }
    if (np >= MAXPL) { sio_puts("\n@X0CThe universe is full.  Ask the sysop to make room.@X07\n"); return -1; }
    sio_printf("\n@X0AA new trader docks at Acorn Station!  Welcome, @X0F%s@X0A.@X07\n", door_user);
    if (!sio_yesno("@X07Do you want to sign on as a trader", 1)) return -1;
    struct player *p = &P[np];
    memset(p, 0, sizeof *p);
    snprintf(p->name, sizeof p->name, "%s", door_user);
    snprintf(p->ship, sizeof p->ship, "%s's Clipper", door_first(door_user, b, 16));
    sio_printf("@X0EName your ship (Enter = \"%s\"): @X0F", p->ship);
    sio_getline(b, 22, 0);
    if (b[0]) snprintf(p->ship, sizeof p->ship, "%s", b);
    p->credits = 2000; p->holds = 30; p->fighters = 20; p->sector = 1; p->turns = TURNS; p->day = door_daynum();
    me = np++;
    door_news("%s signed on as a trader, flying the \"%s\".", p->name, p->ship);
    save_all();
    sio_puts("\n@X07The dockmaster hands you a data cartridge.  \"@X0FYour ship, 30 holds, 20 fighters\n");
    sio_puts("and 2,000 credits.  The port in sector 3 sells Silicon cheap; sector 5 buys it.\n");
    sio_puts("Don't die.@X07\"\n");
    return 1;
}

int main(int argc, char **argv)
{
    door_init(argc, argv, bar);
    if (setjmp(sio_drop) == 0) {
        title();
        int today = door_daynum();
        load_players();
        if (!load_univ()) {
            sio_puts("\n@X08(The universe is being created.  This takes a moment, not seven days.)@X07\n");
            generate();
            np = 0;
            add_regulars();
            U.lastday = today;
            save_all();
        } else if (U.lastday != today) {
            sio_puts("\n@X08(A new day dawns in the lanes.  The regulars are making their moves...)@X07\n");
            new_day(today);
        }
        if (enter() >= 0) {
            struct player *p = &P[me];
            if (p->day != today) { p->day = today; p->turns = TURNS; sio_printf("\n@X0EA new day: you have %d turns.@X07\n", TURNS); }
            if (p->msg[0]) { sio_printf("\n@X0CWhile you were away: %s@X07\n", p->msg); p->msg[0] = 0; }
            save_all();
            sio_pause();
            play();
            save_all();
        }
        sio_puts("\n@X0AYou return to the BBS.  Clear skies, trader!@X07\n");
    } else {
        if (me >= 0) save_all();
    }
    door_exit();
    return 0;
}
