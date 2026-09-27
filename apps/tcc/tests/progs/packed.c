/* packed structures: members at odd addresses (ARMv5 has no unaligned access) */
#include <stdio.h>
#include <string.h>
#include <dos.h>

struct __attribute__((packed)) P { char c; int i; short s; unsigned u; char d; unsigned short us; };
#pragma pack(push, 1)
struct Q { char a; long l; char b; short t; };
#pragma pack(pop)

struct P gp = { 'x', 0x12345678, -1234, 0xDEADBEEF, 'y', 0xBEEF };

int main(void)
{
    struct P p;
    struct Q q[3];
    unsigned char buf[32];
    int *ip;
    struct find_t f;
    memset(&p, 0, sizeof p);
    p.c = 'a'; p.i = -5; p.s = 300; p.u = 0xCAFEBABE; p.d = 'z'; p.us = 65535;
    printf("sizes %d %d\n", (int)sizeof(struct P), (int)sizeof(struct Q));
    printf("p %c %d %d %X %c %u\n", p.c, p.i, p.s, p.u, p.d, p.us);
    p.i += 10; p.s *= 2; p.u >>= 4; p.us--;
    printf("p2 %d %d %X %u\n", p.i, p.s, p.u, p.us);
    printf("gp %c %X %d %X %c %X\n", gp.c, gp.i, gp.s, gp.u, gp.d, gp.us);
    q[1].l = 0x11223344; q[1].t = 0x5566; q[2].l = -1; q[0].b = 1;
    printf("q %lX %X %ld\n", q[1].l, q[1].t, q[2].l);
    memcpy(buf, &p, sizeof p);
    printf("bytes"); for (int i = 0; i < (int)sizeof p; i++) printf(" %02X", buf[i]); printf("\n");
    ip = &p.i;      /* address of a packed member */
    printf("addr %d\n", (int)((char *)ip - (char *)&p));
    memset(&f, 0, sizeof f);
    f.size = 123456789; f.wr_date = 0x1234;
    printf("find_t %lu %X %d\n", f.size, f.wr_date, (int)((char *)&f.size - (char *)&f));
    return 0;
}
