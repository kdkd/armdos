/*
 * hle.c - the x86 world's firmware: IVT stubs, the F000 "ROM", the native
 * BIOS services (INT 10h-1Ah, 2Fh, 33h ...) mapped onto the ARM BIOS, and
 * port I/O passed through to the ISA I/O window.
 *
 * Every x86 IVT entry starts out pointing at a 4-byte stub F000:E000+n*4 =
 * 0F FF nn CF ("native INT nn", IRET).  An INT whose vector still points at
 * its stub is serviced natively without touching the stack; a program that
 * hooked the vector and chains to the old one runs the stub.
 *
 * Copyright (c) 2026 Europa Micro Systems. Part of ARM-DOS (apps/x86).
 */
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include "dos86.h"
#include "jit.h"

extern volatile int kbd_latched, kbd_unread;
extern volatile uint8_t kbd_latch;
static int nested_depth;
static uint32_t deferred_exit;          /* 0 or 0x10000 | type << 8 | code */

void x86_msg(const char *s) { write(2, s, strlen(s)); }

static void w16(uint32_t lin, uint16_t v) { uint8_t *p = hptr(lin); p[0] = v; p[1] = v >> 8; }
static void w32(uint32_t lin, uint32_t v) { w16(lin, v); w16(lin + 2, v >> 16); }
#define FLAT(seg, off) ((uint32_t)hptr(LIN(seg, off)))

void x86_regs_to_arm(struct armregs *r)
{
    memset(r, 0, sizeof *r);
    r->r0 = rAX; r->r1 = rBX; r->r2 = rCX; r->r3 = rDX;
    r->r4 = rSI; r->r5 = rDI; r->r6 = rBP;
    r->r7 = cpu.sreg[SEG_DS]; r->r8 = cpu.sreg[SEG_ES];
}
static void arm_back(const struct armregs *r)
{
    rAX = r->r0; rBX = r->r1; rCX = r->r2; rDX = r->r3;
}
static void cf_from(const struct armregs *r) { set_cf((r->cpsr & ARM_CPSR_C) != 0); }

/* ------------------------------------------------------------ the world */

static void copy_font(uint16_t off, int bh, int bytes)
{
    struct armregs r;
    memset(&r, 0, sizeof r);
    r.r0 = 0x1130; r.r1 = bh << 8;
    _armdos_int10(&r);
    if (r.r6) memcpy(hptr(LIN(HLE_SEG, off)), (const void *)r.r6, bytes);
}

void world_init(void)
{
    /* IVT: every vector -> its stub */
    for (int n = 0; n < 256; n++) {
        w32(n * 4, HLE_VEC(n));
        uint8_t *s = hptr(LIN(HLE_SEG, HLE_OFF(n)));
        s[0] = 0x0F; s[1] = 0xFF; s[2] = n; s[3] = 0xCF;
    }
    static const uint8_t specials[] = {
        0x0F, 0xFE, 0x00, 0x90,           /* E400 sentinel */
        0x0F, 0xFE, 0x01, 0xCB,           /* E404 CALL 5 */
        0x0F, 0xFE, 0x02, 0xCB,           /* E408 case map */
        0x0F, 0xFE, 0x03, 0x90,           /* E40C reset */
        0x0F, 0xFE, 0x04, 0x90,           /* E410 end of a mouse callback */
        0xCB, 0x90, 0x90, 0x90,           /* E414 RETF (device strategy/interrupt) */
    };
    memcpy(hptr(LIN(HLE_SEG, ROM_SENTINEL)), specials, sizeof specials);

    /* data vectors */
    static const uint8_t diskparm[11] = { 0xDF, 0x02, 0x25, 0x02, 0x12, 0x1B, 0xFF, 0x6C, 0xF6, 0x0F, 0x08 };
    memcpy(hptr(LIN(HLE_SEG, ROM_DISKPARM)), diskparm, sizeof diskparm);
    w32(0x1E * 4, (HLE_SEG << 16) | ROM_DISKPARM);
    w32(0x1D * 4, (HLE_SEG << 16) | ROM_VIDPARM);
    copy_font(ROM_FONT16, 6, 4096);
    copy_font(ROM_FONT8, 3, 2048);
    memcpy(hptr(LIN(HLE_SEG, ROM_FONT14)), hptr(LIN(HLE_SEG, ROM_FONT16)), 3584);
    memcpy(hptr(LIN(HLE_SEG, 0xFA6E)), hptr(LIN(HLE_SEG, ROM_FONT8)), 1024);   /* where the PC keeps it */
    w32(0x1F * 4, (HLE_SEG << 16) | (ROM_FONT8 + 1024));
    w32(0x43 * 4, (HLE_SEG << 16) | ROM_FONT8);
    w32(0x41 * 4, 0);
    w32(0x46 * 4, 0);

    /* ROM identification: reset vector, date, model byte (AT) */
    static const uint8_t tail[16] = { 0xEA, 0x0C, 0xE4, 0x00, 0xF0, '0', '6', '/', '1', '7', '/', '8', '8', 0, 0xFC, 0x00 };
    memcpy(hptr(LIN(HLE_SEG, 0xFFF0)), tail, 16);
    static const uint8_t config[10] = { 0x08, 0x00, 0xFC, 0x01, 0x00, 0x70, 0x00, 0x00, 0x00, 0x00 };
    memcpy(hptr(LIN(HLE_SEG, ROM_CONFIG)), config, sizeof config);
    /* CP/M CALL 5 lands at 0:00C0 (A20 off) or FFFF:00D0 (A20 on) */
    static const uint8_t jmp5[5] = { 0xEA, ROM_CALL5 & 0xFF, ROM_CALL5 >> 8, 0x00, 0xF0 };
    memcpy(hptr(0x100C0), jmp5, 5);
    memcpy(hptr(0xC0), jmp5, 5);           /* (INT 30h/31h vectors, as on DOS) */
}

