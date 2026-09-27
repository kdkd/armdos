/*
 * aux.c - AUX/COM1-COM4 (INT 14h, MSAUX.ASM) and PRN/LPT1-LPT3 (INT 17h,
 * MSLPT.ASM), and CLOCK$ (MSCLOCK.ASM: INT 1Ah tick count + CMOS date).
 */
#include "iosys.h"

/* ------------------------------------------------------------ serial */

static struct reqhdr *ser_req[4];
static int16_t ser_la[4] = { -1, -1, -1, -1 };  /* one byte of look-ahead for non-destructive reads */
#define SER(n) \
    static void com##n##_strategy(struct reqhdr *r) { ser_req[n] = r; } \
    static void com##n##_interrupt(void) { ser_do(n, ser_req[n]); }

static int ser_status(int port)
{
    struct armregs r = { 0 };
    r.r0 = 0x0300;
    r.r3 = port;
    kint(0x14, &r);
    return (r.r0 >> 8) & 0xFF;          /* line status */
}

static void ser_do(int port, struct reqhdr *r)
{
    r->status = RS_DONE;
    switch (r->cmd) {
    case CMD_INIT: break;
    case CMD_READ: {
        struct req_rw *q = (struct req_rw *)r;
        uint8_t *p = (uint8_t *)q->addr;
        for (unsigned i = 0; i < q->count; i++) {
            if (ser_la[port] >= 0) { p[i] = ser_la[port]; ser_la[port] = -1; continue; }
            for (;;) {
                struct armregs a = { 0 };
                a.r0 = 0x0200;
                a.r3 = port;
                kint(0x14, &a);
                if (!(a.r0 & 0x8000)) { p[i] = a.r0; break; }
                cpu_wfi();              /* wait for the next tick and poll again */
            }
        }
        break;
    }
    case CMD_NDREAD:
    case CMD_INSTAT:
        if (ser_la[port] < 0 && (ser_status(port) & 1)) {
            struct armregs a = { 0 };
            a.r0 = 0x0200;
            a.r3 = port;
            kint(0x14, &a);
            if (!(a.r0 & 0x8000)) ser_la[port] = a.r0 & 0xFF;
        }
        if (ser_la[port] < 0) r->status |= RS_BUSY;
        else if (r->cmd == CMD_NDREAD) ((struct req_ndread *)r)->ch = ser_la[port];
        break;
    case CMD_WRITE:
    case CMD_WRITEV: {
        struct req_rw *q = (struct req_rw *)r;
        const uint8_t *p = (const uint8_t *)q->addr;
        for (unsigned i = 0; i < q->count; i++) {
            struct armregs a = { 0 };
            a.r0 = 0x0100 | p[i];
            a.r3 = port;
            kint(0x14, &a);
            if (a.r0 & 0x8000) { q->count = i; r->status |= RS_ERROR | DE_WRITE; return; }
        }
        break;
    }
    case CMD_OUTSTAT:
        if (!(ser_status(port) & 0x20)) r->status |= RS_BUSY;
        break;
    case CMD_INFLUSH:
        ser_la[port] = -1;
        break;
    case CMD_OUTFLUSH: case CMD_OPEN: case CMD_CLOSE:
        break;
    default:
        r->status |= RS_ERROR | DE_BADCMD;
    }
}

SER(0) SER(1) SER(2) SER(3)

#define CHARDEV(next, strat, intr, name) { next, DEVA_CHAR, 0, strat, intr, name }
struct devhdr aux_dev  = CHARDEV(&prn_dev,  com0_strategy, com0_interrupt, "AUX     ");
struct devhdr com1_dev = CHARDEV(&lpt1_dev, com0_strategy, com0_interrupt, "COM1    ");
struct devhdr com2_dev = CHARDEV(&com3_dev, com1_strategy, com1_interrupt, "COM2    ");
struct devhdr com3_dev = CHARDEV(&com4_dev, com2_strategy, com2_interrupt, "COM3    ");
struct devhdr com4_dev = CHARDEV(DEV_END,   com3_strategy, com3_interrupt, "COM4    ");

