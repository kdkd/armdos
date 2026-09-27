/* ARM/AT BIOS — power-on self test, the vector table, and the bootstrap */
#include "bios.h"

#define A_NORM   0x07
#define A_BRIGHT 0x0F
#define A_LOGO1  0x0A
#define A_LOGO2  0x02

static const char *const logo[3] = {
    "\xDC\xDF\xDF\xDC \xDB\xDF\xDF\xDC \xDB\xDC \xDC\xDB",
    "\xDB\xDF\xDF\xDB \xDB\xDF\xDF\xDC \xDB \xDF \xDB",
    "\xDF  \xDF \xDF  \xDF \xDF   \xDF",
};

static int cpu_mhz(void)
{
    int m = inb(0xF1);
    return (m == 0 || m == 0xFF) ? 100 : m;
}

static int del_pressed;
static int esc_pressed;

static void poll_keys(void)
{
    int k;
    while ((k = kbd_peek()) >= 0) {
        struct armregs r = { 0 };
        r.r0 = 0x1000;
        int16_handler(&r);
        int sc = (r.r0 >> 8) & 0xFF;
        if (sc == 0x53) del_pressed = 1;
        if (sc == 0x01) esc_pressed = 1;
        (void)k;
    }
}

/* --------------------------------------------------------- vector table */

static void install_vectors(void)
{
    for (int i = 0; i < 256; i++) IVT[i] = 0;
    for (int i = 0x08; i <= 0x0F; i++) IVT[i] = (uint32_t)irq_default_handler;
    for (int i = 0x70; i <= 0x77; i++) IVT[i] = (uint32_t)irq_default_handler;
    IVT[0x00] = (uint32_t)fault_handler;       /* divide error */
    IVT[0x05] = (uint32_t)iret_handler;        /* print screen */
    IVT[0x06] = (uint32_t)fault_handler;       /* undefined instruction */
    IVT[0x08] = (uint32_t)int08_handler;
    IVT[0x09] = (uint32_t)int09_handler;
    IVT[0x0D] = (uint32_t)fault_handler;       /* data abort */
    IVT[0x0E] = (uint32_t)fault_handler;       /* prefetch abort */
    IVT[0x10] = (uint32_t)int10_handler;
    IVT[0x11] = (uint32_t)int11_handler;
    IVT[0x12] = (uint32_t)int12_handler;
    IVT[0x13] = (uint32_t)int13_handler;
    IVT[0x14] = (uint32_t)int14_handler;
    IVT[0x15] = (uint32_t)int15_handler;
    IVT[0x16] = (uint32_t)int16_handler;
    IVT[0x17] = (uint32_t)int17_handler;
    IVT[0x18] = (uint32_t)int18_handler;
    IVT[0x19] = (uint32_t)int19_handler;
    IVT[0x1A] = (uint32_t)int1a_handler;
    IVT[0x1B] = (uint32_t)iret_handler;
    IVT[0x1C] = (uint32_t)iret_handler;
    IVT[0x1F] = (uint32_t)(font8x8 + 128 * 8);
    IVT[0x43] = (uint32_t)font8x8;
    IVT[0x4A] = (uint32_t)iret_handler;
    IVT[0x74] = (uint32_t)int74_handler;
}

void irq_default_handler(struct armregs *f)
{
    if (f->intno >= 0x70) outb(0xA0, 0x20);
    outb(0x20, 0x20);
}

/* ---------------------------------------------------- simple services */

void int11_handler(struct armregs *f)
{
    set_ax(f, BDA16(BDA_EQUIP));
}

void int12_handler(struct armregs *f)
{
    set_ax(f, BDA16(BDA_MEMKB));
}

/* INT 14h - serial ports (COM1 = 3F8h console, COM2 = 2F8h with the internal modem).
 * As the PC/AT BIOS: AH=00 init (AL = baud<<5 | parity<<3 | stop<<2 | length), 01 send
 * (raises DTR+RTS, waits for DSR, CTS and THRE), 02 receive (raises DTR, waits for DSR and
 * data), 03 status (AH = LSR, AL = MSR). Waits time out after BDA 7Ch+port seconds
 * (default 1) with AH bit 7 set. See docs/MODEM.md and bios/README.md. */