/* ------------------------------------------------------------ nested x86 calls */

/* Run x86 code at cs:ip until it returns to the sentinel (RETF or IRET if
 * as_int).  The caller saves/restores what it needs; returns 0, or -1 if the
 * program ended inside (the exit is carried out afterwards). */
int x86_nested_call(uint16_t cs, uint16_t ip, int as_int)
{
    if (as_int) { uint32_t f = get_flags(); cpu_push16(f); cpu.flags = f & ~(F_IF | F_TF); }
    cpu_push16(HLE_SEG);
    cpu_push16(ROM_SENTINEL);
    cpu_far_jump(cs, ip);
    int saved_active = x86_active, saved_led = alt_led;
    x86_active = 1;
    alt_led_set(1);
    nested_depth++;
    for (;;) {
        cpu_run();
        if (cpu.stop) break;
    }
    nested_depth--;
    x86_active = saved_active;
    alt_led_set(saved_led);
    cpu.stop = 0;
    return deferred_exit ? -1 : 0;
}

void cpu_stop_run(int why)
{
    cpu.stop = why;
    pend_set(PEND_STOP);
}

/* x86 code ended while nested: finish it once back at the top level */
int x86_defer_exit(int code, int type)
{
    if (!nested_depth) return 0;
    deferred_exit = 0x10000 | (type << 8) | (code & 0xFF);
    cpu_stop_run(STOP_NESTED);
    return 1;
}
void x86_run_deferred(void)
{
    if (deferred_exit && !nested_depth) {
        uint32_t d = deferred_exit;
        deferred_exit = 0;
        x86_terminate(d & 0xFF, (d >> 8) & 0xFF);
    }
}

/* ------------------------------------------------------------ BIOS services */

static void idle_wait(void)
{
    if (!irq_pending) __asm__ volatile("mcr p15, 0, %0, c7, c0, 4" :: "r"(0) : "memory");
}

/* ---- program fonts in the CGA graphics modes -------------------------
 * A PC BIOS draws characters in modes 4-6 from the table at INT 43h
 * (00h-7Fh) and INT 1Fh (80h-FFh), so programs install their own glyphs
 * there (Apogee's CGA games print all their text that way).  The ARM BIOS
 * has only its own font: while a program's table is installed, ELBOW draws
 * those characters itself. */
static const uint8_t *prog_glyph(int ch)
{
    uint32_t vec = (ch & 0x80) ? 0x1F : 0x43;
    uint8_t *e = hptr(vec * 4);
    uint16_t off = e[0] | e[1] << 8, seg = e[2] | e[3] << 8;
    uint16_t def = (ch & 0x80) ? ROM_FONT8 + 1024 : ROM_FONT8;
    if (seg == HLE_SEG && off == def) return 0;
    if (seg == 0 && off == 0) return 0;
    return hptr(LIN(seg, (uint16_t)(off + (ch & 0x7F) * 8)));
}

static void cga_glyph(int mode, int row, int col, const uint8_t *g, int attr)
{
    for (int y = 0; y < 8; y++) {
        int line = row * 8 + y;
        uint8_t *p = hptr(LIN(0xB800, (line & 1) * 0x2000 + (line >> 1) * 80));
        uint8_t bits = g[y];
        if (mode == 6) {
            uint8_t v = (attr & 1) ? bits : 0;
            if (attr & 0x80) p[col] ^= v; else p[col] = v;
        } else {
            uint16_t w = 0;
            for (int x = 0; x < 8; x++) if (bits & (0x80 >> x)) w |= (attr & 3) << (14 - 2 * x);
            if (attr & 0x80) { p[col * 2] ^= w >> 8; p[col * 2 + 1] ^= w & 0xFF; }
            else { p[col * 2] = w >> 8; p[col * 2 + 1] = w & 0xFF; }
        }
    }
}

