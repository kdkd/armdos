/* ARM/AT BIOS — timer, clock, CMOS, speaker, and the INT 15h system services */
#include "bios.h"

#define PIT_HZ 1193182u
#define TICKS_PER_DAY 0x1800B0u

static volatile uint32_t beep_ticks;    /* speaker auto-off countdown */

uint8_t cmos_read(uint8_t reg)
{
    uint32_t s = irq_save();
    outb(0x70, reg);
    uint8_t v = inb(0x71);
    irq_restore(s);
    return v;
}

void cmos_write(uint8_t reg, uint8_t v)
{
    uint32_t s = irq_save();
    outb(0x70, reg);
    outb(0x71, v);
    irq_restore(s);
}

int bcd2bin(int v) { return (v >> 4) * 10 + (v & 15); }
int bin2bcd(int v) { return ((v / 10) << 4) | (v % 10); }

uint32_t ticks(void) { return BDA32(BDA_TICKS); }

void timer_init(void)
{
    outb(0x43, 0x36);           /* ch0, lo/hi, mode 3 */
    outb(0x40, 0);
    outb(0x40, 0);              /* divisor 65536 -> 18.2065 Hz */
    int h = bcd2bin(cmos_read(4)), m = bcd2bin(cmos_read(2)), s = bcd2bin(cmos_read(0));
    uint32_t secs = h * 3600 + m * 60 + s;
    BDA32(BDA_TICKS) = (uint32_t)(((uint64_t)secs * PIT_HZ) / 65536);
    BDA8(BDA_MIDNIGHT) = 0;
}

void delay_ticks(uint32_t n)
{
    uint32_t start = ticks();
    irq_enable();
    while (ticks() - start < n) wfi();
}

void speaker(int hz)
{
    if (hz <= 0) { outb(0x61, inb(0x61) & ~3); return; }
    uint32_t div = PIT_HZ / hz;
    outb(0x43, 0xB6);
    outb(0x42, div & 0xFF);
    outb(0x42, div >> 8);
    outb(0x61, inb(0x61) | 3);
}

/* non-blocking: the tone is switched off by the timer tick */
void beep(int hz, int ms)
{
    speaker(hz);
    beep_ticks = (ms + 54) / 55;
    if (!beep_ticks) beep_ticks = 1;
}

void int08_handler(struct armregs *f)
{
    (void)f;
    uint32_t t = BDA32(BDA_TICKS) + 1;
    if (t >= TICKS_PER_DAY) { t = 0; BDA8(BDA_MIDNIGHT) = 1; }
    BDA32(BDA_TICKS) = t;
    if (beep_ticks && --beep_ticks == 0) outb(0x61, inb(0x61) & ~3);
    struct armregs r = { 0 };
    bios_int(0x1C, &r);
    outb(0x20, 0x20);
}

void iret_handler(struct armregs *f) { (void)f; }

void int1a_handler(struct armregs *f)
{
    set_cf(f, 0);
    switch (AH(f)) {
    case 0x00: {
        uint32_t s = irq_save();
        uint32_t t = BDA32(BDA_TICKS);
        set_al(f, BDA8(BDA_MIDNIGHT));
        BDA8(BDA_MIDNIGHT) = 0;
        irq_restore(s);
        f->r2 = t >> 16;
        f->r3 = t & 0xFFFF;
        break;
    }
    case 0x01:
        BDA32(BDA_TICKS) = ((f->r2 & 0xFFFF) << 16) | (f->r3 & 0xFFFF);
        BDA8(BDA_MIDNIGHT) = 0;
        break;
    case 0x02:
        set_ch(f, cmos_read(4)); set_cl(f, cmos_read(2)); set_dh(f, cmos_read(0)); set_dl(f, 0);
        break;
    case 0x03:
        cmos_write(4, CH(f)); cmos_write(2, CL(f)); cmos_write(0, DH(f));
        break;
    case 0x04:
        set_ch(f, cmos_read(0x32)); set_cl(f, cmos_read(9)); set_dh(f, cmos_read(8)); set_dl(f, cmos_read(7));
        break;
    case 0x05:
        cmos_write(0x32, CH(f)); cmos_write(9, CL(f)); cmos_write(8, DH(f)); cmos_write(7, DL(f));
        break;
    default:
        set_cf(f, 1);
        break;
    }
}

/* ------------------------------------------------------------ INT 15h */

