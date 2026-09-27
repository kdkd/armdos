/* ARM/AT BIOS — the keyboard: INT 09h (IRQ1) and INT 16h */
#include "bios.h"

/* flags at 40:17 */
#define RSHIFT  0x01
#define LSHIFT  0x02
#define CTRL    0x04
#define ALT     0x08
#define SCROLL  0x10
#define NUM     0x20
#define CAPS    0x40
#define INSERT  0x80

static const char k_normal[0x3A] =
    "\0\x1b" "1234567890-=\b\t" "qwertyuiop[]\r\0" "asdfghjkl;'`\0\\" "zxcvbnm,./\0*\0 ";
static const char k_shift[0x3A] =
    "\0\x1b" "!@#$%^&*()_+\b\0" "QWERTYUIOP{}\r\0" "ASDFGHJKL:\"~\0|" "ZXCVBNM<>?\0*\0 ";
static const char kp_digits[13] = "789-456+1230.";

static uint8_t e0_pending;
static uint8_t e1_pending;
static uint8_t alt_num;         /* Alt+keypad composition */
static uint8_t alt_num_used;

int kbd_stuff(uint16_t key)
{
    uint16_t tail = BDA16(BDA_KBTAIL);
    uint16_t next = tail + 2;
    if (next >= BDA16(BDA_KBEND)) next = BDA16(BDA_KBSTART);
    if (next == BDA16(BDA_KBHEAD)) {
        return 0;               /* buffer full: a PC beeps here */
    }
    *(volatile uint16_t *)(0x400 + tail) = key;
    BDA16(BDA_KBTAIL) = next;
    return 1;
}

void kbd_flush(void)
{
    BDA16(BDA_KBHEAD) = BDA16(BDA_KBTAIL);
}

static int kb_raw_peek(void)
{
    uint16_t head = BDA16(BDA_KBHEAD);
    if (head == BDA16(BDA_KBTAIL)) return -1;
    return *(volatile uint16_t *)(0x400 + head);
}

static int kb_raw_get(void)
{
    uint32_t s = irq_save();
    int k = kb_raw_peek();
    if (k >= 0) {
        uint16_t head = BDA16(BDA_KBHEAD) + 2;
        if (head >= BDA16(BDA_KBEND)) head = BDA16(BDA_KBSTART);
        BDA16(BDA_KBHEAD) = head;
    }
    irq_restore(s);
    return k;
}

void kbd_init(void)
{
    BDA16(BDA_KBSTART) = 0x1E;
    BDA16(BDA_KBEND) = 0x3E;
    BDA16(BDA_KBHEAD) = 0x1E;
    BDA16(BDA_KBTAIL) = 0x1E;
    BDA8(BDA_KBFLAGS) = (cmos_read(CMOS_FLAGS) & 4) ? 0 : NUM;
    BDA8(BDA_KBFLAGS2) = 0;
    BDA8(BDA_KBFLAGS3) = 0x10;  /* 101/102-key keyboard */
    /* drain the controller */
    for (int i = 0; i < 16 && (inb(0x64) & 1); i++) (void)inb(0x60);
}

static void reboot(void)
{
    BDA16(BDA_WARMBOOT) = 0x1234;
    extern void reset_entry(void);
    outb(0x20, 0x20);
    reset_entry();
}

