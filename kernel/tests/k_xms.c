/* k_xms.c - HIMEM.SYS: XMS 2.0 through INT 2Fh and the entry point, and the
   SDK heap growing into extended memory */
#include "t.h"

static void *entry;
static struct armregs X;

static unsigned xms(unsigned ax, unsigned bx, unsigned dx, uint32_t si)
{
    memset(&X, 0, sizeof X);
    X.r0 = ax; X.r1 = bx; X.r3 = dx; X.r4 = si;
    return _armdos_farcall(entry, &X) & 0xFFFF;
}
#define BLV (X.r1 & 0xFF)
#define DXV (X.r3 & 0xFFFF)
#define BXV (X.r1 & 0xFFFF)

struct move { uint32_t len; uint16_t sh; uint32_t so; uint16_t dh; uint32_t dof; } __attribute__((packed));

int main(void)
{
    t_begin("xms");
    struct armregs r = { 0 };
    r.r0 = 0x4300;
    _armdos_int2f(&r);
    T_EQ(r.r0 & 0xFF, 0x80);
    memset(&r, 0, sizeof r);
    r.r0 = 0x4310;
    _armdos_int2f(&r);
    entry = (void *)r.r1;
    T_CHECK(entry != 0, "entry");
    if (!entry) return t_end();
    T_EQ(armdos_xms_entry(), entry);

    T_EQ(xms(0x0000, 0, 0, 0), 0x0200);
    T_EQ(DXV, 1);
    T_EQ(xms(0x0100, 0, 0, 0), 0);           /* HMA: in use by the BIOS */
    T_EQ(BLV, 0x91);
    T_EQ(xms(0x0700, 0, 0, 0), 1);           /* A20 on */
    xms(0x0800, 0, 0, 0);
    unsigned largest = X.r0 & 0xFFFF, total = DXV;
    T_EQ(total, 15296);
    T_EQ(largest, 15296);

    T_EQ(xms(0x0900, 0, 1024, 0), 1);
    unsigned h1 = DXV;
    T_EQ(xms(0x0900, 0, 2048, 0), 1);
    unsigned h2 = DXV;
    xms(0x0800, 0, 0, 0);
    T_EQ(DXV, 15296 - 3072);
    T_EQ(xms(0x0C00, 0, h1, 0), 1);
    uint32_t a1 = (DXV << 16) | BXV;
    T_EQ(a1, 0x110000);
    T_EQ(xms(0x0C00, 0, h2, 0), 1);
    uint32_t a2 = (DXV << 16) | BXV;
    T_EQ(a2, 0x110000 + 1024 * 1024);
    /* the memory is real */
    for (uint32_t i = 0; i < 1024 * 1024; i += 4096) *(volatile uint32_t *)(a1 + i) = i ^ 0x5A5A5A5A;
    int ok = 1;
    for (uint32_t i = 0; i < 1024 * 1024; i += 4096) if (*(volatile uint32_t *)(a1 + i) != (i ^ 0x5A5A5A5A)) ok = 0;
    T_EQ(ok, 1);
    T_EQ(xms(0x0A00, 0, h1, 0), 0);          /* locked */
    T_EQ(BLV, 0xAB);
    xms(0x0E00, 0, h1, 0);
    T_EQ((BXV >> 8), 1);
    T_EQ(DXV, 1024);
    T_EQ(xms(0x0D00, 0, h1, 0), 1);
    T_EQ(xms(0x0D00, 0, h1, 0), 0);
    T_EQ(BLV, 0xAA);
    xms(0x0D00, 0, h2, 0);

    /* move: conventional -> XMS -> XMS -> conventional */
    static char src[256], back[256];
    for (int i = 0; i < 256; i++) src[i] = i;
    static struct move m;
    m.len = 256; m.sh = 0; m.so = (uint32_t)src; m.dh = h2; m.dof = 1000;
    T_EQ(xms(0x0B00, 0, 0, (uint32_t)&m), 1);
    m.len = 256; m.sh = h2; m.so = 1000; m.dh = h1; m.dof = 5000;
    T_EQ(xms(0x0B00, 0, 0, (uint32_t)&m), 1);
    m.len = 256; m.sh = h1; m.so = 5000; m.dh = 0; m.dof = (uint32_t)back;
    T_EQ(xms(0x0B00, 0, 0, (uint32_t)&m), 1);
    T_CHECK(!memcmp(src, back, 256), "moved data");
    m.len = 3;
    T_EQ(xms(0x0B00, 0, 0, (uint32_t)&m), 0);
    T_EQ(BLV, 0xA7);
    m.len = 2; m.sh = h1; m.so = 1024 * 1024; m.dh = 0;
    T_EQ(xms(0x0B00, 0, 0, (uint32_t)&m), 0);
    T_EQ(BLV, 0xA4);

    /* realloc: shrink, grow (moves past h2) */
    T_EQ(xms(0x0F00, 512, h1, 0), 1);
    T_EQ(xms(0x0F00, 4096, h1, 0), 1);
    xms(0x0E00, 0, h1, 0);
    T_EQ(DXV, 4096);
    T_EQ(xms(0x0A00, 0, h1, 0), 1);
    T_EQ(xms(0x0A00, 0, h2, 0), 1);
    T_EQ(xms(0x0A00, 0, h2, 0), 0);
    T_EQ(BLV, 0xA2);
    xms(0x0800, 0, 0, 0);
    T_EQ(DXV, 15296);
    T_EQ(xms(0x0900, 0, 20000, 0), 0);
    T_EQ(BLV, 0xA0);
    T_EQ(xms(0x1000, 0, 0, 0), 0);           /* no UMBs */
    T_EQ(BLV, 0xB1);
    T_EQ(xms(0x7700, 0, 0, 0), 0);
    T_EQ(BLV, 0x80);

    /* the C heap grows into extended memory when conventional memory runs out */
    void *p = malloc(3 * 1024 * 1024);
    T_CHECK(p != 0, "malloc 3 MB");
    T_CHECK((uint32_t)p >= 0x110000, "heap in XMS at %p", p);
    if (p) memset(p, 0x77, 3 * 1024 * 1024);
    T_CHECK(armdos_xms_heap_size() > 3 * 1024 * 1024, "xms heap %lu", armdos_xms_heap_size());
    return t_end();
}
