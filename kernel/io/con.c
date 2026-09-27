/*
 * con.c - the CON device (MSCON.ASM): keyboard through INT 16h, screen
 * through INT 10h, plus INT 29h (fast console output) and INT 1Bh (Ctrl-Break).
 */
#include "iosys.h"

static void con_strategy(struct reqhdr *r);
static void con_interrupt(void);

struct devhdr con_dev = {
    &aux_dev, DEVA_CHAR | DEVA_STDIN | DEVA_STDOUT | DEVA_SPECIAL | DEVA_GENIOCTL, 0,
    con_strategy, con_interrupt, "CON     "
};

static struct reqhdr *con_req;
static volatile uint8_t altah;          /* pending second byte / ^C from Ctrl-Break */
static uint8_t kbd_read_fn = 0x00;      /* 00h or 10h (enhanced keyboard) */

static void con_strategy(struct reqhdr *r) { con_req = r; }

/* INT 16h status: -1 if no key, else the key (0x0000 break keys are eaten) */
static int kbd_peek(void)
{
    for (;;) {
        struct armregs r = { 0 };
        r.r0 = (kbd_read_fn + 1) << 8;
        kint(0x16, &r);
        if (r.cpsr & CPSR_Z) return -1;
        if ((r.r0 & 0xFFFF) != 0) return r.r0 & 0xFFFF;
        r.r0 = kbd_read_fn << 8;        /* the 0000h Ctrl-Break stuffs */
        kint(0x16, &r);
    }
}

static int kbd_get(void)
{
    for (;;) {
        struct armregs r = { 0 };
        r.r0 = kbd_read_fn << 8;
        kint(0x16, &r);
        if ((r.r0 & 0xFFFF) != 0) return r.r0 & 0xFFFF;
    }
}

static int key_ascii(int k)
{
    int al = k & 0xFF;
    if (al == 0xE0 && (k >> 8)) al = 0;         /* grey keys of the enhanced keyboard */
    return al;
}

static int con_getc(void)
{
    if (altah) { int c = altah; altah = 0; return c; }
    int k = kbd_get();
    int al = key_ascii(k);
    if (al == 0) altah = (k >> 8) & 0xFF;
    return al;
}

static void tty(int c)
{
    struct armregs r = { 0 };
    r.r0 = 0x0E00 | (c & 0xFF);
    r.r1 = 0x0007;
    kint(0x10, &r);
}

static void con_interrupt(void)
{
    struct reqhdr *r = con_req;
    r->status = RS_DONE;
    switch (r->cmd) {
    case CMD_INIT:
        ((struct req_init *)r)->units = 0;
        break;
    case CMD_READ: {
        struct req_rw *q = (struct req_rw *)r;
        uint8_t *p = (uint8_t *)q->addr;
        for (unsigned i = 0; i < q->count; i++) p[i] = con_getc();
        break;
    }
    case CMD_NDREAD: {
        struct req_ndread *q = (struct req_ndread *)r;
        if (altah) { q->ch = altah; break; }
        int k = kbd_peek();
        if (k < 0) r->status |= RS_BUSY;
        else q->ch = key_ascii(k);
        break;
    }
    case CMD_INSTAT:
    case CMD_OUTSTAT:
    case CMD_OUTFLUSH:
    case CMD_OPEN:
    case CMD_CLOSE:
        break;
    case CMD_INFLUSH:
        altah = 0;
        while (kbd_peek() >= 0) kbd_get();
        break;
    case CMD_WRITE:
    case CMD_WRITEV: {
        struct req_rw *q = (struct req_rw *)r;
        const uint8_t *p = (const uint8_t *)q->addr;
        for (unsigned i = 0; i < q->count; i++) tty(p[i]);
        break;
    }
    case CMD_GENIOCTL: {
        /* category 3 (CON): 7Fh get / 5Fh set display information (MODE CON) */
        struct req_gioctl *q = (struct req_gioctl *)r;
        uint8_t *d = (uint8_t *)q->data;
        if (q->category == 3 && q->minor == 0x7F) {
            /* level, res, length 14, flags, mode, res, colours,
               pixel columns, pixel rows, character columns, character rows */
            int mode = BDA8(0x49);
            uint16_t cols = BDA16(0x4A), rows = BDA8(0x84) + 1;
            uint16_t pc = mode == 6 ? 640 : 320, pr = 200, colours = mode == 6 ? 2 : mode == 0x13 ? 256 : mode >= 4 ? 4 : 16;
            if (mode <= 3) { pc = cols * 9; pr = rows * 16; }
            memset(d, 0, 18);
            d[2] = 14;
            d[6] = mode <= 3 ? 1 : 2;
            d[8] = colours; d[9] = colours >> 8;
            d[10] = pc; d[11] = pc >> 8;
            d[12] = pr; d[13] = pr >> 8;
            d[14] = cols; d[15] = cols >> 8;
            d[16] = rows; d[17] = rows >> 8;
        } else if (q->category == 3 && q->minor == 0x5F) {
            /* only 80x25 exists */
        } else r->status |= RS_ERROR | DE_BADCMD;
        break;
    }
    default:
        r->status |= RS_ERROR | DE_BADCMD;
        break;
    }
}

/* INT 29h: fast console output (AL) */
static void int29_handler(struct armregs *f)
{
    tty(f->r0);
}

/* INT 1Bh: Ctrl-Break - the next console read returns ^C */
static void int1b_handler(struct armregs *f)
{
    (void)f;
    altah = 3;
}

void con_init(int enhanced)
{
    kbd_read_fn = enhanced ? 0x10 : 0x00;
    IVT[0x29] = (uint32_t)int29_handler;
    IVT[0x1B] = (uint32_t)int1b_handler;
}
