/* comm.c - interrupt-driven 16550 driver, see comm.h. */
#include <dos.h>
#include <armdos.h>
#include "comm.h"

int com_base = 0x2F8, com_irq = 3, com_portno = 2;
long com_baud = 2400;
volatile uint32_t com_rxcount, com_txcount, com_overruns;

static volatile uint8_t rxbuf[COM_RXSIZE];
static volatile uint8_t txbuf[COM_TXSIZE];
static volatile uint32_t rxhead, rxtail, txhead, txtail;
static volatile int txbusy;
static volatile uint8_t msr_latch, msr_edges;
static armdos_vect_t oldvec;
static int is_open;
static uint8_t oldmask_bit;

static const int bases[4] = { 0x3F8, 0x2F8, 0x3E8, 0x2E8 };
static const int irqs[4]  = { 4, 3, 4, 3 };

#define IN(r)     armdos_inb(com_base + (r))
#define OUT(r, v) armdos_outb(com_base + (r), (uint8_t)(v))

/* Move bytes: RX FIFO -> ring, ring -> TX FIFO. Called from the ISR (IRQs
 * off) and from com_poll with IRQs disabled. */
static void service(void)
{
    int guard = 64;
    while (guard--) {
        uint8_t iir = IN(2);
        uint8_t lsr = IN(5);
        if (lsr & 0x02) com_overruns++;
        while (lsr & 0x01) {
            uint8_t c = IN(0);
            uint32_t h = rxhead;
            if (((h + 1) & (COM_RXSIZE - 1)) != rxtail) { rxbuf[h] = c; rxhead = (h + 1) & (COM_RXSIZE - 1); }
            else com_overruns++;
            com_rxcount++;
            lsr = IN(5);
        }
        if (lsr & 0x20) {                 /* THR (and FIFO) empty: refill */
            int n = 16;
            if (txhead == txtail) txbusy = 0;
            while (n-- && txhead != txtail) {
                OUT(0, txbuf[txtail]);
                txtail = (txtail + 1) & (COM_TXSIZE - 1);
                com_txcount++;
                txbusy = 1;
            }
        }
        if ((iir & 0x0F) == 0x00) {       /* modem status */
            uint8_t m = IN(6);
            msr_latch = m;
            msr_edges |= m & 0x0F;
        }
        if (iir & 1) break;               /* nothing (more) pending */
    }
}

static void com_isr(struct armregs *f)
{
    (void)f;
    service();
    armdos_outb(0x20, 0x20);             /* EOI */
}

void com_poll(void)
{
    if (!is_open) return;
    armdos_disable();
    uint8_t lsr = IN(5);
    if ((lsr & 0x01) || ((lsr & 0x20) && txhead != txtail)) service();
    armdos_enable();
}

void com_setbaud(long baud)
{
    unsigned div = (unsigned)(115200L / (baud ? baud : 2400));
    uint8_t lcr = IN(3);
    OUT(3, lcr | 0x80);
    OUT(0, div & 0xFF);
    OUT(1, div >> 8);
    OUT(3, lcr & 0x7F);
    com_baud = baud;
}

int com_open(int port, long baud)
{
    if (port < 1 || port > 4) return -1;
    if (is_open) com_close(1);
    com_portno = port;
    com_base = bases[port - 1];
    com_irq = irqs[port - 1];
    /* is there a UART? the scratch register must hold a value */
    OUT(7, 0x5A);
    if (IN(7) != 0x5A) return -1;
    OUT(1, 0);                            /* IER off while we set up */
    OUT(3, 0x03);                         /* 8N1 */
    com_setbaud(baud);
    OUT(2, 0xC7);                         /* FIFO on, clear both, trigger 14 */
    OUT(2, 0x87);                         /* trigger 8 */
    rxhead = rxtail = txhead = txtail = 0; txbusy = 0;
    (void)IN(0); (void)IN(5); (void)IN(2);
    msr_latch = IN(6); msr_edges = 0;
    oldvec = _dos_getvect(8 + com_irq);
    _dos_setvect(8 + com_irq, com_isr);
    armdos_disable();
    uint8_t mask = armdos_inb(0x21);
    oldmask_bit = mask & (1 << com_irq);
    armdos_outb(0x21, mask & ~(1 << com_irq));
    armdos_enable();
    OUT(4, 0x0B);                         /* DTR, RTS, OUT2 */
    OUT(1, 0x0F);                         /* RX, THRE, line status, modem status */
    is_open = 1;
    com_poll();
    return 0;
}

