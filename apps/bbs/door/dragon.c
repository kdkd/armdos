/* dragon.c - LEGEND OF THE RISC DRAGON, a BBS door game for The ARM Pit.
 *
 * Original game in the tradition of the daily-turns door RPGs of the late
 * 1980s (Seth Robinson's Legend of the Red Dragon, 1989, is the famous one):
 * a village, a forest full of monsters, shops, a bank, a healer, a master to
 * beat for each level, other callers on the scoreboard, a daily news file,
 * and a dragon at the end. All names, monsters and text are new.
 *
 * Launched by the BBS as   DRAGON.EXE <dir with DOOR.SYS or DORINFO1.DEF>
 * It opens the COM port named in the drop file itself (keeping DTR up), as
 * doors did; "DRAGON /L" plays locally.
 */
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <time.h>
#include <dos.h>
#include <armdos.h>
#include "../../term/lib/comm.h"
#include "../../term/lib/scr.h"
#include "../sio.h"

#define FIGHTS_PER_DAY 15
#define MAXPLAYERS 64

struct player {
    char name[36];
    int16_t level, hp, maxhp, str, def;
    int32_t exp, gold, bank;
    int8_t weapon, armor, dead, gems;
    int16_t fights, day, kills, deaths, dragons, masterwins;
    char lastplayed[10];
};
union prec { struct player p; char pad[128]; };

static struct player pl;
static int plnum = -1;
static char username[36] = "Local Player";
static int port, ansi = 1;
static long baud;
static int minutes = 60;

static const char *const weapons[] = { "Wooden Ruler", "Letter Opener", "Soldering Iron", "Keyboard Cable Whip",
    "Screwdriver of Opening", "Oscilloscope Probe", "Logic Analyzer", "Sword of Barrel Shifting",
    "Conditional Blade", "The Great Pipeline" };
static const int32_t wprice[] = { 0, 200, 1000, 3000, 10000, 30000, 100000, 300000, 1000000, 4000000 };
static const int16_t wpower[] = { 5, 10, 20, 30, 40, 60, 80, 120, 180, 250 };
static const char *const armors[] = { "Cardigan", "Pocket Protector", "Anti-static Strap", "Mouse Pad Buckler",
    "Lab Coat", "Faraday Mail", "Twisted-Pair Armor", "Plate of Metal Oxide", "Shield of 16 Registers", "Aegis of the Acorn" };
static const int16_t apower[] = { 1, 3, 10, 15, 25, 35, 50, 75, 100, 150 };
#define NWEAP 10

static const int32_t expneed[] = { 0, 100, 300, 800, 2000, 5000, 12000, 30000, 70000, 150000, 300000, 600000 };

struct monster { const char *name, *weapon; int hp, str; };
static const struct monster monsters[11][3] = {
    { { "Stray Pointer", "dangles at you", 8, 6 }, { "Byte Weevil", "nibbles", 10, 5 }, { "Lost Floppy Gremlin", "flaps its shutter", 12, 7 } },
    { { "Rogue Interrupt", "interrupts you", 25, 12 }, { "Segment Wraith", "wraps around", 28, 14 }, { "Bus Error Imp", "misaligns you", 22, 16 } },
    { { "Wild 8088", "attacks very, very slowly", 50, 22 }, { "Parity Hag", "flips a bit", 55, 25 }, { "Swarm of Bugs", "crawls into your code", 45, 28 } },
    { { "Carrier Dropper", "cuts the line", 90, 40 }, { "Line Noise Elemental", "sprays {@#%&", 80, 45 }, { "Null Modem Twins", "cross their wires", 100, 38 } },
    { { "GOTO Troll", "jumps you", 150, 60 }, { "Spaghetti Coder", "tangles you", 140, 66 }, { "Unterminated String", "goes on and on and on", 170, 55 } },
    { { "Stack Overflow Ogre", "pushes and pushes", 250, 90 }, { "Heap Fragment Horror", "splits you", 230, 95 }, { "Memory Leak Slime", "oozes", 270, 85 } },
    { { "CISC Behemoth", "decodes a 15-byte instruction at you", 400, 130 }, { "Microcode Golem", "stomps", 380, 140 }, { "Seg:Off Hydra", "bites with 64K heads", 420, 125 } },
    { { "Deadlock Wyvern", "waits for you, forever", 650, 190 }, { "Race Condition Twins", "hit you first. Or second", 600, 200 }, { "Thrashing Kraken", "pages you out", 700, 180 } },
    { { "Divide Overflow Djinn", "divides you by zero", 1000, 280 }, { "Undocumented Opcode", "does something weird", 950, 300 }, { "Cosmic Ray", "flips your bits", 900, 320 } },
    { { "Mainframe Titan", "bills you for CPU time", 1500, 400 }, { "Punch Card Lich", "folds, spindles and mutilates", 1400, 420 }, { "Tape Drive Colossus", "spins up", 1600, 380 } },
    { { "Vaporware Phantom", "announces an attack for next year", 2200, 550 }, { "Legal Department Wyrm", "sends a letter", 2400, 520 }, { "Big Blue Leviathan", "sets a standard", 2600, 600 } },
};
static const char *const masters[] = { "Old Man Christensen", "Sister Hayes", "Brother Kermit", "The Forsberg",
    "Magistrate Motorola", "Lady Sophie of Cambridge", "Sir Roger the Wise", "The Grand Pipeliner",
    "Warden Wilson", "The Acorn Archon", "The Hermit of Bit 31" };