static int com_wait(uint32_t reg, uint8_t mask, int port)
{
    uint32_t start = ticks();
    uint32_t limit = (uint32_t)(BDA8(0x7C + port) ? BDA8(0x7C + port) : 1) * 18 + 1;
    irq_enable();                       /* like the IBM BIOS's STI: the tick must advance */
    for (;;) {
        if ((inb(reg) & mask) == mask) return 1;
        if (ticks() - start >= limit) return 0;
    }
}

void int14_handler(struct armregs *f)
{
    static const uint16_t divisors[8] = { 1047, 768, 384, 192, 96, 48, 24, 12 };
    unsigned port = f->r3 & 0xFFFF;
    uint16_t base = port < 4 ? BDA16(port * 2) : 0;
    if (!base) { set_ah(f, 0x80); return; }
    switch (AH(f)) {
    case 0x00: {
        uint16_t d = divisors[AL(f) >> 5];
        outb(base + 3, 0x80);
        outb(base + 0, d & 0xFF);
        outb(base + 1, d >> 8);
        outb(base + 3, AL(f) & 0x1F);
        outb(base + 1, 0x00);
        set_ah(f, inb(base + 5)); set_al(f, inb(base + 6));
        break;
    }
    case 0x01:
        outb(base + 4, 0x03);
        if (!com_wait(base + 6, 0x30, port) || !com_wait(base + 5, 0x20, port)) {
            set_ah(f, inb(base + 5) | 0x80); break;
        }
        outb(base, AL(f));
        set_ah(f, inb(base + 5));
        break;
    case 0x02:
        outb(base + 4, 0x01);
        if (!com_wait(base + 6, 0x20, port) || !com_wait(base + 5, 0x01, port)) {
            set_ah(f, (inb(base + 5) & 0x1E) | 0x80); break;
        }
        set_ah(f, inb(base + 5) & 0x1E);
        set_al(f, inb(base));
        break;
    case 0x03:
        set_ah(f, inb(base + 5)); set_al(f, inb(base + 6));
        break;
    default:
        set_ah(f, 0x80);
        break;
    }
}

void int17_handler(struct armregs *f)
{
    if ((f->r3 & 0xFFFF) != 0) { set_ah(f, 0x00); return; }   /* LPT2+: none */
    switch (AH(f)) {
    case 0x00:
        outb(0x378, AL(f));
        outb(0x37A, 0x0D);      /* strobe */
        outb(0x37A, 0x0C);
        /* fall through */
    case 0x01:
    case 0x02:
        set_ah(f, 0x90);        /* not busy, selected */
        break;
    }
}

void int18_handler(struct armregs *f)
{
    (void)f;
    video_puts("\r\nNO ROM BASIC - SYSTEM HALTED\r\n"
               "(An ARM/AT has no BASIC in ROM; try BASIC.EXE from the hard disk.)\r\n");
    irq_enable();
    for (;;) wfi();
}

void int19_handler(struct armregs *f)
{
    (void)f;
    boot_system();
}

/* ------------------------------------------------------ the crash screen */

static const char *fault_name(int n)
{
    switch (n) {
    case 0x00: return "DIVIDE ERROR";
    case 0x06: return "UNDEFINED INSTRUCTION";
    case 0x0D: return "DATA ABORT";
    case 0x0E: return "PREFETCH ABORT";
    }
    return "EXCEPTION";
}