typedef void (*mouse_fn)(uint32_t status, int32_t dx, int32_t dy, int32_t dz);
static mouse_fn mouse_handler;
static uint8_t mouse_enabled, mouse_pkt[4], mouse_n;

static const uint8_t sys_config[10] = { 8, 0, 0xFC, 0x01, 0x00, 0x74, 0x00, 0x00, 0x00, 0x00 };

static void aux_write(uint8_t v)
{
    outb(0x64, 0xD4);
    outb(0x60, v);
    for (int i = 0; i < 1000 && (inb(0x64) & 0x21) != 0x21; i++) ;
    if ((inb(0x64) & 0x21) == 0x21) (void)inb(0x60);   /* the ACK */
}

void int74_handler(struct armregs *f)
{
    (void)f;
    while ((inb(0x64) & 0x21) == 0x21) {
        uint8_t b = inb(0x60);
        if (mouse_n == 0 && !(b & 0x08)) continue;     /* resync on bit 3 */
        mouse_pkt[mouse_n++] = b;
        if (mouse_n == 3) {
            mouse_n = 0;
            if (mouse_handler && mouse_enabled) {
                int32_t dx = mouse_pkt[1], dy = mouse_pkt[2];
                if (mouse_pkt[0] & 0x10) dx -= 256;
                if (mouse_pkt[0] & 0x20) dy -= 256;
                mouse_handler(mouse_pkt[0], dx, dy, 0);
            }
        }
    }
    outb(0xA0, 0x20);
    outb(0x20, 0x20);
}

/* ---- the game adapter (201h): the resistive inputs as the AT BIOS's TEST_CORD measured
 * them (fire the one-shots, poll until the bits drop, give up after 4FFh polls = 0), with
 * the counts turned into the AT's units: PIT clocks / 8 (8254 channel 0 in the BIOS's
 * mode 3 steps by 2 per clock, and TEST_CORD divided the difference by 16), 6.704 us
 * each, so 0-100 kOhm read about 3-168 whatever the CPU clock. One fire measures all four
 * axes (TEST_CORD fired once per axis); the polling loop below is 14 instructions plus
 * the IN's 1 us ISA cycle (ARCH.md 4.4), which is what the conversion uses. Interrupts
 * are off while it counts (TEST_CORD's CLI). An axis with nothing plugged in never ends
 * its pulse: it reads 0 and is left out of the next calls (no 1.3 ms wait for it each
 * time) until its bit is seen low again, i.e. a stick was plugged in.
 * See README.md. */
#define JOY_LIMIT 0x4FF
static uint8_t joy_open;         /* axes found open (no potentiometer) */

/* fire, then poll: counts[i] = polls that saw axis i timing; returns the polls made */
static uint32_t __attribute__((naked, noinline)) joy_poll(volatile uint8_t *p, uint32_t mask, uint32_t *counts)
{
    __asm__ volatile(
        "   push  {r4-r8, lr}\n"
        "   mov   r3, #0\n"
        "   mov   r4, #0\n"
        "   mov   r5, #0\n"
        "   mov   r6, #0\n"
        "   mov   r7, #0\n"
        "   mov   r8, #0x500\n"
        "   sub   r8, r8, #1\n"            /* JOY_LIMIT */
        "   strb  r3, [r0]\n"              /* fire the one-shots */
        "1: ldrb  ip, [r0]\n"
        "   tst   ip, #1\n"
        "   addne r4, r4, #1\n"
        "   tst   ip, #2\n"
        "   addne r5, r5, #1\n"
        "   tst   ip, #4\n"
        "   addne r6, r6, #1\n"
        "   tst   ip, #8\n"
        "   addne r7, r7, #1\n"
        "   tst   ip, r1\n"
        "   beq   2f\n"
        "   add   r3, r3, #1\n"
        "   cmp   r3, r8\n"
        "   blo   1b\n"
        "2: stmia r2, {r4-r7}\n"
        "   mov   r0, r3\n"
        "   pop   {r4-r8, pc}\n");
}

