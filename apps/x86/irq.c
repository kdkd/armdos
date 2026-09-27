/*
 * irq.c - hardware interrupts for the x86 program.
 *
 * The real IRQs arrive in the ARM world (ARCH.md 5): the BIOS's IRQ stub
 * calls the ARM IVT's INT 08h / 09h handler with IRQs off.  X86.EXE hooks
 * those vectors.  When the x86 program has hooked the same vector in its own
 * (x86) IVT and the x86 CPU is running, the ARM handler only records the IRQ
 * as pending - without an EOI, exactly as a PC's 8259 holds the line in
 * service - and the run loop raises it in the x86 world at the next
 * instruction boundary with IF=1.  The x86 handler reads port 60h, sends its
 * own EOI (OUT 20h,20h passes through to the real PIC), or chains to the
 * original vector, which is our stub: it hands the IRQ to the BIOS.  When the
 * program has not hooked the vector, the IRQ goes straight to the BIOS.
 *
 * INT 1Ch: the BIOS's INT 08h calls ARM INT 1Ch every tick; if the x86 IVT
 * has a 1Ch hook, an x86 INT 1Ch is made pending (games use it for timing).
 *
 * Keyboard: a program that reads port 60h in its INT 9 handler and then
 * chains to the BIOS has consumed the byte; the default INT 9 then puts it
 * back into the 8042 (command D2h, "write keyboard output buffer") and lets
 * the resulting IRQ1 go to the BIOS, which translates it as usual.
 *
 * Copyright (c) 2026 Europa Micro Systems. Part of ARM-DOS (apps/x86).
 */
#include <armdos.h>
#include "x86.h"
#include "dos86.h"
#include "jit.h"

volatile uint32_t irq_pending;
int trace_kb;
volatile int x86_active;
volatile int last_port60_read;
volatile int last_port60_value;

/* Keyboard bytes for the x86 program's own INT 9 handler.  The ARM IRQ1
 * handler takes each byte from the 8042 at once (and sends the EOI), so the
 * controller never waits on the slower x86 handler and nothing is lost or
 * reordered; the x86 program then gets one IRQ1 per byte, and IN 60h / IN 64h
 * show it the byte of the IRQ it is handling (repeatable, as a real 8042's
 * output buffer).  Chaining to the original INT 9 runs kbd86_process(). */
#define KBQ 32
static volatile uint8_t kbq[KBQ];
static volatile unsigned kbq_head, kbq_tail;
volatile int kbd_latched;        /* kbd_latch holds the byte of the current x86 IRQ1 */
volatile int kbd_unread;         /* ... IN 60h has not read it yet (OBF for IN 64h) */
volatile uint8_t kbd_latch;
extern void kbd86_process(uint8_t code);

static int kbq_pop(uint8_t *b)
{
    if (kbq_head == kbq_tail) return 0;
    *b = kbq[kbq_head % KBQ];
    kbq_head++;
    return 1;
}

static armdos_vect_t old08, old09, old1c, old1b, old23, old24;

/* The other hardware IRQs an x86 program may service itself: IRQ 3 and 4
 * (COM2/COM1 - terminal programs such as Kermit), 5 and 7.  Same scheme as
 * IRQ0: pending without an EOI while the program has hooked the vector. */
static const uint8_t hw_irq[4] = { 3, 4, 5, 7 };
static armdos_vect_t oldhw[4];
static int hw_index(int vec) { for (int i = 0; i < 4; i++) if (hw_irq[i] + 8 == vec) return i; return -1; }
static void hw_common(int i, struct armregs *f)
{
    if (x86_active && hle_hooked(hw_irq[i] + 8)) { irq_pending |= 0x10u << i; return; }
    int led = alt_led; alt_led_set(0);
    if (oldhw[i]) armdos_callold(oldhw[i], f); else armdos_outb(0x20, 0x20);
    alt_led_set(led);
}
static void h0b(struct armregs *f) { hw_common(0, f); }
static void h0c(struct armregs *f) { hw_common(1, f); }
static void h0d(struct armregs *f) { hw_common(2, f); }
static void h0f(struct armregs *f) { hw_common(3, f); }
static void (*const hw_handler[4])(struct armregs *) = { h0b, h0c, h0d, h0f };
static int installed;

/* saved hardware state, restored however X86.EXE ends */
static uint8_t pic_mask0, pic_mask1;
uint8_t pit_touched, speaker_touched;

