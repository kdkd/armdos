/* k_flop.c - the floppy: FAT12 (12-bit packing at odd and even clusters),
   filling the disk, media change, the B: phantom drive.  Roles:
     FILL    fill A: with files, check, delete every other, refill, check
     SWAP    read A:'s label, ask the harness to change diskettes, read again
     PHANTOM use B: (prompt) then A: again (prompt)
*/
#include "t.h"

static struct armregs R;
#define AXV (R.r0 & 0xFFFF)

static int d21(unsigned ax, unsigned bx, unsigned cx, uint32_t dx)
{
    memset(&R, 0, sizeof R);
    R.r0 = ax; R.r1 = bx; R.r2 = cx; R.r3 = dx;
    return _armdos_int21(&R);
}

static uint8_t buf[40000];

static void fill_pattern(uint8_t *p, unsigned n, unsigned id)
{
    uint32_t x = id * 2654435761u + 12345;
    for (unsigned i = 0; i < n; i++) { x = x * 1103515245u + 12345; p[i] = x >> 24; }
}

static unsigned file_size(unsigned i) { return (i * 3571u) % 17000 + (i % 3) * 511; }

static int write_file(unsigned i)
{
    char name[20];
    sprintf(name, "A:\\F%03u.DAT", i);
    unsigned n = file_size(i);
    fill_pattern(buf, n, i);
    if (d21(0x3C00, 0, 0, (uint32_t)name)) return -1;
    int h = AXV;
    d21(0x4000, h, n, (uint32_t)buf);
    unsigned w = AXV;
    d21(0x3E00, h, 0, 0);
    return w == n ? 0 : 1;
}

static int check_file(unsigned i)
{
    char name[20];
    static uint8_t want[40000];
    sprintf(name, "A:\\F%03u.DAT", i);
    unsigned n = file_size(i);
    fill_pattern(want, n, i);
    if (d21(0x3D00, 0, 0, (uint32_t)name)) return -1;
    int h = AXV;
    d21(0x3F00, h, 40000, (uint32_t)buf);
    unsigned r = AXV;
    d21(0x3E00, h, 0, 0);
    return (r == n && !memcmp(buf, want, n)) ? 0 : 1;
}

static unsigned free_clusters(void)
{
    d21(0x3600, 0, 0, 1);
    return R.r1 & 0xFFFF;
}

static void role_fill(void)
{
    t_begin("floppy-fill");
    d21(0x3600, 0, 0, 1);
    T_EQ(R.r0 & 0xFFFF, 1);                  /* sectors per cluster */
    T_EQ(R.r2 & 0xFFFF, 512);
    T_EQ(R.r3 & 0xFFFF, 2847);               /* clusters on a 1.44 MB diskette */
    unsigned free0 = R.r1 & 0xFFFF;
    /* write until the disk is full */
    unsigned n = 0, full = 0;
    for (;; n++) {
        int r = write_file(n);
        if (r < 0) { t_log("T:LOG create failed at %u\n", n); break; }
        if (r > 0) { full = 1; break; }
        if (n > 400) break;
    }
    T_EQ(full, 1);
    T_EQ(free_clusters(), 0);
    t_log("T:LOG %u files before full\n", n);
    for (unsigned i = 0; i < n; i++) T_CHECK(check_file(i) == 0, "file %u", i);
    /* the partial last file has what was written */
    char name[20];
    sprintf(name, "A:\\F%03u.DAT", n);
    d21(0x4E00, 0, 0, (uint32_t)name);
    /* delete every other one, refill with new sizes */
    for (unsigned i = 0; i <= n; i += 2) {
        sprintf(name, "A:\\F%03u.DAT", i);
        d21(0x4100, 0, 0, (uint32_t)name);
    }
    T_CHECK(free_clusters() > 0, "space came back");
    unsigned m = 0;
    for (unsigned i = 1000; ; i++) {
        int r = write_file(i);
        if (r) break;
        m++;
        if (m > 400) break;
    }
    t_log("T:LOG refilled %u files\n", m);
    for (unsigned i = 1; i < n; i += 2) T_CHECK(check_file(i) == 0, "old file %u", i);
    for (unsigned i = 1000; i < 1000 + m; i++) T_CHECK(check_file(i) == 0, "new file %u", i);
    /* delete everything: all space back */
    uint8_t dta[43];
    d21(0x1A00, 0, 0, (uint32_t)dta);
    int cf = d21(0x4E00, 0, 0, (uint32_t)"A:\\F*.DAT");
    while (!cf) {
        char nm[20];
        sprintf(nm, "A:\\%s", (char *)dta + 0x1E);
        d21(0x4100, 0, 0, (uint32_t)nm);
        cf = d21(0x4F00, 0, 0, 0);
    }
    T_EQ(free_clusters(), free0);
    /* leave some files for the harness to check with mtools */
    for (unsigned i = 0; i < 20; i++) write_file(i);
    t_end();
}

static void label(char *out)
{
    uint8_t dta[43];
    d21(0x1A00, 0, 0, (uint32_t)dta);
    if (d21(0x4E00, 0, 8, (uint32_t)"A:\\*.*")) strcpy(out, "(none)");
    else strcpy(out, (char *)dta + 0x1E);
}

static void role_swap(void)
{
    t_begin("floppy-swap");
    char l[20];
    label(l);
    t_log("T:LABEL1 %s\n", l);
    int cf = d21(0x3D00, 0, 0, (uint32_t)"A:\\ONE.TXT");
    T_EQ(cf, 0);
    d21(0x3E00, AXV, 0, 0);
    t_log("T:SWAP\n");
    /* wait for the harness to say it is done: it types a key */
    d21(0x0800, 0, 0, 0);
    label(l);
    t_log("T:LABEL2 %s\n", l);
    cf = d21(0x3D00, 0, 0, (uint32_t)"A:\\ONE.TXT");
    T_EQ(cf, 1);
    cf = d21(0x3D00, 0, 0, (uint32_t)"A:\\TWO.TXT");
    T_EQ(cf, 0);
    d21(0x3E00, AXV, 0, 0);
    /* write on the new diskette */
    d21(0x3C00, 0, 0, (uint32_t)"A:\\NEW.TXT");
    int h = AXV;
    d21(0x4000, h, 5, (uint32_t)"fresh");
    d21(0x3E00, h, 0, 0);
    t_end();
}

static void role_phantom(void)
{
    t_begin("floppy-phantom");
    int cf = d21(0x3D00, 0, 0, (uint32_t)"B:\\TWO.TXT");
    T_EQ(cf, 0);
    d21(0x3E00, AXV, 0, 0);
    d21(0x4400 | 0x0E, 1, 0, 0);             /* who owns the drive: B: (2) */
    T_EQ(R.r0 & 0xFF, 2);
    cf = d21(0x3D00, 0, 0, (uint32_t)"A:\\TWO.TXT");
    T_EQ(cf, 0);
    d21(0x3E00, AXV, 0, 0);
    d21(0x4400 | 0x0E, 1, 0, 0);
    T_EQ(R.r0 & 0xFF, 1);
    t_end();
}

int main(int argc, char **argv)
{
    if (argc < 2) return 100;
    t_log("T:ROLE %s\n", argv[1]);
    if (!strcmp(argv[1], "FILL")) role_fill();
    else if (!strcmp(argv[1], "SWAP")) role_swap();
    else if (!strcmp(argv[1], "PHANTOM")) role_phantom();
    return t_fails ? 1 : 0;
}
