/* comm.h - interrupt-driven 16550 UART driver (COM1-COM4), the way DOS comm
 * programs of 1989 did it: hook the IRQ vector (INT 0Bh for IRQ3 / 0Ch for
 * IRQ4), unmask the PIC, set OUT2, and move bytes between the UART and two
 * ring buffers in the interrupt handler. Shared by TERM, BBS and the door. */
#ifndef COMM_H
#define COMM_H
#include <stdint.h>

#define COM_RXSIZE 8192u      /* powers of two */
#define COM_TXSIZE 4096u

/* MSR bits */
#define MSR_DCTS 0x01
#define MSR_DDSR 0x02
#define MSR_TERI 0x04
#define MSR_DDCD 0x08
#define MSR_CTS  0x10
#define MSR_DSR  0x20
#define MSR_RI   0x40
#define MSR_DCD  0x80

/* port: 1-4. Returns 0 on success, -1 if no UART answers at that address. */
int  com_open(int port, long baud);
/* keep_dtr: leave DTR/RTS up (a BBS going to a door, a door returning to the
 * BBS); otherwise DTR drops, which hangs up a modem configured with &D2. */
void com_close(int keep_dtr);
void com_setbaud(long baud);
void com_dtr(int on);
int  com_carrier(void);          /* DCD */
int  com_ring(void);             /* RI asserted or a trailing-edge seen since last call */
uint8_t com_msr(void);

int  com_getc(void);             /* -1 if nothing waiting */
int  com_avail(void);            /* bytes waiting in the RX ring */
int  com_putc(int c);            /* waits while the TX ring is full; -1 if carrier-less wait times out */
void com_write(const void *buf, int n);
void com_puts(const char *s);
int  com_txfree(void);
int  com_txpending(void);        /* bytes not yet on the wire (ring + UART FIFO) */
void com_txflush(void);          /* wait until everything is sent (max ~10 s) */
void com_rxpurge(void);
void com_txpurge(void);
void com_poll(void);             /* belt and braces: service the UART by polling too */

extern int com_base, com_irq, com_portno;
extern long com_baud;
extern volatile uint32_t com_rxcount, com_txcount, com_overruns;

/* BIOS tick helpers (18.2 Hz) */
/* BIOS ticks (40:6C, 18.2 Hz) made monotonic: the BIOS count restarts at 0 at midnight
   (1800B0h ticks a day), which turned every "TICKS() - t0" across midnight into ~4e9 - a long
   emulated run (hours of ZMODEM) showed "CPS: -828" and "Online 00:00:00". */
static inline uint32_t mono_ticks(void)
{
    static uint32_t last, base;
    uint32_t t = *(volatile uint32_t *)0x46C;
    if (t < last) base += 0x1800B0u;
    last = t;
    return base + t;
}
#define TICKS() mono_ticks()
static inline uint32_t ms2ticks(uint32_t ms) { return (ms * 182u + 9999u) / 10000u; }
void idle(void);                 /* WFI: sleep until the next interrupt */
void delay_ms(uint32_t ms);
#endif