static int today_num(void)
{
    time_t t = time(NULL); struct tm *tm = localtime(&t);
    return (tm->tm_year % 100) * 400 + tm->tm_yday;
}
static int rnd(int n) { return n > 0 ? rand() % n : 0; }

/* ------------------------------------------------------------ files */
static void news(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void news(const char *fmt, ...)
{
    char b[160];
    va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    FILE *f = fopen("DRAGNEWS.TXT", "a");
    if (!f) return;
    time_t t = time(NULL); struct tm *tm = localtime(&t);
    fprintf(f, "%02d-%02d-%02d  %s\n", tm->tm_mon + 1, tm->tm_mday, tm->tm_year % 100, b);
    fclose(f);
}

static void save_player(void)
{
    union prec r;
    if (!pl.name[0]) return;
    memset(&r, 0, sizeof r);
    r.p = pl;
    FILE *f = fopen("DRAGON.DAT", "r+b");
    if (!f) f = fopen("DRAGON.DAT", "w+b");
    if (!f) return;
    if (plnum < 0) { fseek(f, 0, SEEK_END); plnum = (int)(ftell(f) / (long)sizeof r); }
    fseek(f, (long)plnum * (long)sizeof r, SEEK_SET);
    fwrite(&r, sizeof r, 1, f);
    fclose(f);
}

static int load_players(struct player *all, int max)
{
    union prec r;
    int n = 0;
    FILE *f = fopen("DRAGON.DAT", "rb");
    if (!f) return 0;
    while (n < max && fread(&r, sizeof r, 1, f) == 1) all[n++] = r.p;
    fclose(f);
    return n;
}

static void read_dropfile(const char *dir)
{
    char p[100], line[80];
    snprintf(p, sizeof p, "%s\\DOOR.SYS", dir);
    FILE *f = fopen(p, "r");
    if (f) {
        for (int i = 1; fgets(line, sizeof line, f); i++) {
            char *nl = strpbrk(line, "\r\n"); if (nl) *nl = 0;
            if (i == 1) port = (line[0] == 'C' && line[3]) ? line[3] - '0' : 0;
            if (i == 2) baud = atol(line);
            if (i == 10) snprintf(username, sizeof username, "%s", line);
            if (i == 19) minutes = atoi(line);
            if (i == 20) ansi = !strcmp(line, "GR");
        }
        fclose(f);
        return;
    }
    snprintf(p, sizeof p, "%s\\DORINFO1.DEF", dir);
    f = fopen(p, "r");
    if (!f) return;
    char first[24] = "", last[24] = "";
    for (int i = 1; fgets(line, sizeof line, f); i++) {
        char *nl = strpbrk(line, "\r\n"); if (nl) *nl = 0;
        if (i == 4) port = atoi(line + 3);
        if (i == 5) baud = atol(line);
        if (i == 7) snprintf(first, sizeof first, "%s", line);
        if (i == 8) snprintf(last, sizeof last, "%s", line);
        if (i == 10) ansi = atoi(line) != 0;
        if (i == 12) minutes = atoi(line);
    }
    fclose(f);
    snprintf(username, sizeof username, "%s %s", first, last);
}

/* ------------------------------------------------------------ screens */
static void status(void)
{
    char b[96];
    scr_fill(0, 23, 80, 2, ' ', 0x2F);
    snprintf(b, sizeof b, " LEGEND OF THE RISC DRAGON   %-24.24s  Level %-2d  HP %d/%d", pl.name, pl.level, pl.hp, pl.maxhp);
    scr_puts(0, 23, 0x2F, b);
    snprintf(b, sizeof b, " Gold %-9ld Bank %-9ld Fights %-3d Exp %-8ld  Time left %lu", (long)pl.gold, (long)pl.bank,
             pl.fights, (long)pl.exp, (unsigned long)sio_minutes_left());
    scr_puts(0, 24, 0x2F, b);
    scr_cursor(sio_vt.x, sio_vt.y);
}

static void title(void)
{
    sio_cls();
    sio_puts("\n@X0C                    \xDC\xDC\xDC  @X0E L E G E N D   O F   T H E @X0C  \xDC\xDC\xDC\n");
    sio_puts("@X04          \xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\xDB\n");
    sio_puts("@X4F          \xDB  R I S C     D R A G O N  \xDB  @X4E~ a door for The ARM Pit ~  @X4F\xDB@X07\n");
    sio_puts("@X04          \xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF\xDF@X07\n\n");
    sio_puts("@X0A   Deep in the Silicon Caves beneath the village of Acorn Vale sleeps the\n");
    sio_puts("   @X0CRISC Dragon@X0A: thirty-two registers, no microcode, and a breath that\n");
    sio_puts("   executes in a single cycle.  The villagers need a hero.  It could be you.@X07\n\n");
}

static void show_stats(void)
{
    sio_cls();
    sio_printf("\n@X0F%s's stats@X07\n", pl.name);
    sio_puts("@X02\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD@X07\n");
    sio_printf("@X02 Level:        @X0F%d\n", pl.level);
    sio_printf("@X02 Experience:   @X0F%ld @X02(next level at %ld)\n", (long)pl.exp, pl.level < 12 ? (long)expneed[pl.level] : 0L);
    sio_printf("@X02 Hit points:   @X0F%d of %d\n", pl.hp, pl.maxhp);
    sio_printf("@X02 Strength:     @X0F%d  @X02Defence: @X0F%d\n", pl.str, pl.def);
    sio_printf("@X02 Weapon:       @X0F%s\n", weapons[pl.weapon]);
    sio_printf("@X02 Armour:       @X0F%s\n", armors[pl.armor]);
    sio_printf("@X02 Gold:         @X0F%ld  @X02In the bank: @X0F%ld\n", (long)pl.gold, (long)pl.bank);
    sio_printf("@X02 Gems:         @X0F%d\n", pl.gems);
    sio_printf("@X02 Forest fights left today: @X0F%d\n", pl.fights);
    sio_printf("@X02 Monsters slain: @X0F%d  @X02Deaths: @X0F%d  @X02Dragons: @X0F%d@X07\n", pl.kills, pl.deaths, pl.dragons);
    sio_pause();
}

static int attack_power(void) { return pl.str + wpower[pl.weapon]; }
static int defence(void) { return pl.def + apower[pl.armor]; }

static void die(const char *killer)
{
    sio_printf("\n@X0CYou have been slain by %s!@X07\n", killer);
    sio_printf("@X04You lose the %ld gold you were carrying.@X07  Your bank account is safe.\n", (long)pl.gold);
    sio_puts("@X08Come back tomorrow, adventurer.@X07\n");
    news("%s was slain by %s.", pl.name, killer);
    pl.gold = 0; pl.dead = 1; pl.deaths++; pl.hp = 0;
    save_player();
    sio_pause();
}

/* returns 1 won, 0 ran, -1 died */
static int fight(const char *mname, const char *mweapon, int mhp, int mstr, int canrun)
{
    int hp = mhp;
    sio_printf("\n@X0AYou have encountered @X0F%s@X0A!@X07\n", mname);
    for (;;) {
        if (hp == mhp) sio_puts("\n@X02Your skill allows you to get the first strike.@X07\n");
        sio_printf("@X0AYour hit points: @X0F%d@X0A   %s's hit points: @X0F%d@X07\n", pl.hp, mname, hp);
        sio_printf("@X02(@X0AA@X02)ttack  %s@X02(@X0AS@X02)tats @X0F: ", canrun ? "@X02(@X0AR@X02)un  " : "");
        int k = sio_hotkey(canrun ? "ARS" : "AS");
        if (k == 'S') { show_stats(); continue; }
        if (k == 'R') {
            if (rnd(3)) { sio_puts("@X0AYou run away like a coward.  It works!@X07\n"); return 0; }
            sio_printf("@X0CYou try to run, but %s blocks your way!@X07\n", mname);
        } else {
            int dmg = attack_power() / 2 + rnd(attack_power() / 2 + 1);
            if (rnd(12) == 0) { dmg *= 2; sio_puts("@X0E** POWER MOVE **@X07  "); }
            hp -= dmg;
            sio_printf("@X0AYou hit %s for @X0F%d@X0A damage!@X07\n", mname, dmg);
            if (hp <= 0) return 1;
        }
        int mdmg = mstr / 2 + rnd(mstr / 2 + 1) - defence();
        if (mdmg <= 0) sio_printf("@X02%s %s, but misses.@X07\n", mname, mweapon);
        else {
            pl.hp -= mdmg;
            sio_printf("@X0C%s %s for @X0F%d@X0C damage!@X07\n", mname, mweapon, mdmg);
        }
        status();
        if (pl.hp <= 0) return -1;
    }
}

static void check_level(void)
{
    if (pl.level < 12 && pl.exp >= expneed[pl.level])
        sio_printf("@X0EYou have enough experience to challenge %s at the Training Hall!@X07\n", masters[pl.level - 1]);
}

static void forest_event(void)
{
    int e = rnd(5);
    if (e == 0) {
        int g = 50 * pl.level * pl.level + rnd(100);
        sio_printf("\n@X0EYou find a bag containing @X0F%d@X0E gold pieces under a pile of old 5\xAB\" floppies!@X07\n", g);
        pl.gold += g;
    } else if (e == 1) {
        sio_puts("\n@X0DYou find a sparkling gem!  It looks like a 1 MB SIMM.@X07\n");
        pl.gems++;
    } else if (e == 2) {
        sio_puts("\n@X0BAn old hacker sits on a stump, typing on a luggable.  \"Here, drink this,\"\n");
        sio_puts("he says, handing you a lukewarm cola.  You feel much better!@X07\n");
        pl.hp = pl.maxhp;
    } else if (e == 3) {
        sio_puts("\n@X0BA fairy made of pure line noise flies by: \"+++ATH0!\"  You feel refreshed and\n");
        sio_puts("gain an extra forest fight.@X07\n");
        pl.fights++;
    } else {
        int x = 5 * pl.level * pl.level;
        sio_printf("\n@X0BYou find a manual that nobody has ever read.  You read it.  You gain @X0F%d@X0B experience!@X07\n", x);
        pl.exp += x;
    }
    save_player();
    sio_pause();
}

static void forest(void)
{
    for (;;) {
        sio_cls();
        sio_puts("\n@X0A  THE SILICON FOREST@X07\n");
        sio_puts("@X02  The trees hum at 60 Hz.  Somewhere, a disk drive grinds.@X07\n\n");
        sio_puts("  @X02(@X0AL@X02)ook for something to kill\n  (@X0AH@X02)ealer's hut\n  (@X0AR@X02)eturn to town\n  (@X0AV@X02)iew your stats@X07\n");
        sio_printf("\n@X02The Forest @X0F(%d fights left, HP %d/%d)@X02 [@X0AL H R V@X02]: @X0F", pl.fights, pl.hp, pl.maxhp);
        int k = sio_hotkey("LHRV");
        if (k == 'R') return;
        if (k == 'V') { show_stats(); continue; }
        if (k == 'H') {
            int cost = (pl.maxhp - pl.hp) * pl.level * 2;
            if (pl.hp >= pl.maxhp) { sio_puts("@X0AYou're already fit as a fiddle.@X07\n"); sio_pause(); continue; }
            sio_printf("@X0AThe healer will patch you up for @X0F%d@X0A gold.@X07\n", cost);
            if (pl.gold >= cost) { if (sio_yesno("Pay", 1)) { pl.gold -= cost; pl.hp = pl.maxhp; save_player(); } }
            else { sio_puts("@X0CYou can't afford it.@X07\n"); sio_pause(); }
            continue;
        }
        if (pl.fights <= 0) { sio_puts("@X0CYou are too tired to fight any more today.  Come back tomorrow!@X07\n"); sio_pause(); continue; }
        pl.fights--;
        if (rnd(8) == 0) { forest_event(); continue; }
        const struct monster *m = &monsters[pl.level - 1 < 10 ? pl.level - 1 : 10][rnd(3)];
        int mhp = m->hp * (85 + rnd(31)) / 100, mstr = m->str * (85 + rnd(31)) / 100;
        int r = fight(m->name, m->weapon, mhp, mstr, 1);
        if (r < 0) { die(m->name); return; }
        if (r > 0) {
            int32_t g = (int32_t)(m->hp * 2 + rnd(m->hp)), x = (int32_t)(m->hp / 2 + rnd(m->hp / 2 + 1)) * (pl.level > 1 ? 2 : 1);
            sio_printf("\n@X0AYou have killed %s!@X07\n", m->name);
            sio_printf("@X02You receive @X0F%ld@X02 gold and @X0F%ld@X02 experience.@X07\n", (long)g, (long)x);
            pl.gold += g; pl.exp += x; pl.kills++;
            check_level();
            save_player();
            sio_pause();
        }
    }
}

static void shop(int armour)
{
    const char *const *names = armour ? armors : weapons;
    const int16_t *power = armour ? apower : wpower;
    int8_t *have = armour ? &pl.armor : &pl.weapon;
    for (;;) {
        sio_cls();
        sio_puts(armour ? "\n@X0E  DORIS'S DISCOUNT SHIELDING@X07\n@X06  \"If it stops a static zap, it'll stop a sword.\"@X07\n\n"
                        : "\n@X0E  HACKER HANK'S HARDWARE@X07\n@X06  \"Weapons, tools, and everything in between.\"@X07\n\n");
        for (int i = 1; i < NWEAP; i++)
            sio_printf("  @X0F%d@X06. @X0E%-28s@X06 %9ld gold  %s\n", i, names[i], (long)wprice[i], i == *have ? "@X0A(yours)@X07" : "");
        sio_printf("\n@X06You have @X0F%ld@X06 gold and a @X0F%s@X06 (power %d).@X07\n", (long)pl.gold, names[(int)*have], power[(int)*have]);
        sio_puts("@X06Buy which (Enter = leave): @X0F");
        char b[4];
        sio_getline(b, 2, GL_DIGITS);
        if (!b[0]) return;
        int i = atoi(b);
        if (i < 1 || i >= NWEAP) continue;
        if (i == *have) { sio_puts("@X0CYou already own that one.@X07\n"); sio_pause(); continue; }
        int32_t trade = wprice[(int)*have] / 2;
        if (pl.gold + trade < wprice[i]) { sio_puts("@X0C\"Come back when you have the money, pal.\"@X07\n"); sio_pause(); continue; }
        sio_printf("@X06Trade in your %s for @X0F%ld@X06 gold and buy the %s", names[(int)*have], (long)trade, names[i]);
        if (sio_yesno("", 1)) {
            pl.gold += trade - wprice[i];
            *have = (int8_t)i;
            save_player();
            sio_puts("@X0A\"Pleasure doing business with you.\"@X07\n");
            sio_pause();
        }
    }
}

static void bank(void)
{
    char b[12];
    for (;;) {
        sio_cls();
        sio_puts("\n@X0E  FIRST BANK OF ACORN VALE@X07\n@X06  Gold in the bank is safe if you die.@X07\n\n");
        sio_printf("  @X06On hand: @X0F%ld@X06   In the bank: @X0F%ld@X07\n\n", (long)pl.gold, (long)pl.bank);
        sio_puts("  @X06(@X0ED@X06)eposit all   (@X0EW@X06)ithdraw   (@X0EL@X06)eave: @X0F");
        int k = sio_hotkey("DWL");
        if (k == 'L') return;
        if (k == 'D') { pl.bank += pl.gold; pl.gold = 0; }
        if (k == 'W') {
            sio_puts("@X06How much (Enter = all): @X0F");
            sio_getline(b, 9, GL_DIGITS);
            int32_t v = b[0] ? atol(b) : pl.bank;
            if (v > pl.bank) v = pl.bank;
            pl.bank -= v; pl.gold += v;
        }
        save_player();
    }
}

static void level_up(void)
{
    int hp = 10 + rnd(8) + pl.level * 3, st = 5 + rnd(4) + pl.level, df = 2 + rnd(3);
    pl.level++;
    pl.maxhp += hp; pl.hp = pl.maxhp; pl.str += st; pl.def += df;
    pl.masterwins++;
    sio_printf("\n@X0E*** You are now level %d! ***@X07\n", pl.level);
    sio_printf("@X0AHit points +%d, strength +%d, defence +%d.@X07\n", hp, st, df);
    news("%s defeated %s and is now level %d.", pl.name, masters[pl.level - 2], pl.level);
    save_player();
}

static int fought_master_today;
static void training(void)
{
    sio_cls();
    sio_puts("\n@X0E  THE TRAINING HALL@X07\n\n");
    if (pl.level >= 12) { sio_puts("@X0AYou have learned all there is to learn.  Only the dragon remains.@X07\n"); sio_pause(); return; }
    const char *m = masters[pl.level - 1];
    sio_printf("@X06Your master is @X0F%s@X06.@X07\n", m);
    if (pl.exp < expneed[pl.level]) {
        sio_printf("@X0B\"You need %ld more experience before you're ready for me.\"@X07\n", (long)(expneed[pl.level] - pl.exp));
        sio_pause();
        return;
    }
    if (fought_master_today) { sio_puts("@X0B\"Once a day is enough.  Go rest.\"@X07\n"); sio_pause(); return; }
    if (!sio_yesno("@X06Challenge your master", 1)) return;
    fought_master_today = 1;
    const struct monster *ref = &monsters[pl.level - 1][1];
    int r = fight(m, "strikes with a well-worn manual", ref->hp * 3 / 2, ref->str * 5 / 4, 0);
    if (r > 0) {
        sio_printf("\n@X0B\"Well done!  You have beaten me.  Go now, and be %s no longer.\"@X07\n", pl.level == 1 ? "a beginner" : "my student");
        level_up();
        sio_pause();
    } else {
        pl.hp = 1;
        sio_printf("\n@X0B%s spares your life.  \"Not yet, not yet.\"@X07\n", m);
        save_player();
        sio_pause();
    }
}

static int cmp_players(const void *a, const void *b)
{
    const struct player *x = a, *y = b;
    if (x->dragons != y->dragons) return y->dragons - x->dragons;
    if (x->level != y->level) return y->level - x->level;
    return y->exp > x->exp ? 1 : y->exp < x->exp ? -1 : 0;
}

static void scores(void)
{
    static struct player all[MAXPLAYERS];
    int n = load_players(all, MAXPLAYERS);
    qsort(all, n, sizeof all[0], cmp_players);
    sio_cls();
    sio_puts("\n@X0E  HEROES OF ACORN VALE@X07\n\n");
    sio_puts("@X0F   Name                        Level   Experience  Kills  Dragons  Status@X07\n");
    sio_linecount = 4;
    for (int i = 0; i < n; i++) {
        sio_printf("@X0E%2d@X07 %-27.27s %5d %12ld %6d %8d  %s\n", i + 1, all[i].name, all[i].level, (long)all[i].exp,
                   all[i].kills, all[i].dragons, all[i].dead ? "@X0Cdead@X07" : "@X0Aalive@X07");
        if (!sio_line_done()) break;
    }
    sio_pause();
}

static void daily_news(void)
{
    char lines[16][120];
    int n = 0;
    FILE *f = fopen("DRAGNEWS.TXT", "r");
    sio_cls();
    sio_puts("\n@X0E  THE ACORN VALE DAILY NEWS@X07\n\n");
    if (f) {
        char b[120];
        while (fgets(b, sizeof b, f)) { char *nl = strpbrk(b, "\r\n"); if (nl) *nl = 0; memmove(lines[0], lines[1], sizeof lines[0] * 15); snprintf(lines[15], 120, "%s", b); n++; }
        fclose(f);
    }
    if (n > 16) n = 16;
    for (int i = 16 - n; i < 16; i++) sio_printf("@X02 %s@X07\n", lines[i]);
    if (!n) sio_puts("@X02 Nothing happened.  Nothing at all.@X07\n");
    sio_pause();
}

static void inn(void)
{
    sio_cls();
    sio_puts("\n@X0E  THE FLOATING POINT INN@X07\n");
    sio_puts("@X06  The fire crackles.  Someone in the corner is arguing about RISC vs CISC.@X07\n\n");
    static const char *const rumours[] = {
        "\"They say the dragon has thirty-two registers.  THIRTY-TWO.\"",
        "\"Deposit your gold before you go into the forest.  Trust me.\"",
        "\"Hank's Barrel Shifting sword is worth every coin.\"",
        "\"The Hermit of Bit 31 is the last master.  Nobody knows his sign.\"",
        "\"My cousin dialed 555-1989 once.  Never came back.  Well, he logs on every night.\"",
        "\"You can't run from your master.  Don't even try.\"",
    };
    sio_printf("@X0BThe bartender leans over: %s@X07\n\n", rumours[rnd(6)]);
    if (pl.gems >= 2 && sio_yesno("@X06Trade 2 gems for a potion of +1 strength", 1)) { pl.gems -= 2; pl.str++; save_player(); }
    sio_pause();
}

static void dragon(void)
{
    sio_cls();
    if (pl.level < 12) { sio_puts("\n@X0CYou wouldn't last a single clock cycle.  Reach level 12 first.@X07\n"); sio_pause(); return; }
    sio_puts("\n@X0CYou descend into the Silicon Caves.  The air smells of hot solder.\n");
    sio_puts("Two red LEDs open in the darkness...@X07\n");
    int r = fight("the RISC Dragon", "breathes a single-cycle flame", 4000, 700, 0);
    if (r < 0) { die("the RISC Dragon"); return; }
    sio_puts("\n@X0E*** YOU HAVE SLAIN THE RISC DRAGON! ***@X07\n");
    sio_puts("@X0AThe village cheers.  Bards write songs.  Your name goes on the scoreboard, and\n");
    sio_puts("you begin again as a humble level 1 adventurer, wiser than before.@X07\n");
    news("*** %s HAS SLAIN THE RISC DRAGON! ***", pl.name);
    pl.dragons++;
    pl.level = 1; pl.exp = 0; pl.maxhp = 20 + pl.dragons * 5; pl.hp = pl.maxhp; pl.str = 10 + pl.dragons * 2; pl.def = 1;
    pl.weapon = 0; pl.armor = 0; pl.gold = 0;
    save_player();
    sio_pause();
}

static void town(void)
{
    for (;;) {
        status();
        sio_cls();
        sio_puts("\n@X0A  ACORN VALE - TOWN SQUARE@X07\n");
        sio_puts("@X02  The square is quiet.  The clock tower strikes 25 MHz.@X07\n\n");
        sio_puts("  @X02(@X0AF@X02)orest                  (@X0AT@X02)raining Hall\n");
        sio_puts("  @X02(@X0AW@X02)eapons - Hacker Hank   (@X0AA@X02)rmour - Doris\n");
        sio_puts("  @X02(@X0AH@X02)ealer's hut            (@X0AB@X02)ank\n");
        sio_puts("  @X02(@X0AI@X02)nn                     (@X0AV@X02)iew your stats\n");
        sio_puts("  @X02(@X0AP@X02)layer rankings         (@X0AD@X02)aily news\n");
        sio_puts("  @X02(@X0AS@X02)lay the RISC Dragon    (@X0AQ@X02)uit to the BBS@X07\n");
        sio_printf("\n@X02Town Square @X0F(%lu min left)@X02 [@X0AF T W A H B I V P D S Q@X02]: @X0F", (unsigned long)sio_minutes_left());
        int k = sio_hotkey("FTWAHBIVPDSQ");
        switch (k) {
        case 'F': forest(); if (pl.dead) return; break;
        case 'T': training(); break;
        case 'W': shop(0); break;
        case 'A': shop(1); break;
        case 'H': {
            int cost = (pl.maxhp - pl.hp) * pl.level * 2;
            if (pl.hp >= pl.maxhp) sio_puts("@X0AThe healer says you look fine.@X07\n");
            else if (pl.gold < cost) sio_printf("@X0CHealing costs %d gold.  You don't have it.@X07\n", cost);
            else { pl.gold -= cost; pl.hp = pl.maxhp; save_player(); sio_printf("@X0AFor %d gold you are healed completely.@X07\n", cost); }
            sio_pause();
            break;
        }
        case 'B': bank(); break;
        case 'I': inn(); break;
        case 'V': show_stats(); break;
        case 'P': scores(); break;
        case 'D': daily_news(); break;
        case 'S': dragon(); if (pl.dead) return; break;
        case 'Q': return;
        }
    }
}

static int find_or_create(void)
{
    static struct player all[MAXPLAYERS];
    int n = load_players(all, MAXPLAYERS);
    for (int i = 0; i < n; i++) if (!strcasecmp(all[i].name, username)) { pl = all[i]; plnum = i; return 0; }
    sio_printf("\n@X0AA new face in Acorn Vale!  Welcome, @X0F%s@X0A.@X07\n", username);
    if (!sio_yesno("@X02Do you want to become an adventurer", 1)) return -1;
    memset(&pl, 0, sizeof pl);
    snprintf(pl.name, sizeof pl.name, "%s", username);
    pl.level = 1; pl.maxhp = pl.hp = 20; pl.str = 10; pl.def = 1; pl.gold = 100;
    pl.fights = FIGHTS_PER_DAY; pl.day = (int16_t)today_num();
    plnum = -1;
    save_player();
    news("%s has come to Acorn Vale.", pl.name);
    return 1;
}

static void int23(struct armregs *f) { f->cpsr &= ~ARM_CPSR_C; }

int main(int argc, char **argv)
{
    int local = 0;
    const char *dir = ".";
    for (int i = 1; i < argc; i++) {
        if (!strcasecmp(argv[i], "/L")) local = 1;
        else dir = argv[i];
    }
    _dos_setvect(0x23, int23);
    srand((unsigned)time(NULL) ^ (unsigned)TICKS());
    if (!local) read_dropfile(dir);
    if (port < 1 || port > 4) port = 0;
    scr_init();
    int remote = 0;
    if (port && com_open(port, baud ? baud : 2400) == 0) remote = com_carrier();
    sio_begin(remote, ansi, 23);
    sio_deadline = TICKS() + (uint32_t)(minutes > 0 ? minutes : 1) * 1092;
    sio_idle_ticks = 5u * 1092;
    sio_status_hook = status;
    vt_clear(&sio_vt);
    scr_fill(0, 23, 80, 2, ' ', 0x2F);
    int why = setjmp(sio_drop);
    if (why == 0) {
        title();
        int r = find_or_create();
        if (r >= 0) {
            int day = today_num();
            if (pl.day != day) {                /* a new day dawns */
                pl.day = (int16_t)day;
                pl.fights = FIGHTS_PER_DAY;
                if (pl.dead) { pl.dead = 0; pl.hp = pl.maxhp; }
                save_player();
                if (r == 0) sio_puts("\n@X0EA new day dawns.  You feel rested, and the forest calls.@X07\n");
            }
            fought_master_today = 0;
            if (pl.dead) {
                sio_puts("\n@X0CYou are dead.  Ghosts can't fight.  Come back tomorrow!@X07\n");
                sio_pause();
            } else {
                sio_pause();
                town();
            }
            save_player();
        }
        sio_puts("\n@X0AYou return to the BBS.  Farewell, adventurer!@X07\n");
        sio_flush();
    } else {
        save_player();
    }
    com_close(1);                               /* never drop DTR in a door */
    return 0;
}
