/* k_stress.c - random file operations checked against a model, deep
   directories, and filling the hard disk.  Roles: RANDOM [seed] [ops], FILLHD */
#include "t.h"

static struct armregs R;
#define AXV (R.r0 & 0xFFFF)
static int d21(unsigned ax, unsigned bx, unsigned cx, uint32_t dx)
{ memset(&R, 0, sizeof R); R.r0 = ax; R.r1 = bx; R.r2 = cx; R.r3 = dx; return _armdos_int21(&R); }

static uint32_t rng = 1;
static uint32_t rnd(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }

#define NF 48
#define MAXSZ 24000
static struct { int exists; char path[64]; unsigned size; uint32_t seed; } model[NF];
static uint8_t buf[MAXSZ + 1024], chk[MAXSZ + 1024];

static void gen(uint8_t *p, unsigned off, unsigned n, uint32_t seed)
{
    for (unsigned i = 0; i < n; i++) { uint32_t x = (off + i) * 2654435761u ^ seed; p[i] = (x >> 13) ^ (x >> 24); }
}

static const char *dirs[] = { "S", "S\\A", "S\\A\\B", "S\\A\\B\\C", "S\\A\\B\\C\\D", "S\\A\\B\\C\\D\\E", "S\\X", "S\\X\\Y" };

static int write_all(int h, const uint8_t *p, unsigned n)
{
    while (n) {
        unsigned c = n > 16000 ? 16000 : n;
        if (d21(0x4000, h, c, (uint32_t)p) || AXV != c) return -1;
        p += c; n -= c;
    }
    return 0;
}

static int verify(int i)
{
    if (d21(0x3D00, 0, 0, (uint32_t)model[i].path)) return model[i].exists ? -1 : 0;
    if (!model[i].exists) { d21(0x3E00, AXV, 0, 0); return -2; }
    int h = AXV;
    unsigned got = 0;
    for (;;) {
        if (d21(0x3F00, h, 7000, (uint32_t)(buf + got)) || !AXV) break;
        got += AXV;
        if (got > MAXSZ) break;
    }
    d21(0x3E00, h, 0, 0);
    if (got != model[i].size) return -3;
    gen(chk, 0, got, model[i].seed);
    return memcmp(buf, chk, got) ? -4 : 0;
}

static void role_random(uint32_t seed, int ops)
{
    t_begin("stress-random");
    rng = seed;
    for (unsigned k = 0; k < sizeof dirs / sizeof dirs[0]; k++) d21(0x3900, 0, 0, (uint32_t)dirs[k]);
    for (int i = 0; i < NF; i++) {
        sprintf(model[i].path, "%s\\F%02d.DAT", dirs[i % 8], i);
        model[i].exists = 0;
        d21(0x4100, 0, 0, (uint32_t)model[i].path);    /* from an earlier run */
    }
    int bad = 0;
    for (int op = 0; op < ops && !bad; op++) {
        int i = rnd() % NF;
        int what = rnd() % 6;
        if (what <= 1) {                       /* create / overwrite */
            unsigned n = rnd() % MAXSZ;
            model[i].seed = rnd();
            gen(buf, 0, n, model[i].seed);
            if (d21(0x3C00, 0, 0, (uint32_t)model[i].path)) { t_log("T:FAIL create %s %u\n", model[i].path, AXV); bad = 1; break; }
            int h = AXV;
            if (write_all(h, buf, n)) { bad = 1; t_log("T:FAIL write %s\n", model[i].path); }
            d21(0x3E00, h, 0, 0);
            model[i].exists = 1;
            model[i].size = n;
        } else if (what == 2 && model[i].exists) {      /* append (same seed: data continues) */
            unsigned n = rnd() % 4000;
            if (model[i].size + n > MAXSZ) n = MAXSZ - model[i].size;
            d21(0x3D02, 0, 0, (uint32_t)model[i].path);
            int h = AXV;
            d21(0x4202, h, 0, 0);
            gen(buf, model[i].size, n, model[i].seed);
            write_all(h, buf, n);
            d21(0x3E00, h, 0, 0);
            model[i].size += n;
        } else if (what == 3 && model[i].exists) {      /* truncate */
            unsigned n = model[i].size ? rnd() % model[i].size : 0;
            d21(0x3D02, 0, 0, (uint32_t)model[i].path);
            int h = AXV;
            d21(0x4200, h, n >> 16, n & 0xFFFF);
            d21(0x4000, h, 0, (uint32_t)buf);
            d21(0x3E00, h, 0, 0);
            model[i].size = n;
        } else if (what == 4 && model[i].exists) {      /* delete */
            d21(0x4100, 0, 0, (uint32_t)model[i].path);
            model[i].exists = 0;
        } else if (what == 5) {                        /* rename to another slot (another directory) */
            int j = rnd() % NF;
            if (j == i || !model[i].exists || model[j].exists) continue;
            memset(&R, 0, sizeof R);
            R.r0 = 0x5600; R.r3 = (uint32_t)model[i].path; R.r5 = (uint32_t)model[j].path;
            if (_armdos_int21(&R)) { t_log("T:FAIL rename %s %s %u\n", model[i].path, model[j].path, AXV); bad = 1; break; }
            model[j] = model[i];
            sprintf(model[j].path, "%s\\F%02d.DAT", dirs[j % 8], j);
            model[i].exists = 0;
        }
        if (op % 50 == 49) for (int k = 0; k < NF; k++) {
            int v = verify(k);
            if (v) { t_log("T:FAIL verify %s: %d (op %d)\n", model[k].path, v, op); bad = 1; break; }
        }
    }
    for (int k = 0; k < NF && !bad; k++) T_CHECK(verify(k) == 0, "final %s", model[k].path);
    T_EQ(bad, 0);
    t_end();
}