static uint16_t translate(uint8_t sc, uint8_t flags, int e0)
{
    int shift = flags & (LSHIFT | RSHIFT);
    int ctrl = flags & CTRL, alt = flags & ALT;

    /* function keys */
    if (sc >= 0x3B && sc <= 0x44) {
        int n = sc - 0x3B;
        if (alt) return (0x68 + n) << 8;
        if (ctrl) return (0x5E + n) << 8;
        if (shift) return (0x54 + n) << 8;
        return sc << 8;
    }
    if (sc == 0x57 || sc == 0x58) {
        int n = sc - 0x57;
        if (alt) return (0x8B + n) << 8;
        if (ctrl) return (0x89 + n) << 8;
        if (shift) return (0x87 + n) << 8;
        return (0x85 + n) << 8;
    }
    /* grey keys and keypad */
    if (sc >= 0x47 && sc <= 0x53 && sc != 0x4A && sc != 0x4E) {
        if (!e0 && !alt && (((flags & NUM) != 0) ^ (shift != 0)))
            return (sc << 8) | (uint8_t)kp_digits[sc - 0x47];
        if (ctrl) {
            static const uint8_t ctl[13] = { 0x77, 0x8D, 0x84, 0, 0x73, 0x8F, 0x74, 0, 0x75, 0x91, 0x76, 0x92, 0x93 };
            return (ctl[sc - 0x47] << 8) | (e0 ? 0xE0 : 0);
        }
        if (alt && e0) return (sc + 0x50) << 8;
        return (sc << 8) | (e0 ? 0xE0 : 0);
    }
    if (sc == 0x4A) return alt ? 0x4A00 : ctrl ? 0x8E00 : 0x4A2D;
    if (sc == 0x4E) return alt ? 0x4E00 : ctrl ? 0x9000 : 0x4E2B;
    if (e0 && sc == 0x1C) return ctrl ? 0xE00A : alt ? 0xA600 : 0xE00D;
    if (e0 && sc == 0x35) return alt ? 0xA400 : ctrl ? 0x9500 : 0xE02F;
    if (sc == 0x37 && !e0) return alt ? 0x3700 : ctrl ? 0x9600 : 0x372A;

    if (sc >= 0x3A) return 0;

    if (alt) {
        if (sc >= 0x02 && sc <= 0x0D) return (sc + 0x76) << 8;   /* Alt+1..= : 78h..83h */
        if (sc == 0x39) return 0x3920;
        if (sc == 0x0E) return 0x0E00;
        if (sc == 0x1C) return 0x1C00;
        if (sc == 0x0F) return 0xA500;
        if (sc == 0x01) return 0x0100;
        return sc << 8;
    }
    if (ctrl) {
        char c = k_normal[sc];
        if (c >= 'a' && c <= 'z') return (sc << 8) | (c - 'a' + 1);
        switch (sc) {
        case 0x01: return 0x011B;
        case 0x03: return 0x0300;
        case 0x07: return 0x071E;
        case 0x0C: return 0x0C1F;
        case 0x0E: return 0x0E7F;
        case 0x0F: return 0x9400;
        case 0x1A: return 0x1A1B;
        case 0x1B: return 0x1B1D;
        case 0x1C: return 0x1C0A;
        case 0x2B: return 0x2B1C;
        case 0x39: return 0x3920;
        }
        return 0;
    }
    char c = shift ? k_shift[sc] : k_normal[sc];
    if (flags & CAPS) {
        if (c >= 'a' && c <= 'z') c -= 32;
        else if (c >= 'A' && c <= 'Z') c += 32;
    }
    if (sc == 0x0F && shift) return 0x0F00;
    if (!c) return 0;
    return (sc << 8) | (uint8_t)c;
}

void int09_handler(struct armregs *f)
{
    (void)f;
    uint8_t status = inb(0x64);
    if (!(status & 1) || (status & 0x20)) { outb(0x20, 0x20); return; }
    uint8_t code = inb(0x60);

    /* INT 15h AH=4Fh: the keyboard intercept a program may hook */
    struct armregs r = { 0 };
    r.r0 = 0x4F00 | code;
    r.cpsr = CF;
    bios_int(0x15, &r);
    if (!(r.cpsr & CF)) { outb(0x20, 0x20); return; }
    code = r.r0 & 0xFF;

    if (code == 0xE0) { e0_pending = 1; BDA8(BDA_KBFLAGS3) |= 0x02; outb(0x20, 0x20); return; }
    if (code == 0xE1) { e1_pending = 2; outb(0x20, 0x20); return; }
    if (e1_pending) {
        /* Pause: E1 1D 45 E1 9D C5 */
        if (--e1_pending == 0 && code == 0x45) {
            BDA8(BDA_KBFLAGS2) |= 0x08;
            outb(0x20, 0x20);
            irq_enable();
            while (BDA8(BDA_KBFLAGS2) & 0x08) wfi();
            irq_disable();
            return;
        }
        if (code == 0xC5 || code == 0x9D) { outb(0x20, 0x20); return; }
        outb(0x20, 0x20);
        return;
    }

    int e0 = e0_pending;
    e0_pending = 0;
    BDA8(BDA_KBFLAGS3) &= ~0x02;
    int brk = code & 0x80;
    uint8_t sc = code & 0x7F;
    volatile uint8_t *fl = (volatile uint8_t *)(0x400 + BDA_KBFLAGS);
    volatile uint8_t *fl2 = (volatile uint8_t *)(0x400 + BDA_KBFLAGS2);

    /* fake shifts from E0 sequences (E0 2A / E0 AA around grey keys) */
    if (e0 && (sc == 0x2A || sc == 0x36)) { outb(0x20, 0x20); return; }

    /* any make ends a pause */
    if (!brk && (*fl2 & 0x08)) { *fl2 &= ~0x08; outb(0x20, 0x20); return; }

    switch (sc) {
    case 0x2A: if (brk) *fl &= ~LSHIFT; else *fl |= LSHIFT; goto done;
    case 0x36: if (brk) *fl &= ~RSHIFT; else *fl |= RSHIFT; goto done;
    case 0x1D:
        if (brk) { *fl &= ~CTRL; if (e0) BDA8(BDA_KBFLAGS3) &= ~0x04; else *fl2 &= ~0x01; }
        else { *fl |= CTRL; if (e0) BDA8(BDA_KBFLAGS3) |= 0x04; else *fl2 |= 0x01; }
        goto done;
    case 0x38:
        if (brk) {
            *fl &= ~ALT;
            if (e0) BDA8(BDA_KBFLAGS3) &= ~0x08; else *fl2 &= ~0x02;
            if (alt_num_used) { kbd_stuff(alt_num); alt_num = 0; alt_num_used = 0; }
        } else {
            if (!(*fl & ALT)) { alt_num = 0; alt_num_used = 0; }
            *fl |= ALT;
            if (e0) BDA8(BDA_KBFLAGS3) |= 0x08; else *fl2 |= 0x02;
        }
        goto done;
    case 0x3A:
        if (brk) *fl2 &= ~0x40; else if (!(*fl2 & 0x40)) { *fl ^= CAPS; *fl2 |= 0x40; }
        goto done;
    case 0x45:
        if (brk) *fl2 &= ~0x20; else if (!(*fl2 & 0x20)) { *fl ^= NUM; *fl2 |= 0x20; }
        goto done;
    case 0x46:
        if (!brk && (*fl & CTRL)) {             /* Ctrl-Break */
            kbd_flush();
            BDA8(BDA_BREAK) |= 0x80;
            kbd_stuff(0x0000);
            outb(0x20, 0x20);
            struct armregs b = { 0 };
            bios_int(0x1B, &b);
            return;
        }
        if (brk) *fl2 &= ~0x10; else if (!(*fl2 & 0x10)) { *fl ^= SCROLL; *fl2 |= 0x10; }
        goto done;
    }
    if (brk) goto done;

    /* Ctrl-Alt-Del */
    if (sc == 0x53 && (*fl & CTRL) && (*fl & ALT)) reboot();

    /* Alt + keypad digits compose a character code */
    if ((*fl & ALT) && !e0 && sc >= 0x47 && sc <= 0x52) {
        char d = kp_digits[sc - 0x47];
        if (d >= '0' && d <= '9') { alt_num = alt_num * 10 + (d - '0'); alt_num_used = 1; goto done; }
    }

    uint16_t key = translate(sc, *fl, e0);
    if ((key >> 8) == 0x52 && (key & 0xFF) != '0') *fl ^= INSERT;
    if (key) kbd_stuff(key);
done:
    outb(0x20, 0x20);
}