/* AH=09h/0Ah/0Eh in modes 4-6 with a program font: 1 if handled */
static int gfx_prog_char(void)
{
    uint8_t *bda = hptr(0x400);
    int mode = bda[0x49] & 0x7F;
    if (mode < 4 || mode > 6) return 0;
    int ch = rAL;
    if (rAH == 0x0E && (ch == 7 || ch == 8 || ch == 10 || ch == 13)) return 0;
    const uint8_t *g = prog_glyph(ch);
    if (!g) return 0;
    int cols = mode == 6 ? 80 : 40;
    int col = bda[0x50], row = bda[0x51];
    if (rAH != 0x0E) {
        for (int i = 0; i < rCX && col + i < cols; i++) cga_glyph(mode, row, col + i, g, rBL);
        return 1;
    }
    cga_glyph(mode, row, col, g, rBL);
    struct armregs r;
    memset(&r, 0, sizeof r);
    if (++col >= cols) {
        col = 0;
        if (row >= 24) {                       /* scroll: the BIOS's line feed */
            r.r0 = 0x0E0A; r.r1 = rBX; _armdos_int10(&r);
            return 1;
        }
        row++;
    }
    r.r0 = 0x0200; r.r3 = (row << 8) | col; _armdos_int10(&r);
    return 1;
}

/* AH=08h in modes 4-6: which character is at the cursor?  A PC BIOS reads
   the 8x8 cell back and looks the pattern up in its font (INT 43h, then
   INT 1Fh for 80h-FFh); the ARM BIOS answers 0.  GW-BASIC's screen editor
   reads every line typed in graphics mode this way. */
static int gfx_read_char(void)
{
    uint8_t *bda = hptr(0x400);
    int mode = bda[0x49] & 0x7F;
    if (mode < 4 || mode > 6) return 0;
    int col = bda[0x50], row = bda[0x51];
    uint8_t cell[8];
    for (int y = 0; y < 8; y++) {
        int line = row * 8 + y;
        uint8_t *p = hptr(LIN(0xB800, (line & 1) * 0x2000 + (line >> 1) * 80));
        if (mode == 6) cell[y] = p[col];
        else {
            uint16_t w = (uint16_t)(p[col * 2] << 8 | p[col * 2 + 1]);
            uint8_t b = 0;
            for (int x = 0; x < 8; x++) if ((w >> (14 - 2 * x)) & 3) b |= 0x80 >> x;
            cell[y] = b;
        }
    }
    for (int ch = 0; ch < 256; ch++) {
        uint8_t *e = hptr((ch & 0x80 ? 0x1F : 0x43) * 4);
        uint16_t off = e[0] | e[1] << 8, seg = e[2] | e[3] << 8;
        if (!seg && !off) continue;
        const uint8_t *g = hptr(LIN(seg, (uint16_t)(off + (ch & 0x7F) * 8)));
        if (!memcmp(g, cell, 8)) { rAX = ch; return 1; }
    }
    rAX = 0;
    return 1;
}

/* set once the program asks INT 10h AX=1A00h (display combination), i.e.
   knows it is on a VGA */
static int vga_aware;