static void role_fillhd(void)
{
    t_begin("stress-fillhd");
    d21(0x3600, 0, 0, 3);
    unsigned long freeb = (unsigned long)(R.r1 & 0xFFFF) * (R.r0 & 0xFFFF) * (R.r2 & 0xFFFF);
    d21(0x3C00, 0, 0, (uint32_t)"BIG.DAT");
    int h = AXV;
    static uint8_t block[32768];
    for (unsigned i = 0; i < sizeof block; i++) block[i] = i * 13;
    unsigned long total = 0;
    for (;;) {
        d21(0x4000, h, sizeof block, (uint32_t)block);
        total += AXV;
        if (AXV < sizeof block) break;
    }
    t_log("T:LOG wrote %lu of %lu free\n", total, freeb);
    T_EQ(total, freeb);
    d21(0x3600, 0, 0, 3);
    T_EQ(R.r1 & 0xFFFF, 0);
    /* a small file cannot be created with data now */
    d21(0x3E00, h, 0, 0);
    T_EQ(d21(0x3C00, 0, 0, (uint32_t)"SMALL.DAT"), 0);
    int h2 = AXV;
    d21(0x4000, h2, 10, (uint32_t)block);
    T_EQ(AXV, 0);
    d21(0x3E00, h2, 0, 0);
    /* read the big file back (spot checks) */
    d21(0x3D00, 0, 0, (uint32_t)"BIG.DAT");
    h = AXV;
    d21(0x4202, h, 0, 0);
    T_EQ(((R.r3 & 0xFFFF) << 16) | AXV, total);
    d21(0x4200, h, 0x0100, 0);
    static uint8_t b2[512];
    d21(0x3F00, h, 512, (uint32_t)b2);
    T_EQ(b2[1], block[(0x01000000 + 1) % sizeof block]);
    d21(0x3E00, h, 0, 0);
    d21(0x4100, 0, 0, (uint32_t)"BIG.DAT");
    d21(0x4100, 0, 0, (uint32_t)"SMALL.DAT");
    d21(0x3600, 0, 0, 3);
    T_EQ((unsigned long)(R.r1 & 0xFFFF) * (R.r0 & 0xFFFF) * (R.r2 & 0xFFFF), freeb);
    t_end();
}

int main(int argc, char **argv)
{
    if (argc < 2) return 100;
    if (!strcmp(argv[1], "RANDOM")) role_random(argc > 2 ? atoi(argv[2]) : 1, argc > 3 ? atoi(argv[3]) : 400);
    else if (!strcmp(argv[1], "FILLHD")) role_fillhd();
    return t_fails ? 1 : 0;
}