/* ----------------------------------------------------------- printer */

static struct reqhdr *lpt_req[3];

/* INT 17h status byte -> device error code, or -1 if the printer is fine */
static int lpt_error(int st)
{
    if (st & 0x20) return DE_PAPER;
    if (st & 0x01) return DE_NOTREADY;          /* time out */
    if (st & 0x08) return DE_WRITE;             /* I/O error */
    return -1;
}

static void lpt_do(int port, struct reqhdr *r)
{
    r->status = RS_DONE;
    switch (r->cmd) {
    case CMD_INIT: break;
    case CMD_WRITE:
    case CMD_WRITEV:
    case CMD_OUTBUSY: {
        struct req_rw *q = (struct req_rw *)r;
        const uint8_t *p = (const uint8_t *)q->addr;
        for (unsigned i = 0; i < q->count; i++) {
            struct armregs a = { 0 };
            a.r0 = p[i];
            a.r3 = port;
            kint(0x17, &a);
            int e = lpt_error((a.r0 >> 8) & 0xFF);
            if (e >= 0) { q->count = i; r->status |= RS_ERROR | e; return; }
        }
        break;
    }
    case CMD_OUTSTAT: {
        struct armregs a = { 0 };
        a.r0 = 0x0200;
        a.r3 = port;
        kint(0x17, &a);
        if (!((a.r0 >> 8) & 0x80)) r->status |= RS_BUSY;
        break;
    }
    case CMD_READ:                      /* reading a printer: nothing, as DOS */
        ((struct req_rw *)r)->count = 0;
        break;
    case CMD_NDREAD:
        r->status |= RS_BUSY;
        break;
    case CMD_INSTAT: case CMD_INFLUSH: case CMD_OUTFLUSH: case CMD_OPEN: case CMD_CLOSE:
        break;
    default:
        r->status |= RS_ERROR | DE_BADCMD;
    }
}

#define LPT(n) \
    static void lpt##n##_strategy(struct reqhdr *r) { lpt_req[n] = r; } \
    static void lpt##n##_interrupt(void) { lpt_do(n, lpt_req[n]); }
LPT(0) LPT(1) LPT(2)

struct devhdr prn_dev  = { &clock_dev, DEVA_CHAR | DEVA_NONIBM, 0, lpt0_strategy, lpt0_interrupt, "PRN     " };
struct devhdr lpt1_dev = { &lpt2_dev, DEVA_CHAR | DEVA_NONIBM, 0, lpt0_strategy, lpt0_interrupt, "LPT1    " };
struct devhdr lpt2_dev = { &lpt3_dev, DEVA_CHAR | DEVA_NONIBM, 0, lpt1_strategy, lpt1_interrupt, "LPT2    " };
struct devhdr lpt3_dev = { &com2_dev, DEVA_CHAR | DEVA_NONIBM, 0, lpt2_strategy, lpt2_interrupt, "LPT3    " };

/* ------------------------------------------------------------- CLOCK$ */

static struct reqhdr *clk_req;
static uint16_t clk_days;               /* days since 1-1-1980 */