static int int10(void)
{
    struct armregs r;
    x86_regs_to_arm(&r);
    uint32_t ax = rAX;
    if (ax == 0x1A00) vga_aware = 1;
    switch (rAH) {
    case 0x09: case 0x0A: case 0x0E:
        if (gfx_prog_char()) return HLE_DONE;
        break;
    case 0x08:
        if (gfx_read_char()) return HLE_DONE;
        break;
    case 0x10:
        /* palette / overscan / blink registers.  ELBOW reports no EGA (AH=12h
           below), so a program that has not identified a VGA believes it is
           on a CGA, whose BIOS ignores AH=10h -- GW-BASIC calls AX=1002h
           there with a buffer that is not a palette.  Ignore it likewise
           (the DAC calls, AL=10h and up, still go through). */
        if (rAL <= 0x03 && !vga_aware) return HLE_DONE;
        if (rAL == 0x02 || rAL == 0x09 || rAL == 0x12 || rAL == 0x17) r.r3 = FLAT(cpu.sreg[SEG_ES], rDX);
        if (rAL == 0x09) {                 /* read all palette registers: 16 + overscan */
            uint8_t *p = (uint8_t *)r.r3;
            for (int i = 0; i < 16; i++) { armdos_inb(0x3DA); armdos_outb(0x3C0, i); p[i] = armdos_inb(0x3C1); }
            armdos_inb(0x3DA); armdos_outb(0x3C0, 0x11); p[16] = armdos_inb(0x3C1);
            armdos_inb(0x3DA); armdos_outb(0x3C0, 0x20);
            return HLE_DONE;
        }
        if (rAL == 0x07) {                 /* read one palette register */
            armdos_inb(0x3DA); armdos_outb(0x3C0, rBL & 0x1F); rBH = armdos_inb(0x3C1);
            armdos_inb(0x3DA); armdos_outb(0x3C0, 0x20);
            return HLE_DONE;
        }
        break;
    case 0x11:
        if (rAL == 0x00 || rAL == 0x10) r.r6 = FLAT(cpu.sreg[SEG_ES], rBP);
        if (rAL == 0x30) {
            _armdos_int10(&r);
            rCX = r.r2; rDL = r.r3;
            uint16_t off;
            switch (rBH) {
            case 0: off = ROM_FONT8 + 1024; break;           /* INT 1Fh */
            case 1: off = ROM_FONT8; break;                  /* INT 43h */
            case 2: case 5: off = ROM_FONT14; break;
            case 3: off = ROM_FONT8; break;
            case 4: off = ROM_FONT8 + 1024; break;
            default: off = ROM_FONT16; break;
            }
            set_sreg(SEG_ES, HLE_SEG); rBP = off;
            return HLE_DONE;
        }
        break;
    case 0x13: r.r6 = FLAT(cpu.sreg[SEG_ES], rBP); break;
    case 0x12:
        /* EGA information: the ARM PC's VGA has the planar modes (0Dh, 0Eh,
           10h, 12h) and Mode X; a program that asks is
           EGA-aware, so its palette calls go through as well */
        if (rBL == 0x10) vga_aware = 1;
        break;
    case 0x1B: {
        if (rBX != 0) return HLE_DONE;
        uint8_t tmp[64];
        r.r5 = (uint32_t)tmp;
        _armdos_int10(&r);
        tmp[0] = 0; tmp[1] = 0xFF; tmp[2] = 0; tmp[3] = 0;       /* static table pointer: none (not used by callers) */
        memcpy(hptr(LIN(cpu.sreg[SEG_ES], rDI)), tmp, 64);
        rAL = r.r0;
        return HLE_DONE;
    }
    case 0x0F:
        _armdos_int10(&r);
        rAX = r.r0; rBH = r.r1 >> 8;
        return HLE_DONE;
    }
    _armdos_int10(&r);
    switch (ax >> 8) {
    case 0x03: rCX = r.r2; rDX = r.r3; break;
    case 0x08: rAX = r.r0; break;
    case 0x0D: rAL = r.r0; break;
    case 0x10: if (rAL == 0x15) { rCX = r.r2; rDH = r.r3 >> 8; } break;
    case 0x12: rBX = r.r1; rCX = r.r2; rAL = r.r0; break;
    case 0x1A: rAX = r.r0; rBX = r.r1; break;
    case 0x1C: rAL = r.r0; break;
    default: break;
    }
    return HLE_DONE;
}


static int int13(void)
{
    struct armregs r;
    x86_regs_to_arm(&r);
    uint8_t pk[16];
    uint32_t ah = rAH, buflin = 0, buflen = 0;
    switch (ah) {
    case 0x02: case 0x03: case 0x04: case 0x05:
        buflin = LIN(cpu.sreg[SEG_ES], rBX); buflen = rAL * 512u;
        r.r1 = (uint32_t)hptr(buflin);
        break;
    case 0x42: case 0x43: {
        const uint8_t *p = hptr(LIN(cpu.sreg[SEG_DS], rSI));
        memcpy(pk, p, 16);
        uint32_t fp = pk[4] | (pk[5] << 8) | (pk[6] << 16) | ((uint32_t)pk[7] << 24);
        buflin = LIN(fp >> 16, fp & 0xFFFF); buflen = (pk[2] | (pk[3] << 8)) * 512u;
        uint32_t fl = (uint32_t)hptr(buflin);
        pk[4] = fl; pk[5] = fl >> 8; pk[6] = fl >> 16; pk[7] = fl >> 24;
        r.r4 = (uint32_t)pk;
        break;
    }
    case 0x48: r.r4 = FLAT(cpu.sreg[SEG_DS], rSI); break;
    }
    _armdos_int13(&r);
    rAX = r.r0;
    if (ah == 0x08) {
        rBX = r.r1; rCX = r.r2; rDX = r.r3;
        if (rDL < 0x80 || (rDX & 0xFF) < 0x80) { set_sreg(SEG_ES, HLE_SEG); rDI = ROM_DISKPARM; }
    } else if (ah == 0x15) { rCX = r.r2; rDX = r.r3; }
    else if (ah == 0x41) { rBX = r.r1; rCX = r.r2; }
    if ((ah == 0x02 || ah == 0x42) && buflen) x86_note_hle_write(buflin, buflen);
    cf_from(&r);
    return HLE_DONE;
}

static int int15(void)
{
    struct armregs r;
    switch (rAH) {
    case 0x24: rAH = 0; set_cf(0); if (rAL == 2) rAL = 1; if (rAL == 3) rBX = 3; return HLE_DONE;   /* A20: always on */
    case 0x4F: set_cf(1); return HLE_DONE;                      /* keyboard intercept: keep the key */
    case 0x86:
        x86_regs_to_arm(&r);
        x86_active = 0; irq_flush_to_bios();
        _armdos_intr(0x15, &r);
        x86_active = 1;
        rAX = r.r0; cf_from(&r);
        return HLE_DONE;
    case 0x88: rAX = 0; set_cf(0); return HLE_DONE;             /* no extended memory for x86 programs */
    case 0x90: case 0x91: rAH = 0; set_cf(0); return HLE_DONE;
    case 0xC0:
        set_sreg(SEG_ES, HLE_SEG); rBX = ROM_CONFIG; rAH = 0; set_cf(0);
        return HLE_DONE;
    default:
        rAH = 0x86; set_cf(1);
        return HLE_DONE;
    }
}