void fault_handler(struct armregs *f)
{
    int row, col;
    if (video_mono()) {         /* the card may be in Hercules graphics: back to text, keep the screen */
        video_get_cursor(&row, &col);
        video_set_mode(0x87);
        video_goto(row, col);
    } else if (BDA8(BDA_VMODE) > 3) video_set_mode(3);
    video_get_cursor(&row, &col);
    (void)col;
    if (row > 13) { video_scroll(1, row - 13, 0x07, 0, 0, 24, 79); row = 13; }
    video_fill(row + 1, 0, 80 * 10, ' ', 0x4F);
    char buf[96];
    snprintf(buf, sizeof buf, " ARM/AT: %s at %08X (INT %02Xh)", fault_name(f->intno), f->pc, f->intno);
    video_write_at(row + 1, 1, buf, 0x4E);
    static const char *names[13] = { "R0 AX", "R1 BX", "R2 CX", "R3 DX", "R4 SI", "R5 DI", "R6 BP",
                                     "R7   ", "R8   ", "R9   ", "R10  ", "R11  ", "R12  " };
    const uint32_t *r = &f->r0;
    for (int i = 0; i < 13; i++) {
        snprintf(buf, sizeof buf, "%s=%08X", names[i], r[i]);
        video_write_at(row + 3 + i / 4, 2 + (i % 4) * 19, buf, 0x4F);
    }
    snprintf(buf, sizeof buf, "SP   =%08X    LR   =%08X    PC   =%08X    CPSR =%08X", f->sp, f->lr, f->pc, f->cpsr);
    video_write_at(row + 7, 2, buf, 0x4F);
    video_write_at(row + 9, 2, "System halted.  Press Ctrl+Alt+Del to restart.", 0x4E);
    video_goto(row + 11 > 24 ? 24 : row + 11, 0);
    dprintf("FAULT %s pc=%08X lr=%08X sp=%08X cpsr=%08X\n", fault_name(f->intno), f->pc, f->lr, f->sp, f->cpsr);
    outb(0x20, 0x20);
    outb(0xA0, 0x20);
    irq_enable();
    for (;;) wfi();
}

/* ------------------------------------------------------------- POST */

static void banner(void)
{
    video_set_mode(video_default_mode());
    video_cursor_shape(0x20, 0x00);
    for (int i = 0; i < 3; i++) video_write_at(1 + i, 62, logo[i], i == 2 ? A_LOGO2 : A_LOGO1);
    video_write_at(4, 62, "ARM/AT  \xB3 32-BIT", A_LOGO2);
    video_goto(1, 0);
    kprintf("Europa ARM/AT BIOS Version %s\n", BIOS_VERSION);
    kprintf("Copyright (C) 1988, Europa Micro Systems\n\n");
    uint32_t id = read_cpuid();
    kprintf("ARM926EJ-S CPU at %d MHz  (ID %08X, ARMv5TE)\n", cpu_mhz(), id);
    video_write_at(23, 0, "Press DEL to enter SETUP, ESC to skip memory test", A_NORM);
    char idline[64];
    snprintf(idline, sizeof idline, "%s-ARM926-EUROPA-AT-%s", BIOS_DATE, "01");
    video_write_at(24, 0, idline, A_NORM);
}

static int test_block(uint32_t base, uint32_t len)
{
    volatile uint32_t *p = (volatile uint32_t *)base;
    uint32_t n = len / 4;
    for (uint32_t i = 0; i < n; i++) p[i] = (base + i * 4) ^ 0xA5A5A5A5u;
    for (uint32_t i = 0; i < n; i++) if (p[i] != ((base + i * 4) ^ 0xA5A5A5A5u)) return 0;
    for (uint32_t i = 0; i < n; i++) p[i] = 0;
    return 1;
}

/* How much is installed: the SIMMs end where a 64 KB block stops holding a pattern (an empty
   socket floats high). The BIOS lives in the first 64 KB above 1 MB, so there is at least that.
   (The board relocates the 384 KB behind the adapter hole to the top of extended memory, up to
   16 MB.) CMOS 30h/31h get the size, as on an AT. */
uint32_t ram_end;