int hle_hooked(int n)
{
    uint32_t v = *(volatile uint32_t *)(mem + n * 4);
    return v != HLE_VEC(n);
}

static inline uint32_t irqs_off(void)
{
    uint32_t s, t;
    __asm__ volatile("mrs %0, cpsr\n\torr %1, %0, #0x80\n\tmsr cpsr_c, %1" : "=r"(s), "=r"(t) :: "memory");
    return s;
}
static inline void irqs_restore(uint32_t s) { __asm__ volatile("msr cpsr_c, %0" :: "r"(s) : "memory"); }

volatile uint8_t alt_led;
void alt_led_set(int on)
{
    if (alt_led != on) { alt_led = (uint8_t)on; armdos_outb(0xF5, (uint8_t)on); }
}

static void h08(struct armregs *f)
{
    if (x86_active && hle_hooked(8)) { irq_pending |= PEND_IRQ0; return; }
    int led = alt_led; alt_led_set(0);
    old08(f);
    alt_led_set(led);
}

static void h09(struct armregs *f)
{
    if (x86_active && hle_hooked(9)) {
        uint8_t st = armdos_inb(0x64);
        if ((st & 0x21) == 0x01) {
            uint8_t b = armdos_inb(0x60);
            if (kbq_tail - kbq_head < KBQ) { kbq[kbq_tail % KBQ] = b; kbq_tail++; }
            if (trace_kb) dbg("q%02X ", b);
            irq_pending |= PEND_IRQ1;
        }
        armdos_outb(0x20, 0x20);
        return;
    }
    /* the BIOS gets it - after anything still queued for the x86 program */
    uint8_t b;
    while (kbq_pop(&b)) { if (trace_kb) dbg("d%02X ", b); kbd86_process(b); }
    if (trace_kb) dbg("B ");
    int led = alt_led; alt_led_set(0);
    old09(f);
    alt_led_set(led);
}

static void h1c(struct armregs *f)
{
    if (hle_hooked(0x1C)) irq_pending |= PEND_1C;
    armdos_callold(old1c, f);
}

static void h1b(struct armregs *f)
{
    if (hle_hooked(0x1B)) { irq_pending |= PEND_1B; return; }
    armdos_callold(old1b, f);
}

/* ^C seen by the kernel during one of our INT 21h calls: the call is
 * re-issued as a harmless AH=19h and dos86 raises the x86 INT 23h. */
volatile int ctrlc_hit;
static void h23(struct armregs *f)
{
    ctrlc_hit = 1;
    f->r0 = 0x1900;
    f->cpsr &= ~ARM_CPSR_C;
}

/* critical error: the x86 program's INT 24h if it has one, else the
 * parent's (COMMAND.COM's "Abort, Retry, Fail?"); Abort becomes Fail plus
 * an x86-level abort of the program, so the kernel never kills X86.EXE
 * behind our back. */
volatile int crit_abort;
static void h24(struct armregs *f)
{
    if (hle_hooked(0x24)) {
        uint32_t al = dos86_crit_x86(f);
        f->r0 = (f->r0 & ~0xFFu) | al;
        return;
    }
    armdos_callold(old24, f);
    if ((f->r0 & 0xFF) == 2) { crit_abort = 1; f->r0 = (f->r0 & ~0xFFu) | 3; }
}

void irq_install(void)
{
    if (installed) return;
    pic_mask0 = armdos_inb(0x21);
    pic_mask1 = armdos_inb(0xA1);
    old08 = armdos_getvect(0x08);
    old09 = armdos_getvect(0x09);
    old1c = armdos_getvect(0x1C);
    old1b = armdos_getvect(0x1B);
    old23 = armdos_getvect(0x23);
    old24 = armdos_getvect(0x24);
    armdos_setvect(0x08, h08);
    armdos_setvect(0x09, h09);
    armdos_setvect(0x1C, h1c);
    armdos_setvect(0x1B, h1b);
    armdos_setvect(0x23, h23);
    armdos_setvect(0x24, h24);
    for (int i = 0; i < 4; i++) { oldhw[i] = armdos_getvect(hw_irq[i] + 8); armdos_setvect(hw_irq[i] + 8, hw_handler[i]); }
    installed = 1;
}

/* Also called from the INT 22h trampoline after the kernel has ended us:
 * direct IVT writes, no DOS calls, nothing in extended memory. */