static const uint8_t mdays[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

static int leap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

static unsigned ymd_to_days(int y, int m, int d)
{
    unsigned n = 0;
    for (int yy = 1980; yy < y; yy++) n += leap(yy) ? 366 : 365;
    for (int mm = 1; mm < m; mm++) n += mdays[mm - 1] + (mm == 2 && leap(y));
    return n + d - 1;
}

static void days_to_ymd(unsigned n, int *y, int *m, int *d)
{
    int yy = 1980;
    for (;;) {
        unsigned len = leap(yy) ? 366 : 365;
        if (n < len) break;
        n -= len; yy++;
    }
    int mm = 1;
    for (;;) {
        unsigned len = mdays[mm - 1] + (mm == 2 && leap(yy));
        if (n < len) break;
        n -= len; mm++;
    }
    *y = yy; *m = mm; *d = n + 1;
}

static int bcd(int v) { return (v >> 4) * 10 + (v & 15); }
static int tobcd(int v) { return ((v / 10) << 4) | (v % 10); }

static void clk_strategy(struct reqhdr *r) { clk_req = r; }

static void clk_interrupt(void)
{
    struct reqhdr *r = clk_req;
    struct req_rw *q = (struct req_rw *)r;
    uint8_t *p = (uint8_t *)q->addr;
    r->status = RS_DONE;
    switch (r->cmd) {
    case CMD_INIT: break;
    case CMD_READ: {
        struct armregs a = { 0 };
        a.r0 = 0x0000;
        kint(0x1A, &a);
        if (a.r0 & 0xFF) clk_days++;                    /* passed midnight */
        uint32_t t = ((a.r2 & 0xFFFF) << 16) | (a.r3 & 0xFFFF);
        /* ticks -> hundredths: 65536 / 1193180 s per tick */
        uint32_t hund = (uint32_t)(((uint64_t)t * 6553600u) / 1193180u);
        uint32_t secs = hund / 100;
        p[0] = clk_days; p[1] = clk_days >> 8;
        p[2] = (secs / 60) % 60;                        /* minutes */
        p[3] = secs / 3600;                             /* hours */
        p[4] = hund % 100;                              /* hundredths */
        p[5] = secs % 60;                               /* seconds */
        break;
    }
    case CMD_WRITE:
    case CMD_WRITEV: {
        clk_days = p[0] | (p[1] << 8);
        uint32_t hund = ((p[3] * 60u + p[2]) * 60u + p[5]) * 100u + p[4];
        uint32_t t = (uint32_t)(((uint64_t)hund * 1193180u) / 6553600u);
        struct armregs a = { 0 };
        a.r0 = 0x0100;
        a.r2 = t >> 16;
        a.r3 = t & 0xFFFF;
        kint(0x1A, &a);
        /* and the battery clock */
        memset(&a, 0, sizeof a);
        a.r0 = 0x0300;
        a.r2 = (tobcd(p[3]) << 8) | tobcd(p[2]);
        a.r3 = tobcd(p[5]) << 8;
        kint(0x1A, &a);
        int y, m, d;
        days_to_ymd(clk_days, &y, &m, &d);
        memset(&a, 0, sizeof a);
        a.r0 = 0x0500;
        a.r2 = (tobcd(y / 100) << 8) | tobcd(y % 100);
        a.r3 = (tobcd(m) << 8) | tobcd(d);
        kint(0x1A, &a);
        break;
    }
    case CMD_NDREAD:
        r->status |= RS_BUSY;
        break;
    case CMD_INSTAT: case CMD_OUTSTAT: case CMD_INFLUSH: case CMD_OUTFLUSH: case CMD_OPEN: case CMD_CLOSE:
        break;
    default:
        r->status |= RS_ERROR | DE_BADCMD;
    }
}

struct devhdr clock_dev = { &disk_dev, DEVA_CHAR | DEVA_CLOCK, 0, clk_strategy, clk_interrupt, "CLOCK$  " };

void clock_init(void)
{
    struct armregs a = { 0 };
    a.r0 = 0x0400;
    kint(0x1A, &a);
    int y = bcd((a.r2 >> 8) & 0xFF) * 100 + bcd(a.r2 & 0xFF);
    int m = bcd((a.r3 >> 8) & 0xFF), d = bcd(a.r3 & 0xFF);
    if (y < 1980 || y > 2099 || m < 1 || m > 12 || d < 1 || d > 31) { y = 1980; m = 1; d = 1; }
    clk_days = ymd_to_days(y, m, d);
    /* the midnight flag from before we started counting */
    a.r0 = 0x0000;
    kint(0x1A, &a);
}