static int int16(void)
{
    struct armregs r;
    uint32_t ah = rAH;
    x86_regs_to_arm(&r);
    switch (ah) {
    case 0x00: case 0x10: case 0x20: {
        /* wait for a key without blocking inside the BIOS, so the program's
           timer and keyboard hooks keep running */
        struct armregs q;
        memset(&q, 0, sizeof q);
        q.r0 = ah == 0x00 ? 0x0100 : 0x1100;
        _armdos_int16(&q);
        if (q.cpsr & ARM_CPSR_Z) { idle_wait(); return HLE_RETRY; }
        r.r0 = ah == 0x20 ? 0x1000 : ah << 8;
        _armdos_int16(&r);
        rAX = r.r0;
        if (opt_trace) dbg("  key %04X\n", rAX);
        { extern int trace_kb; if (trace_kb) dbg("K%04X ", rAX); }
        return HLE_DONE;
    }
    case 0x01: case 0x11: case 0x21:
        r.r0 = ah == 0x01 ? 0x0100 : 0x1100;
        _armdos_int16(&r);
        set_zf((r.cpsr & ARM_CPSR_Z) != 0);
        if (!(r.cpsr & ARM_CPSR_Z)) { rAX = r.r0; if (opt_trace) dbg("  peek %04X\n", rAX); }
        return HLE_DONE;
    case 0x02: case 0x12: case 0x22:
        r.r0 = ah == 0x02 ? 0x0200 : 0x1200;
        _armdos_int16(&r);
        if (ah == 0x02) rAL = r.r0; else rAX = r.r0;
        return HLE_DONE;
    case 0x05:
        _armdos_int16(&r);
        rAL = r.r0;
        return HLE_DONE;
    case 0x03: return HLE_DONE;
    default:
        _armdos_int16(&r);
        rAX = r.r0;
        return HLE_DONE;
    }
}

static int int2f(void)
{
    struct armregs r;
    switch (rAX) {
    case 0x1600: rAL = 0; return HLE_DONE;                      /* no Windows */
    case 0x1680: rAL = 0x80; return HLE_DONE;                   /* idle call: not supported */
    case 0x4300: rAL = 0; return HLE_DONE;                      /* no XMS for x86 programs */
    case 0x4A01: rBX = 0; set_sreg(SEG_ES, 0xFFFF); rDI = 0xFFFF; return HLE_DONE;
    case 0x1A00:                                                /* ANSI.SYS: ask the ARM one */
        x86_regs_to_arm(&r);
        _armdos_int2f(&r);
        rAL = r.r0;
        return HLE_DONE;
    }
    return HLE_DONE;                                            /* "not installed" (AL unchanged) */
}

/* ---- INT 33h: the ARM mouse driver, with x86 event handlers */
static uint32_t x86_mouse_handler;     /* seg << 16 | off */
static uint16_t x86_mouse_mask;
static volatile uint32_t mev_ax, mev_bx, mev_cx, mev_dx;

void x86_mouse_cb(uint32_t ax, uint32_t bx, uint32_t cx, uint32_t dx)
{
    mev_ax |= ax; mev_bx = bx; mev_cx = cx; mev_dx = dx;
    pend_set(PEND_MOUSE);
}

void dos86_mouse_event(void)
{
    uint32_t s;
    __asm__ volatile("mrs %0, cpsr\n\torr r12, %0, #0x80\n\tmsr cpsr_c, r12" : "=r"(s) :: "r12", "memory");
    uint32_t ax = mev_ax, bx = mev_bx, cx = mev_cx, dx = mev_dx;
    mev_ax = 0;
    __asm__ volatile("msr cpsr_c, %0" :: "r"(s) : "memory");
    if (!x86_mouse_handler || !(ax & x86_mouse_mask)) return;
    X86 save;
    get_flags();
    memcpy(&save, &cpu, sizeof cpu);
    rAX = ax & x86_mouse_mask; rBX = bx; rCX = cx; rDX = dx; rSI = 0; rDI = 0;
    uint64_t ic = cpu.icount;
    int r = x86_nested_call(x86_mouse_handler >> 16, x86_mouse_handler & 0xFFFF, 0);
    ic = cpu.icount;
    memcpy(&cpu, &save, sizeof cpu);
    cpu.icount = ic;
    if (r < 0) x86_run_deferred();
}