void x86_emergency_cleanup(void)
{
    if (!installed) return;
    installed = 0;
    x86_active = 0;
    alt_led_set(0);
    elbow_desc_clear();
    uint32_t s = irqs_off();
    volatile uint32_t *ivt = (volatile uint32_t *)0;
    ivt[0x08] = (uint32_t)old08;
    ivt[0x09] = (uint32_t)old09;
    ivt[0x1C] = (uint32_t)old1c;
    ivt[0x1B] = (uint32_t)old1b;
    for (int i = 0; i < 4; i++) ivt[hw_irq[i] + 8] = (uint32_t)oldhw[i];
    /* 23h/24h: the kernel restores them from our PSP */
    armdos_outb(0x21, pic_mask0);
    armdos_outb(0xA1, pic_mask1);
    if (pit_touched) {                   /* back to 18.2 Hz */
        armdos_outb(0x43, 0x36); armdos_outb(0x40, 0); armdos_outb(0x40, 0);
    }
    if (speaker_touched) armdos_outb(0x61, armdos_inb(0x61) & ~3);
    irqs_restore(s);
}

void irq_remove(void)
{
    x86_emergency_cleanup();
    armdos_setvect(0x23, old23);
    armdos_setvect(0x24, old24);
}

/* the default (BIOS) behaviour for IRQ0/IRQ1, when the x86 program chains
 * to the original vector or has since unhooked it */
void irq_default_08(void)
{
    struct armregs r;
    memset(&r, 0, sizeof r);
    uint32_t s = irqs_off();
    old08(&r);
    irqs_restore(s);
}

void irq_default_09(void)
{
    uint32_t s = irqs_off();
    if (kbd_latched) {
        /* the program's handler chained to us with the byte it was given */
        kbd_latched = 0;
        kbd_unread = 0;
        if (trace_kb) dbg("c%02X ", kbd_latch);
        kbd86_process(kbd_latch);
    } else {
        struct armregs r;
        memset(&r, 0, sizeof r);
        old09(&r);
    }
    irqs_restore(s);
}

void irq_default_hw(int vec)
{
    int i = hw_index(vec);
    if (i < 0) return;
    struct armregs r;
    memset(&r, 0, sizeof r);
    uint32_t s = irqs_off();
    if (oldhw[i]) armdos_callold(oldhw[i], &r); else armdos_outb(0x20, 0x20);
    irqs_restore(s);
}

/* Before a call that may wait (keyboard input through DOS, EXEC): IRQs the
 * x86 program has not taken yet go to the BIOS, so nothing stays in service. */
void irq_flush_to_bios(void)
{
    uint32_t s = irqs_off();
    uint32_t p = irq_pending & (PEND_IRQ0 | PEND_IRQ1 | PEND_HWIRQ);
    irq_pending &= ~(PEND_IRQ0 | PEND_IRQ1 | PEND_HWIRQ);
    irqs_restore(s);
    if (p & PEND_IRQ0) irq_default_08();
    for (int i = 0; i < 4; i++) if (p & (0x10u << i)) irq_default_hw(hw_irq[i] + 8);
    if (p & PEND_IRQ1) {
        uint32_t s2 = irqs_off();
        uint8_t b;
        while (kbq_pop(&b)) kbd86_process(b);
        irqs_restore(s2);
    }
}

/* the run loop found irq_pending set and IF=1: raise one x86 interrupt */
void irq_deliver(void)
{
    uint32_t s = irqs_off();
    uint32_t p = irq_pending, bit = 0;
    int vec = -1;
    if (p & PEND_IRQ0) { bit = PEND_IRQ0; vec = 0x08; }
    else if (p & PEND_IRQ1) {
        uint8_t kb;
        if (kbq_pop(&kb)) { vec = 0x09; kbd_latch = kb; kbd_latched = 1; kbd_unread = 1; if (trace_kb) dbg("x%02X ", kb); }
        if (kbq_head == kbq_tail) bit = PEND_IRQ1;
    }
    else if (p & PEND_HWIRQ) {
        for (int i = 0; i < 4; i++) if (p & (0x10u << i)) { bit = 0x10u << i; vec = hw_irq[i] + 8; break; }
    }
    else if (p & PEND_1B) { bit = PEND_1B; vec = 0x1B; }
    else if (p & PEND_1C) { bit = PEND_1C; vec = 0x1C; }
    else if (p & PEND_MOUSE) { bit = PEND_MOUSE; vec = -2; }
    irq_pending &= ~bit;
    irqs_restore(s);
    if (vec >= 0) cpu_interrupt(vec, cpu.eip);
    else if (vec == -2) dos86_mouse_event();
}