int kbd_peek(void) { return kb_raw_peek(); }

int kbd_get(void)
{
    int k;
    for (;;) {
        irq_disable();
        k = kb_raw_peek();
        if (k >= 0) break;
        wfi();          /* wakes on the pending IRQ even with I masked */
        irq_enable();
    }
    irq_enable();
    return kb_raw_get();
}

/* INT 16h AH=00h/01h present extended keys the way an 84-key BIOS would */
static int compat_key(int k)
{
    if ((k & 0xFF) == 0xE0 && (k >> 8)) return k & 0xFF00;
    if ((k & 0xFF00) == 0xE000) return ((k & 0xFF) == 0x0D || (k & 0xFF) == 0x0A) ? (0x1C00 | (k & 0xFF)) : (0x3500 | (k & 0xFF));
    if ((k >> 8) > 0x84) return -1;     /* F11/F12 etc. are invisible to AH=00h */
    return k;
}

void int16_handler(struct armregs *f)
{
    int k;
    switch (AH(f)) {
    case 0x00:
        do { k = compat_key(kbd_get()); } while (k < 0);
        set_ax(f, k);
        break;
    case 0x10:
        set_ax(f, kbd_get());
        break;
    case 0x01:
        for (;;) {
            k = kb_raw_peek();
            if (k < 0) { set_zf(f, 1); break; }
            int c = compat_key(k);
            if (c < 0) { kb_raw_get(); continue; }
            set_ax(f, c); set_zf(f, 0); break;
        }
        break;
    case 0x11:
        k = kb_raw_peek();
        if (k < 0) set_zf(f, 1); else { set_ax(f, k); set_zf(f, 0); }
        break;
    case 0x02:
        set_al(f, BDA8(BDA_KBFLAGS));
        break;
    case 0x12: {
        uint8_t f2 = BDA8(BDA_KBFLAGS2), f3 = BDA8(BDA_KBFLAGS3);
        uint8_t hi = (f2 & 0x01) | ((f2 & 0x02) ? 0x02 : 0) | ((f3 & 0x04) ? 0x04 : 0) | ((f3 & 0x08) ? 0x08 : 0) |
                     (f2 & 0x70) | ((f2 & 0x04) ? 0x80 : 0);
        set_ax(f, (hi << 8) | BDA8(BDA_KBFLAGS));
        break;
    }
    case 0x03:
        break;
    case 0x05:
        set_al(f, kbd_stuff(f->r2 & 0xFFFF) ? 0 : 1);
        break;
    default:
        break;
    }
}
