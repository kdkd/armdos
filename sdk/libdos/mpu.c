/* mpu.c - MPU-401 (UART mode) and the millisecond MIDI clock (midi.h). */
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <armdos.h>
#include <midi.h>

#define inb(p)     armdos_inb(p)
#define outb(p, v) armdos_outb((p), (uint8_t)(v))

#define STAT_DRR 0x40      /* 1 = not ready for a command/data byte */
#define STAT_DSR 0x80      /* 1 = no byte to read */

mpu_card mpu_info = { 0x330, 9, 0, 0 };

/* about n x 15 us (port 61h bit 4, the AT refresh toggle) */
static void wait15(unsigned n)
{
    unsigned b = inb(0x61) & 0x10;
    while (n--) {
        unsigned guard = 100000;
        while ((inb(0x61) & 0x10) == b && --guard) ;
        b ^= 0x10;
    }
}

static unsigned hexfield(const char *s)
{
    unsigned v = 0;
    for (;; s++) {
        unsigned d;
        if (*s >= '0' && *s <= '9') d = *s - '0';
        else if (*s >= 'a' && *s <= 'f') d = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'F') d = *s - 'A' + 10;
        else break;
        v = v * 16 + d;
    }
    return v;
}

/* BLASTER's P field: the MPU-401 port (SB16 convention) */
static unsigned blaster_mpu_port(void)
{
    const char *b0 = getenv("BLASTER"), *b;
    if (b0)
        for (b = b0; *b; b++)
            if ((*b == 'P' || *b == 'p') && (b == b0 || b[-1] == ' ' || b[-1] == '\t'))
                return hexfield(b + 1);
    return 0x330;
}

/* wait until the MPU accepts a byte; 0 on timeout (~30 ms) */
static int wait_ready(void)
{
    unsigned n;
    for (n = 0; n < 2000; n++) {
        unsigned s = inb(mpu_info.port + 1);
        if (!(s & STAT_DRR)) return 1;
        if (!(s & STAT_DSR)) (void)inb(mpu_info.port);     /* drain incoming data */
        wait15(1);
    }
    return 0;
}

/* wait for an ACK (FEh) after a command; 0 on timeout (~50 ms) */
static int wait_ack(void)
{
    unsigned n;
    for (n = 0; n < 3300; n++) {
        if (!(inb(mpu_info.port + 1) & STAT_DSR)) {
            if (inb(mpu_info.port) == 0xFE) return 1;
            continue;
        }
        wait15(1);
    }
    return 0;
}

static int command(unsigned char c)
{
    if (!wait_ready()) return 0;
    outb(mpu_info.port + 1, c);
    return wait_ack();
}

int mpu_reset(void)
{
    mpu_info.uart = 0;
    if (command(0xFF)) return 1;
    return command(0xFF);          /* a UART-mode MPU leaves UART mode without an ACK */
}

int mpu_detect(mpu_card *card)
{
    mpu_info.port = blaster_mpu_port();
    mpu_info.irq = 9;
    mpu_info.uart = 0;
    mpu_info.present = inb(mpu_info.port + 1) != 0xFF || wait_ready();
    if (mpu_info.present) mpu_info.present = mpu_reset();
    if (card) *card = mpu_info;
    return mpu_info.present;
}

static void atexit_shutdown(void) { mpu_shutdown(); }

int mpu_uart(void)
{
    static int registered;
    if (!mpu_info.present) return 0;
    if (mpu_info.uart) return 1;
    if (!command(0x3F)) return 0;
    mpu_info.uart = 1;
    if (!registered) { atexit(atexit_shutdown); registered = 1; }
    return 1;
}

void mpu_write(unsigned char b)
{
    if (!mpu_info.present) return;
    wait_ready();
    outb(mpu_info.port, b);
}

void mpu_send(const void *bytes, unsigned n)
{
    const unsigned char *p = bytes;
    while (n--) mpu_write(*p++);
}

void mpu_msg(unsigned status, unsigned d1, unsigned d2)
{
    mpu_write((unsigned char)status);
    mpu_write((unsigned char)(d1 & 0x7F));
    if ((status & 0xE0) != 0xC0) mpu_write((unsigned char)(d2 & 0x7F));
}

void mpu_all_notes_off(void)
{
    unsigned ch;
    if (!mpu_info.present || !mpu_info.uart) return;
    for (ch = 0; ch < 16; ch++) {
        mpu_msg(0xB0 | ch, 64, 0);     /* sustain off */
        mpu_msg(0xB0 | ch, 123, 0);    /* all notes off */
        mpu_msg(0xB0 | ch, 120, 0);    /* all sound off */
        mpu_msg(0xB0 | ch, 121, 0);    /* reset all controllers */
    }
}

void mpu_shutdown(void)
{
    if (!mpu_info.present) return;
    if (mpu_info.uart) mpu_all_notes_off();
    mpu_reset();
}

/* ------------------------------------------------------------ the clock */
#define PIT_HZ   1193182u
#define DIVISOR  1193u              /* 1000.15 Hz */

static armdos_vect_t old08;
static volatile uint32_t ticks;
static uint32_t bios_acc;
static int timer_on;

static void int08(struct armregs *f)
{
    ticks++;
    bios_acc += DIVISOR;
    if (bios_acc >= 65536u) {          /* the BIOS tick keeps its 18.2 Hz (it sends the EOI) */
        bios_acc -= 65536u;
        if (old08) { old08(f); return; }
    }
    outb(0x20, 0x20);
}

static void set_divisor(unsigned mode, unsigned d)
{
    armdos_disable();
    outb(0x43, mode);
    outb(0x40, d & 0xFF);
    outb(0x40, (d >> 8) & 0xFF);
    armdos_enable();
}

static void atexit_timer(void) { midi_timer_stop(); }

int midi_timer_start(void)
{
    static int registered;
    if (timer_on) return 1;
    ticks = 0; bios_acc = 0;
    old08 = armdos_getvect(0x08);
    armdos_disable();
    armdos_setvect(0x08, int08);
    timer_on = 1;
    armdos_enable();
    set_divisor(0x34, DIVISOR);        /* channel 0, lo/hi, mode 2 */
    if (!registered) { atexit(atexit_timer); registered = 1; }
    return 1;
}

void midi_timer_stop(void)
{
    if (!timer_on) return;
    set_divisor(0x36, 0);              /* mode 3, 65536: the BIOS's 18.2 Hz */
    armdos_disable();
    armdos_setvect(0x08, old08);
    timer_on = 0;
    armdos_enable();
}

unsigned long midi_timer_us(void)
{
    return (unsigned long)((unsigned long long)ticks * (DIVISOR * 1000000ull) / PIT_HZ);
}