static void size_memory(void)
{
    uint32_t a = HMA_END;
    while (a < RAM_END) {
        volatile uint32_t *p = (volatile uint32_t *)a;
        uint32_t save = p[0];
        p[0] = 0x5AA5C33Cu;
        p[1] = 0;                               /* no floating-bus echo of the last value */
        int ok = p[0] == 0x5AA5C33Cu;
        p[0] = ~0x5AA5C33Cu;
        ok = ok && p[0] == ~0x5AA5C33Cu;
        p[0] = save;
        if (!ok) break;
        a += 0x10000;
    }
    ram_end = a;
    uint32_t ext = (ram_end - EXT_MEM_START) / 1024;
    cmos_write(0x30, ext & 0xFF);
    cmos_write(0x31, ext >> 8);
}

static void memory_test(int quick)
{
    int row, col;
    video_get_cursor(&row, &col);
    (void)col;
    const uint32_t total_k = 640 + (ram_end - EXT_MEM_START) / 1024;
    char buf[48];
    int ok = 1;
    if (!quick) {
        /* a cold start: a moment at 0K, as if the board were still being probed,
           so the screen can be read (and DEL pressed) before the count runs */
        video_write_at(row, 0, "Memory Test :      0K", A_NORM);
        for (int i = 0; i < 8 && !esc_pressed && !del_pressed; i++) { poll_keys(); delay_ticks(1); }
        if (esc_pressed) quick = 1;
    }
    for (uint32_t a = 0; a < ram_end; a += 0x10000) {
        if (a >= 0xA0000 && a < EXT_MEM_START) continue;       /* the video/ROM hole */
        poll_keys();
        if (esc_pressed) quick = 1;
        uint32_t done_k = (a < 0xA0000) ? (a + 0x10000) / 1024 : 640 + (a + 0x10000 - EXT_MEM_START) / 1024;
        if (quick) done_k = total_k;
        else {
            uint32_t start = a < 0x10000 ? 0x1000 : a;          /* the IVT and BDA are live */
            int bios_own = a >= EXT_MEM_START && a < HMA_END;   /* the BIOS's own stacks */
            if (!bios_own && !test_block(start, a + 0x10000 - start)) ok = 0;
        }
        if ((a & 0x3FFFF) == 0x30000 || quick) {
            snprintf(buf, sizeof buf, "Memory Test : %6uK", done_k);
            video_write_at(row, 0, buf, A_NORM);
            /* paced like a 1988 board (about two seconds for 16 MB): a tick per 256 KB
               of base memory, two per megabyte of extended */
            if (!quick) {
                if (a < 0xA0000) delay_ticks(1);
                else if ((a & 0xFFFFF) == 0xF0000) delay_ticks(2);
            }
        }
        if (quick) break;
    }
    snprintf(buf, sizeof buf, "Memory Test : %6uK %s", total_k, ok ? "OK" : "FAILED");
    video_write_at(row, 0, buf, A_NORM);
    video_goto(row + 2, 0);
}

static int detect_mouse(void)
{
    outb(0x64, 0xD4);
    outb(0x60, 0xF5);           /* disable reporting: answers ACK if a mouse is there */
    for (int i = 0; i < 2000; i++) {
        if ((inb(0x64) & 0x21) == 0x21) return inb(0x60) == 0xFA;
    }
    return 0;
}

/* ~us microseconds, timed by port 61h's 15 us refresh toggle (not by counting instructions) */
static void io_delay_us(unsigned us)
{
    unsigned n = us / 15 + 1;
    uint8_t last = inb(0x61) & 0x10;
    while (n) {
        uint8_t v = inb(0x61) & 0x10;
        if (v != last) { last = v; n--; }
    }
}

/* the cards in the slots (for the configuration box) */
static int sound_card;                  /* 0 none, 1 AdLib (OPL only), 2 Sound Blaster 16 */
static int midi_card, com_ports, cd_found;
static char cd_model[41];

static int detect_sb16(void)
{
    outb(0x226, 1); io_delay_us(4); outb(0x226, 0);
    for (int i = 0; i < 8; i++) {
        io_delay_us(15);
        if ((inb(0x22E) & 0x80) && inb(0x22A) == 0xAA) return 1;
    }
    return 0;
}