static int int33(void)
{
    struct armregs r;
    x86_regs_to_arm(&r);
    uint32_t ax = rAX;
    switch (ax) {
    case 0x0009: case 0x0016: case 0x0017: r.r3 = FLAT(cpu.sreg[SEG_ES], rDX); break;
    case 0x000C: case 0x0014: {
        uint32_t nh = ((uint32_t)cpu.sreg[SEG_ES] << 16) | rDX, oh = x86_mouse_handler;
        uint16_t om = x86_mouse_mask;
        x86_mouse_handler = nh; x86_mouse_mask = rCX;
        r.r3 = nh ? (uint32_t)x86_mouse_cb : 0;
        if (!nh) r.r2 = 0;
        _armdos_int33(&r);
        if (ax == 0x14) { rCX = om; set_sreg(SEG_ES, oh >> 16); rDX = oh & 0xFFFF; }
        return HLE_DONE;
    }
    case 0x0018: rAX = 0xFFFF; return HLE_DONE;
    }
    _armdos_int33(&r);
    rAX = r.r0; rBX = r.r1; rCX = r.r2; rDX = r.r3;
    if (ax == 0x0000 || ax == 0x0021) { if (!r.r0) { x86_mouse_handler = 0; } }
    return HLE_DONE;
}

/* INT 25h / 26h: absolute disk read/write, leaves the flags on the stack */
static int int2526(int n)
{
    struct armregs r;
    x86_regs_to_arm(&r);
    uint8_t pk[10];
    uint32_t buflin;
    if (rCX == 0xFFFF) {
        const uint8_t *p = hptr(LIN(cpu.sreg[SEG_DS], rBX));
        memcpy(pk, p, 10);
        uint32_t fp = pk[6] | (pk[7] << 8) | (pk[8] << 16) | ((uint32_t)pk[9] << 24);
        buflin = LIN(fp >> 16, fp & 0xFFFF);
        uint32_t fl = (uint32_t)hptr(buflin);
        pk[6] = fl; pk[7] = fl >> 8; pk[8] = fl >> 16; pk[9] = fl >> 24;
        r.r1 = (uint32_t)pk;
    } else {
        buflin = LIN(cpu.sreg[SEG_DS], rBX);
        r.r1 = (uint32_t)hptr(buflin);
    }
    x86_active = 0; irq_flush_to_bios();
    _armdos_intr(n, &r);
    x86_active = 1;
    rAX = r.r0;
    cf_from(&r);
    if (n == 0x25) x86_note_hle_write(buflin, 65536);
    uint32_t f = get_flags();
    if (hle_in_direct()) { cpu_push16(f); return HLE_DONE; }
    /* came through the stub: return like DOS (RETF), the INT's FLAGS stay */
    uint32_t ip = cpu_pop16(), cs = cpu_pop16();
    cpu_far_jump(cs, ip);
    return HLE_SWITCH;
}

/* ------------------------------------------------------------ dispatch */

/* INT 20h and AH=00h end the process whose PSP is the caller's CS (DOS 1
 * rules; a debugger's child runs with the debugger as the current PSP) */
void x86_terminate_cs(int code)
{
    uint16_t cs = cpu.sreg[SEG_CS];
    if (!hle_in_direct()) cs = rd16(cpu.sbase[SEG_SS] + ((rSP + 2) & 0xFFFF));
    const uint8_t *p = hptr(LIN(cs, 0));
    if (p[0] == 0xCD && p[1] == 0x20 && cs != cur_psp) cur_psp = cs;
    x86_terminate(code, 0);
}

void cpu_unsupported(const char *what)
{
    char m[120];
    snprintf(m, sizeof m, "\r\nELBOW: %s at %04X:%04lX is not supported\r\n",
             what, cpu.sreg[SEG_CS], (unsigned long)cpu.prev_eip);
    x86_msg(m);
    x86_terminate(0xFF, 0);
}

static void bad_opcode(void)
{
    const uint8_t *p = hptr(LIN(cpu.sreg[SEG_CS], cpu.eip));
    char what[64];
    snprintf(what, sizeof what, "unsupported instruction %02X %02X %02X", p[0], p[1], p[2]);
    cpu.prev_eip = cpu.eip;
    cpu_unsupported(what);
}

