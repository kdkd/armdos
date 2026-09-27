/* k_mem.c - AH=48h/49h/4Ah/58h: allocation, fragmentation, strategies, errors */
#include "t.h"

static struct armregs R;
#define AXV (R.r0 & 0xFFFF)

static int alloc(unsigned paras, unsigned *largest)
{
    memset(&R, 0, sizeof R);
    R.r0 = 0x4800; R.r1 = paras;
    if (_armdos_int21(&R)) { if (largest) *largest = R.r1 & 0xFFFF; return -(int)AXV; }
    return AXV;
}
static int dfree(unsigned seg)
{
    memset(&R, 0, sizeof R);
    R.r0 = 0x4900; R.r8 = seg;
    return _armdos_int21(&R) ? -(int)AXV : 0;
}
static int resize(unsigned seg, unsigned paras, unsigned *maxp)
{
    memset(&R, 0, sizeof R);
    R.r0 = 0x4A00; R.r1 = paras; R.r8 = seg;
    if (_armdos_int21(&R)) { if (maxp) *maxp = R.r1 & 0xFFFF; return -(int)AXV; }
    return 0;
}
static unsigned largest(void) { unsigned l = 0; alloc(0xFFFF, &l); return l; }
static void strategy(int s) { memset(&R, 0, sizeof R); R.r0 = 0x5801; R.r1 = s; _armdos_int21(&R); }

int main(void)
{
    t_begin("mem");
    unsigned big = largest();
    T_CHECK(big > 20000, "largest free block %u paragraphs", big);
    unsigned l;
    T_EQ(alloc(big + 1, &l), -8);
    T_EQ(l, big);

    /* 20 blocks, free every other one: fragmentation */
    int seg[20];
    for (int i = 0; i < 20; i++) {
        seg[i] = alloc(100, 0);
        T_CHECK(seg[i] > 0, "alloc %d", i);
        memset((void *)(seg[i] << 4), i, 1600);
    }
    for (int i = 1; i < 20; i += 2) T_EQ(dfree(seg[i]), 0);
    for (int i = 0; i < 20; i += 2) {
        uint8_t *p = (uint8_t *)(seg[i] << 4);
        T_CHECK(p[0] == i && p[1599] == i, "block %d intact", i);
    }
    /* first fit reuses the lowest hole */
    int a = alloc(50, 0);
    T_EQ(a, seg[1]);
    dfree(a);
    /* best fit: fill hole 1, put 60 paragraphs in hole 3 -> a 39-paragraph hole */
    int h1 = alloc(100, 0), h3 = alloc(60, 0);
    T_EQ(h1, seg[1]);
    T_EQ(h3, seg[3]);
    strategy(1);
    memset(&R, 0, sizeof R); R.r0 = 0x5800; _armdos_int21(&R);
    T_EQ(AXV, 1);
    a = alloc(39, 0);
    T_EQ(a, seg[3] + 61);
    dfree(a);
    strategy(0);
    a = alloc(39, 0);
    T_EQ(a, seg[3] + 61);                               /* first fit: the first hole too */
    dfree(a);
    dfree(h1);
    dfree(h3);
    a = alloc(39, 0);
    T_EQ(a, seg[1]);
    dfree(a);
    int f18 = alloc(1, 0);
    /* last fit: top of the highest free block */
    strategy(2);
    a = alloc(10, 0);
    unsigned top = a + 10;
    struct mcb *m = (struct mcb *)((uint32_t)(a - 1) << 4);
    T_EQ(m->type, 'Z');
    T_CHECK(top == 0xA000, "last fit block ends at %x", top);
    dfree(a);
    strategy(0);
    dfree(f18);
    for (int i = 0; i < 20; i += 2) T_EQ(dfree(seg[i]), 0);
    T_EQ(largest(), big);

    /* resize: grow into the free space after the block, shrink, fail */
    a = alloc(100, 0);
    int b = alloc(100, 0);
    T_EQ(dfree(b), 0);
    T_EQ(resize(a, 150, 0), 0);
    unsigned mx = 0;
    T_EQ(resize(a, 0xFFFF, &mx), -8);
    T_EQ(mx, big);                                      /* all the rest after coalescing */
    T_EQ(resize(a, 10, 0), 0);
    T_EQ(dfree(a), 0);
    T_EQ(largest(), big);

    /* bad blocks */
    T_EQ(dfree(0x1234), -9);
    T_EQ(resize(0x1234, 10, 0), -7);                    /* DOS says "arena trashed" here */

    /* our own block: the SDK shrank it at start-up; 4Ah on it works */
    unsigned me = (uint32_t)_armdos_psp >> 4;
    struct mcb *mine = (struct mcb *)((uint32_t)(me - 1) << 4);
    T_EQ(mine->owner, me);
    T_EQ(resize(me, mine->size, 0), 0);

    /* malloc through the SDK heap (grows our block with 4Ah) */
    void *p = malloc(200000);
    T_CHECK(p != 0, "malloc 200000");
    free(p);
    return t_end();
}