static void opl_write(int reg, int v) { outb(0x388, reg); outb(0x389, v); }
static int detect_opl(void)             /* the AdLib test: timer 1 must overflow in 80 us */
{
    opl_write(4, 0x60); opl_write(4, 0x80);
    uint8_t s1 = inb(0x388);
    opl_write(2, 0xFF); opl_write(4, 0x21);
    io_delay_us(100);
    uint8_t s2 = inb(0x388);
    opl_write(4, 0x60); opl_write(4, 0x80);
    return (s1 & 0xE0) == 0 && (s2 & 0xE0) == 0xC0;
}

static int detect_uart(uint16_t base)   /* the scratch register holds what was written */
{
    outb(base + 7, 0x5A);
    if (inb(base + 7) != 0x5A) return 0;
    outb(base + 7, 0xA5);
    return inb(base + 7) == 0xA5;
}

/* ATAPI on the secondary IDE channel: IDENTIFY PACKET DEVICE for the model name */
static int detect_cdrom(void)
{
    uint8_t st = inb(0x177);
    if (st == 0xFF) return 0;
    outb(0x176, 0xA0);
    outb(0x177, 0xA1);
    for (int i = 0; i < 20000 && ((st = inb(0x177)) & 0x80); i++) ;
    if ((st & 0x89) != 0x08) return 0;
    for (int i = 0; i < 256; i++) {
        uint16_t w = inw(0x170);
        if (i >= 27 && i < 47) { cd_model[(i - 27) * 2] = w >> 8; cd_model[(i - 27) * 2 + 1] = w & 0xFF; }
    }
    cd_model[40] = 0;
    for (int i = 39; i >= 0 && cd_model[i] == ' '; i--) cd_model[i] = 0;
    return 1;
}

static void detect_devices(int quick)
{
    kprintf("Detecting floppy drive A: ... ");
    outb(0x307, 0x03);          /* recalibrate: the head steps to track 0 */
    if (!quick) delay_ticks(6);
    static const char *const fdn[] = { "None", "360K 5.25\"", "1.2M 5.25\"", "720K 3.5\"", "1.44M 3.5\"" };
    int m = floppy_media();
    kprintf("%s\n", m ? fdn[m] : "1.44M 3.5\" (no diskette)");
    kprintf("Detecting IDE primary master ... ");
    if (!quick) delay_ticks(4);
    if (hd_sectors()) kprintf("%s, %u MB\n", hd_model[0], hd_sectors() / 2048);
    else kprintf("None\n");
    kprintf("Detecting IDE primary slave ... ");
    if (hd_sectors_of(1)) kprintf("%s, %u MB\n", hd_model[1], hd_sectors_of(1) / 2048);
    else kprintf("None\n");
    kprintf("Detecting IDE secondary master ... ");
    cd_found = detect_cdrom();
    kprintf("%s\n", cd_found ? cd_model : "None");
    kprintf("Detecting PS/2 mouse ... ");
    uint32_t ps = irq_save();
    outb(0xA1, 0xFF);           /* keep INT 74h from eating the ACK */
    int mouse = detect_mouse();
    outb(0xA1, 0xEF);
    irq_restore(ps);
    kprintf("%s\n", mouse ? "Installed" : "None");
    /* the serial ports: COM1 on the I/O card, COM2 = the internal modem when it is fitted */
    uint16_t com[2] = { 0x3F8, 0x2F8 };
    com_ports = 0;
    for (int i = 0; i < 4; i++) BDA16(i * 2) = 0;
    for (int i = 0; i < 2; i++) if (detect_uart(com[i])) BDA16(com_ports++ * 2) = com[i];
    sound_card = detect_sb16() ? 2 : detect_opl() ? 1 : 0;
    midi_card = inb(0x331) != 0xFF;             /* the MPU-401's status port (no command: nothing wakes up) */
    uint16_t eq = 0x0001 | 0x4000 | (com_ports << 9);   /* bits 9-11: serial ports */
    eq |= video_mono() ? 0x0030 : 0x0020;       /* bits 4-5: 11 = 80x25 mono, 10 = 80x25 colour */
    if (mouse) eq |= 0x0004;
    if (inb(JOY_PORT) != 0xFF) eq |= EQUIP_GAME;  /* bit 12: a game adapter answers at 201h */
    BDA16(BDA_EQUIP) = eq;
}