void com_close(int keep_dtr)
{
    if (!is_open) return;
    if (!keep_dtr) com_txflush();
    OUT(1, 0);
    armdos_disable();
    uint8_t mask = armdos_inb(0x21);
    armdos_outb(0x21, mask | oldmask_bit);
    armdos_enable();
    OUT(4, keep_dtr ? 0x03 : 0x00);       /* OUT2 off; DTR/RTS as asked */
    _dos_setvect(8 + com_irq, oldvec);
    is_open = 0;
}

void com_dtr(int on)
{
    uint8_t m = IN(4);
    OUT(4, on ? (m | 0x01) : (m & ~0x01));
}

uint8_t com_msr(void)
{
    uint8_t m = IN(6);
    msr_latch = m;
    msr_edges |= m & 0x0F;
    return m;
}
int com_carrier(void) { return (com_msr() & MSR_DCD) != 0; }
int com_ring(void)
{
    uint8_t m = com_msr();
    int r = (m & MSR_RI) || (msr_edges & MSR_TERI);
    msr_edges &= ~MSR_TERI;
    return r;
}

int com_getc(void)
{
    if (rxhead == rxtail) { com_poll(); if (rxhead == rxtail) return -1; }
    uint8_t c = rxbuf[rxtail];
    rxtail = (rxtail + 1) & (COM_RXSIZE - 1);
    return c;
}
int com_avail(void) { com_poll(); return (int)((rxhead - rxtail) & (COM_RXSIZE - 1)); }
int com_txfree(void) { return (int)(COM_TXSIZE - 1 - ((txhead - txtail) & (COM_TXSIZE - 1))); }
int com_txpending(void)
{
    int n = (int)((txhead - txtail) & (COM_TXSIZE - 1));
    if (n == 0 && !(IN(5) & 0x40)) n = 1;  /* transmitter not yet empty */
    return n;
}

static void kick(void)
{
    armdos_disable();
    if (IN(5) & 0x20) service();
    armdos_enable();
}

int com_putc(int c)
{
    uint32_t start = TICKS();
    while (((txhead + 1) & (COM_TXSIZE - 1)) == txtail) {
        kick();
        if (((txhead + 1) & (COM_TXSIZE - 1)) != txtail) break;
        if (TICKS() - start > 20 * 18) return -1;   /* stuck for 20 s */
        idle();
    }
    txbuf[txhead] = (uint8_t)c;
    txhead = (txhead + 1) & (COM_TXSIZE - 1);
    if (!txbusy) kick();
    return 0;
}

void com_write(const void *buf, int n)
{
    const uint8_t *p = buf;
    while (n-- > 0) if (com_putc(*p++) < 0) return;
    kick();
}
void com_puts(const char *s) { while (*s) if (com_putc((uint8_t)*s++) < 0) return; kick(); }

void com_txflush(void)
{
    uint32_t start = TICKS();
    while (com_txpending() && TICKS() - start < 10 * 18) { kick(); idle(); }
}
void com_rxpurge(void) { com_poll(); rxtail = rxhead; }
void com_txpurge(void) { armdos_disable(); txtail = txhead; armdos_enable(); OUT(2, 0x87 | 0x04); }

void idle(void)
{
    /* sleep until an interrupt (timer at 18.2 Hz, keyboard, UART) */
    armdos_halt();
}

void delay_ms(uint32_t ms)
{
    uint32_t t = ms2ticks(ms), start = TICKS();
    while (TICKS() - start < t) idle();
}