void dbg(const char *fmt, ...)
{
    char b[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    for (char *p = b; *p; p++) armdos_outb(0xE9, *p);
}

long trace_at;
int seq_at;
static int hle_int_arm(int n);
/* every DOS/BIOS service runs in ARM code: the alt-CPU LED goes out for it */
int hle_int(int n)
{
    alt_led_set(0);
    int r = hle_int_arm(n);
    if (!x86_exited) alt_led_set(1);
    return r;
}

static int hle_int_arm(int n)
{
    struct armregs r;
    if (trace_at && n != 0x08 && n != 0x09 && n != 0x1C && n != 0x28 && !(n == 0x16 && (rAH == 0 || rAH == 1 || rAH == 0x10 || rAH == 0x11)) && --trace_at == 0) {
        extern int jit_seqlog;
        if (seq_at) jit_seqlog = 1; else opt_trace = 2;     /* /SEQAT: the translator's block log instead */
    }
    if (opt_trace && n != 0x08 && n != 0x09 && n != 0x1C && n != 0x28 && !(n == 0x16 && (rAH == 0 || rAH == 1 || rAH == 0x10 || rAH == 0x11)))
        dbg("INT %02X AX=%04X BX=%04X CX=%04X DX=%04X SI=%04X DI=%04X DS=%04X ES=%04X at %04X:%04lX\n", n, rAX, rBX, rCX, rDX, rSI, rDI,
            cpu.sreg[SEG_DS], cpu.sreg[SEG_ES], cpu.sreg[SEG_CS], (unsigned long)cpu.prev_eip);
    if (opt_trace && n == 0x21 && (rAH == 0x3D || rAH == 0x3C || rAH == 0x4B || rAH == 0x41 || rAH == 0x4E)) {
        char nm[68]; int k = 0;
        for (; k < 67; k++) { uint8_t c = rd8(LIN(cpu.sreg[SEG_DS], rDX + k)); if (!c) break; nm[k] = c; }
        nm[k] = 0; dbg("  name \"%s\"\n", nm);
    }
    switch (n) {
    case 0x00:
        x86_msg("\r\nDivide overflow\r\n");
        x86_terminate(0, 1);
        return HLE_SWITCH;
    case 0x06:
        /* the stacked IP (direct: cpu.eip is the faulting instruction) */
        if (!hle_in_direct()) { uint32_t ip = rd16(cpu.sbase[SEG_SS] + rSP), cs = rd16(cpu.sbase[SEG_SS] + ((rSP + 2) & 0xFFFF)); set_sreg(SEG_CS, cs); cpu.eip = ip; }
        bad_opcode();
        return HLE_SWITCH;
    case 0x08: irq_default_08(); return HLE_DONE;
    case 0x09: irq_default_09(); return HLE_DONE;
    case 0x0B: case 0x0C: case 0x0D: case 0x0F: irq_default_hw(n); return HLE_DONE;
    case 0x10: return int10();
    case 0x67: { extern int ems_int67(void); return ems_int67(); }
    case 0x11: {
        uint32_t eq = *(volatile uint16_t *)0x410;
        rAX = eq & ~2u;                  /* no coprocessor */
        return HLE_DONE;
    }
    case 0x12: rAX = 640; return HLE_DONE;
    case 0x13: return int13();
    case 0x14: case 0x17: {
        uint32_t fn = rAH;
        x86_regs_to_arm(&r);
        _armdos_intr(n, &r);
        rAX = r.r0;
        /* INT 17h AH=00h: a PC BIOS reads the status right after the strobe,
           while the printer still acknowledges, and reports ACK (D0h); some
           programs (GW-BASIC's printer code) insist on it */
        if (n == 0x17 && fn == 0 && (rAH & 0xB9) == 0x90) rAH |= 0x40;
        return HLE_DONE;
    }
    case 0x15: return int15();
    case 0x16: return int16();
    case 0x18:
        x86_msg("\r\nNo ROM BASIC\r\n");
        x86_terminate(0xFF, 0);
        return HLE_SWITCH;
    case 0x19:
        x86_terminate(0, 0);
        return HLE_SWITCH;
    case 0x1A:
        x86_regs_to_arm(&r);
        _armdos_int1a(&r);
        arm_back(&r);
        cf_from(&r);
        return HLE_DONE;
    case 0x1B:                            /* Ctrl-Break: tell DOS (the ARM handler) */
        x86_regs_to_arm(&r);
        _armdos_intr(0x1B, &r);
        return HLE_DONE;
    case 0x20:
        x86_terminate_cs(0);
        return HLE_SWITCH;
    case 0x21: return dos_int21();
    case 0x23:
        x86_terminate(0, 1);
        return HLE_SWITCH;
    case 0x24: rAL = 3; return HLE_DONE;           /* fail */
    case 0x25: case 0x26: return int2526(n);
    case 0x27: {
        /* TSR: DX = first byte past the resident part, from the PSP */
        uint32_t paras = ((uint32_t)rDX + 15) >> 4;
        extern void x86_keep(uint32_t paras, int code);
        x86_keep(paras, 0);
        return HLE_SWITCH;
    }
    case 0x29:
        x86_regs_to_arm(&r);
        _armdos_intr(0x29, &r);
        return HLE_DONE;
    case 0x2F: return int2f();
    case 0x33: return int33();
    default:
        return HLE_DONE;
    }
}

/* 0F FE nn: internal entry points in the F000 ROM */
void hle_special(int code)
{
    switch (code) {
    case 0:                                  /* end of a nested call */
        cpu_stop_run(STOP_NESTED);
        break;
    case 1: {                                /* CALL 5: function in CL */
        uint32_t ah = rCL;
        uint32_t save = rAX;
        rAH = ah;
        dos_int21();
        if (ah < 0x0D) { /* (character functions return AL) */ } else (void)save;
        break;
    }
    case 2: {                                /* country case map: AL >= 80h */
        struct armregs r;
        memset(&r, 0, sizeof r);
        r.r0 = 0x6520; r.r3 = rAL;
        if (rAL >= 0x80) { arm21(&r); rAL = r.r3; }
        break;
    }
    case 3:                                  /* jumped to the reset vector */
        x86_terminate(0, 0);
        break;
    default:
        bad_opcode();
        break;
    }
}

/* ------------------------------------------------------------ ports */

static uint32_t in8(uint32_t port)
{
    if (port == 0x20 || port == 0xA0) return 0;             /* reading here would acknowledge an IRQ */
    if (port >= 0x300 && port <= 0x307) return 0xFF;        /* the ARM-PC's floppy controller */
    if (port == 0x60 && kbd_latched) {
        if ((armdos_inb(0x64) & 0x21) == 0x01) kbd_latched = 0;    /* IRQ1 masked: the program polls the real port */
        else { kbd_unread = 0; return kbd_latch; }
    }
    if (port == 0x64 && kbd_latched) return (armdos_inb(0x64) & ~0x21) | (kbd_unread ? 1 : 0);
    if ((port >= 0x81 && port <= 0x83) || port == 0x87 || (port >= 0x89 && port <= 0x8B) || port == 0x8F)
        return (armdos_inb(port) - ((uint32_t)(uintptr_t)mem >> 16)) & 0xFF;
    return armdos_inb(port);
}

static void out8(uint32_t port, uint32_t v)
{
    if (port >= 0x300 && port <= 0x307) return;
    if (port >= 0xF0 && port <= 0xFF) return;               /* ARM-PC system board (exit port) */
    if (port >= 0x40 && port <= 0x43) pit_touched = 1;
    else if (port == 0x61) speaker_touched = 1;
    else if ((port >= 0x81 && port <= 0x83) || port == 0x87 || (port >= 0x89 && port <= 0x8B) || port == 0x8F)
        v += (uint32_t)(uintptr_t)mem >> 16;                /* DMA page: x86 memory is at mem (main.c) */
    armdos_outb(port, v);
}

uint32_t io_in(uint32_t port, int size)
{
    port &= 0xFFFF;
    if (size == 0) return in8(port);
    if (port == 0x1F0) {
        uint32_t v = armdos_inw(0x1F0);
        if (size == 2) v |= (uint32_t)armdos_inw(0x1F0) << 16;
        return v;
    }
    uint32_t v = in8(port) | (in8(port + 1) << 8);
    if (size == 2) v |= (in8(port + 2) << 16) | (in8(port + 3) << 24);
    return v;
}

void io_out(uint32_t port, uint32_t v, int size)
{
    port &= 0xFFFF;
    if (size == 0) { out8(port, v & 0xFF); return; }
    if (port == 0x1F0) {
        armdos_outw(0x1F0, v);
        if (size == 2) armdos_outw(0x1F0, v >> 16);
        return;
    }
    out8(port, v & 0xFF); out8(port + 1, (v >> 8) & 0xFF);
    if (size == 2) { out8(port + 2, (v >> 16) & 0xFF); out8(port + 3, v >> 24); }
}

/* ------------------------------------------------------------ DOS data */

void dosdata_init(void)
{
    uint32_t b = LIN(DOSDATA_SEG, 0);
    memset(hptr(b), 0, 0x600);
    uint32_t lol = b + DD_LOL;
    w16(lol - 2, ARENA_FIRST);
    w32(lol + 0x00, ((uint32_t)DOSDATA_SEG << 16) | DD_DPB);
    w32(lol + 0x04, ((uint32_t)DOSDATA_SEG << 16) | DD_SFT);
    w32(lol + 0x08, ((uint32_t)HLE_SEG << 16) | 0xE520);
    w32(lol + 0x0C, ((uint32_t)HLE_SEG << 16) | 0xE500);
    w16(lol + 0x10, 512);
    w32(lol + 0x12, 0xFFFFFFFFu);
    *hptr(lol + 0x20) = 3;
    *hptr(lol + 0x21) = 5;
    /* NUL device header (x86 layout) at LoL+22h, then CON and CLOCK$ in the ROM */
    uint32_t nul = lol + 0x22;
    w32(nul, ((uint32_t)HLE_SEG << 16) | 0xE500);
    w16(nul + 4, 0x8004); w16(nul + 6, 0x0FF); w16(nul + 8, 0x0FF);
    memcpy(hptr(nul + 10), "NUL     ", 8);
    *hptr(b + 0x0FF) = 0xCB;
    uint32_t con = LIN(HLE_SEG, 0xE500), clk = LIN(HLE_SEG, 0xE520);
    w32(con, ((uint32_t)HLE_SEG << 16) | 0xE520);
    w16(con + 4, 0x8013); w16(con + 6, 0xE414); w16(con + 8, 0xE414);
    memcpy(hptr(con + 10), "CON     ", 8);
    w32(clk, 0xFFFFFFFFu);
    w16(clk + 4, 0x8008); w16(clk + 6, 0xE414); w16(clk + 8, 0xE414);
    memcpy(hptr(clk + 10), "CLOCK$  ", 8);
    /* an empty SFT block */
    w32(b + DD_SFT, 0xFFFFFFFFu);
    w16(b + DD_SFT + 4, 0);
}