static void box_row(char *b, const char *l1, const char *v1, const char *l2, const char *v2)
{
    snprintf(b, 80, "%-16s : %-13s  %-16s : %s", l1, v1, l2, v2);
}

static void config_box(void)
{
    const int r = 1, nrows = 8;
    video_set_mode(video_default_mode());
    video_fill(r, 1, 1, 0xC9, 0x0B); video_fill(r, 2, 76, 0xCD, 0x0B); video_fill(r, 78, 1, 0xBB, 0x0B);
    const char *title = " ARM/AT System Configuration (C) 1988 Europa Micro Systems ";
    video_write_at(r, 40 - (int)strlen(title) / 2, title, 0x0F);
    int m = floppy_media();
    static const char *const fdn[] = { "1.44 MB, 3\xAB\"", "360 KB, 5\xAB\"", "1.2 MB, 5\xAB\"", "720 KB, 3\xAB\"", "1.44 MB, 3\xAB\"" };
    char hd[24], hd2[24], clk[16], ext[16], ser[16];
    if (hd_sectors()) snprintf(hd, sizeof hd, "%u MB, LBA", hd_sectors() / 2048); else snprintf(hd, sizeof hd, "None");
    if (hd_sectors_of(1)) snprintf(hd2, sizeof hd2, "%u MB, LBA", hd_sectors_of(1) / 2048); else snprintf(hd2, sizeof hd2, "None");
    snprintf(clk, sizeof clk, "%d MHz, RISC", cpu_mhz());
    snprintf(ext, sizeof ext, "%u KB", (ram_end - EXT_MEM_START) / 1024);
    ser[0] = 0;
    for (int i = 0; i < com_ports; i++) snprintf(ser + strlen(ser), sizeof ser - strlen(ser), "%s%X", i ? "," : "", BDA16(i * 2));
    if (!com_ports) snprintf(ser, sizeof ser, "None");
    const char *disp = video_card == VID_HERCULES ? "Monochrome (Hercules)" : video_mono() ? "Monochrome" : "VGA/EGA";
    static const char *const snd[] = { "None", "AdLib 388h", "SB16 220h" };
    char rows[8][80];
    box_row(rows[0], "Main Processor", "ARM926EJ-S", "Base Memory Size", "640 KB");
    box_row(rows[1], "Math Coprocessor", "VFP9-S", "Ext. Memory Size", ext);
    box_row(rows[2], "Clock / ISA", clk, "Display Type", disp);
    box_row(rows[3], "Floppy Drive A:", fdn[m], "Hard Disk C:", hd);
    box_row(rows[4], "Floppy Drive B:", "None", "Hard Disk D:", hd2);
    box_row(rows[5], "Pointing Device", (BDA16(BDA_EQUIP) & 4) ? "PS/2" : "None", "CD-ROM Drive", cd_found ? "ATAPI, 2nd IDE" : "None");
    box_row(rows[6], "Sound Card", snd[sound_card], "Serial Port(s)", ser);
    box_row(rows[7], "MIDI Interface", midi_card ? "MPU-401 330h" : "None", "Parallel Port(s)", "378");
    for (int i = 0; i < nrows; i++) {
        video_fill(r + 1 + i, 1, 1, 0xBA, 0x0B);
        video_fill(r + 1 + i, 2, 76, ' ', 0x07);
        video_write_at(r + 1 + i, 3, rows[i], 0x0F);
        video_fill(r + 1 + i, 78, 1, 0xBA, 0x0B);
    }
    video_fill(r + nrows + 1, 1, 1, 0xC8, 0x0B); video_fill(r + nrows + 1, 2, 76, 0xCD, 0x0B); video_fill(r + nrows + 1, 78, 1, 0xBC, 0x0B);
    video_goto(r + nrows + 3, 0);
    video_cursor_default();
}