static void joy_axes(uint32_t out[4])
{
    volatile uint8_t *p = (volatile uint8_t *)(0x10000000 + JOY_PORT);
    uint32_t counts[4], n, mask;
    joy_open &= *p;                                 /* an open axis whose bit dropped has a stick now */
    mask = 15 & ~joy_open;
    /* the 558 is not retriggerable: let the pulses of the last fire end first */
    for (n = 0; n < JOY_LIMIT && (*p & mask); n++) ;
    joy_open |= *p & mask;                          /* never ended: open */
    mask = 15 & ~joy_open;
    uint32_t s = irq_save();
    n = mask ? joy_poll(p, mask, counts) : 0;
    irq_restore(s);
    uint32_t mhz = inb(0xF1);                       /* the clock now (turbo switch), port F1h */
    if (mhz == 0 || mhz == 0xFF) mhz = 100;
    uint32_t ns = 1000 + (14000 + mhz / 2) / mhz;   /* one poll: the ISA cycle + 14 instructions */
    for (int i = 0; i < 4; i++) {
        uint32_t b = 1u << i;
        if (!(mask & b) || (n >= JOY_LIMIT && counts[i] >= n)) { if (mask & b) joy_open |= b; out[i] = 0; continue; }
        out[i] = (counts[i] * ns + 3352) / 6704;    /* 8 / 1.193182 MHz = 6.704 us */
    }
}

void int15_handler(struct armregs *f)
{
    set_cf(f, 0);
    switch (AH(f)) {
    case 0x4F:          /* keyboard intercept: process the key */
        set_cf(f, 1);
        break;
    case 0x24:          /* A20: always enabled on this machine */
        if (AL(f) == 0x02) set_al(f, 1);
        else if (AL(f) == 0x03) f->r1 = 0x0003;
        set_ah(f, 0);
        break;
    case 0x84:          /* joystick support (the game adapter at 201h) */
        if (!(BDA16(BDA_EQUIP) & EQUIP_GAME)) { set_ah(f, 0x86); set_cf(f, 1); break; }
        if ((f->r3 & 0xFFFF) == 0) {                /* DX=0: the switches, AL bits 4-7 (0 = pressed) */
            set_al(f, inb(JOY_PORT) & 0xF0);
        } else if ((f->r3 & 0xFFFF) == 1) {         /* DX=1: the resistive inputs A(x) A(y) B(x) B(y) */
            uint32_t v[4];
            joy_axes(v);
            f->r0 = v[0]; f->r1 = v[1]; f->r2 = v[2]; f->r3 = v[3];
        } else { set_ah(f, 0x86); set_cf(f, 1); }
        break;
    case 0x86: {        /* wait CX:DX microseconds */
        uint32_t us = ((f->r2 & 0xFFFF) << 16) | (f->r3 & 0xFFFF);
        uint32_t n = (us + 27000) / 54925;
        delay_ticks(n);
        break;
    }
    case 0x87: {        /* move block: the GDT descriptors hold 24/32-bit linear addresses */
        const uint8_t *g = (const uint8_t *)f->r4;
        uint32_t src = g[0x12] | (g[0x13] << 8) | (g[0x14] << 16) | (g[0x17] << 24);
        uint32_t dst = g[0x1A] | (g[0x1B] << 8) | (g[0x1C] << 16) | (g[0x1F] << 24);
        memmove((void *)dst, (const void *)src, (f->r2 & 0xFFFF) * 2);
        set_ah(f, 0);
        break;
    }
    case 0x88:
        set_ax(f, (ram_end - HMA_END) / 1024);
        break;
    case 0x90: case 0x91:
        set_ah(f, 0);
        break;
    case 0xC0:
        f->r1 = (uint32_t)sys_config;
        set_ah(f, 0);
        break;
    case 0xC2:
        if (!(BDA16(BDA_EQUIP) & 4)) {      /* POST found no pointing device: interface error */
            set_ah(f, 0x03); set_cf(f, 1);
            break;
        }
        set_ah(f, 0);
        switch (AL(f)) {
        case 0x00:
            mouse_enabled = BH(f) ? 1 : 0;
            aux_write(mouse_enabled ? 0xF4 : 0xF5);
            mouse_n = 0;
            break;
        case 0x01: f->r1 = 0x00AA; mouse_n = 0; aux_write(0xFF); break;
        case 0x04: set_bh(f, 0); break;
        case 0x05: mouse_n = 0; break;
        case 0x07: mouse_handler = (mouse_fn)f->r1; break;
        default: break;
        }
        break;
    case 0xE8:
        if (AL(f) == 0x01) {
            f->r0 = f->r2 = (ram_end - EXT_MEM_START) / 1024;
            f->r1 = f->r3 = 0;
        } else { set_ah(f, 0x86); set_cf(f, 1); }
        break;
    default:
        set_ah(f, 0x86);
        set_cf(f, 1);
        break;
    }
}