void bios_main(void)
{
    int warm = BDA16(BDA_WARMBOOT) == 0x1234;

    /* the PIC: everything masked except timer, keyboard and mouse */
    outb(0x20, 0x20); outb(0xA0, 0x20);
    outb(0x21, 0xFC);
    outb(0xA1, 0xEF);

    /* the UARTs: MCR = 0 drops DTR and RTS, as an AT BIOS's reset left them, so a modem
       set to &D2 hangs up on Ctrl-Alt-Del or RESET instead of staying online */
    outb(0x3FC, 0x00);
    outb(0x2FC, 0x00);

    install_vectors();
    for (int i = 0; i < 256; i++) BDA8(i) = 0;
    BDA16(BDA_MEMKB) = 640;
    BDA16(0x00) = 0x3F8;        /* COM1 */
    BDA16(0x02) = 0x2F8;        /* COM2: the internal modem (docs/MODEM.md) */
    BDA16(0x08) = 0x378;        /* LPT1 */
    BDA16(BDA_EQUIP) = 0x4421;
    BDA8(0x7C) = 1; BDA8(0x7D) = 1;   /* INT 14h timeouts, seconds */

    if (cmos_read(CMOS_VALID) != 0xA5) {
        cmos_write(CMOS_BOOT, 0);
        cmos_write(CMOS_FLAGS, 0);
        cmos_write(CMOS_COLOR, 0);
        cmos_write(CMOS_FLOPPY, 0x40);
        cmos_write(CMOS_VALID, 0xA5);
    }
    int flags = cmos_read(CMOS_FLAGS);
    int quick = warm || (flags & 1);

    timer_init();
    kbd_init();
    disk_init();
    irq_enable();
    video_detect();             /* VGA at 3D4h, or MDA/Hercules at 3B4h */
    BDA16(BDA_EQUIP) = video_mono() ? 0x4431 : 0x4421;

    size_memory();
    banner();
    memory_test(quick);
    detect_devices(quick);

    if (!(flags & 2)) beep(1000, 110);
    /* last chance for DEL */
    for (int i = 0; i < (quick ? 4 : 14) && !del_pressed; i++) { poll_keys(); delay_ticks(1); }
    poll_keys();
    if (del_pressed) setup_utility();

    config_box();
    BDA16(BDA_WARMBOOT) = 0;
    boot_system();
}

/* ------------------------------------------------------------- INT 19h */

static int try_boot(int drive)
{
    uint8_t *sec = (uint8_t *)0x7C00;
    for (int attempt = 0; attempt < 3; attempt++) {
        int e = disk_read_lba(drive, 0, 1, sec);
        if (e == 0x80) return 0;
        if (e == 0) {
            if (sec[510] == 0x55 && sec[511] == 0xAA) return 1;
            return 0;
        }
    }
    return 0;
}

void boot_system(void)
{
    int order = cmos_read(CMOS_BOOT);
    for (;;) {
        int drives[2] = { 0, 0x80 };
        int n = 2;
        if (order == 1) { drives[0] = 0x80; drives[1] = 0; }
        if (order == 2) { drives[0] = 0x80; n = 1; }
        for (int i = 0; i < n; i++) {
            if (drives[i] == 0 && !floppy_present()) continue;
            if (drives[i] == 0x80 && !hd_sectors()) continue;
            if (try_boot(drives[i])) {
                dprintf("BIOS: booting from drive %02X\n", drives[i]);
                bios_enter_sys(0x7C00, 0x7C00, drives[i], 1);
            }
        }
        video_puts("\r\nDISK BOOT FAILURE, INSERT SYSTEM DISK AND PRESS ENTER\r\n");
        kbd_get();
    }
}
