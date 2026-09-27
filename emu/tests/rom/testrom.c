/* ARM-PC device test ROM. Exercises the machine's devices per ARCH.md and
   reports on the screen, COM1 and port E9. Interactive steps wait for keys:
   the headless test (tests/machine/rom-test.mjs) drives them.            */
#include <stdint.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32;

#define IOB(p) (*(volatile u8 *)(0x10000000u + (p)))
#define IOW(p) (*(volatile u16 *)(0x10000000u + (p)))
static inline void outb(int p, u8 v) { IOB(p) = v; }
static inline u8 inb(int p) { return IOB(p); }

extern const u8 font8x16[4096];
void *memcpy(void *d, const void *s, unsigned n) { u8 *a = d; const u8 *b = s; while (n--) *a++ = *b++; return d; }
void *memset(void *d, int c, unsigned n) { u8 *a = d; while (n--) *a++ = c; return d; }
void wfi(void); void irq_on(void); void irq_off(void);

volatile u32 ticks, irq_counts[16], faults;
volatile u8 kq[64]; volatile u32 kq_head, kq_tail;
volatile u8 mq[64]; volatile u32 mq_head, mq_tail;

/* ------------------------------------------------------------ output */
static volatile u16 *const vram = (volatile u16 *)0xB8000;
static int cx, cy; static u8 attr = 0x07;
static void scroll(void) {
  for (int i = 0; i < 80 * 24; i++) vram[i] = vram[i + 80];
  for (int i = 80 * 24; i < 80 * 25; i++) vram[i] = 0x0720;
  cy = 24;
}
static void setcursor(void) { u16 p = cy * 80 + cx; outb(0x3D4, 0x0E); outb(0x3D5, p >> 8); outb(0x3D4, 0x0F); outb(0x3D5, p & 0xFF); }
static void putc_(int c) {
  outb(0x3F8, c);
  if (c == '\n') { cx = 0; if (++cy >= 25) scroll(); }
  else if (c == '\r') cx = 0;
  else { vram[cy * 80 + cx] = (attr << 8) | (u8)c; if (++cx >= 80) { cx = 0; if (++cy >= 25) scroll(); } }
  setcursor();
}
static void puts_(const char *s) { while (*s) putc_(*s++); }
static void phex(u32 v, int d) { for (int i = d - 1; i >= 0; i--) putc_("0123456789ABCDEF"[(v >> (i * 4)) & 15]); }
static void pdec(u32 v) { char b[12]; int n = 0; do { b[n++] = '0' + v % 10; v /= 10; } while (v); while (n) putc_(b[--n]); }
static void ok(const char *what, int good) { attr = good ? 0x0A : 0x0C; puts_(good ? "[ OK ] " : "[FAIL] "); attr = 0x07; puts_(what); putc_('\n'); }
static void cls(void) { for (int i = 0; i < 80 * 25; i++) vram[i] = 0x0720; cx = cy = 0; setcursor(); }

/* ------------------------------------------------------------ interrupts */
void irq_handler(void) {
  u8 n = inb(0x20);
  if (n > 15) return;
  irq_counts[n]++;
  switch (n) {
    case 0: ticks++; break;
    case 1: { u8 s = inb(0x60); kq[kq_head++ & 63] = s; break; }
    case 12: { u8 s = inb(0x60); mq[mq_head++ & 63] = s; break; }
    case 14: inb(0x1F7); break;
    case 8: outb(0x70, 0x0C); inb(0x71); break;
  }
  if (n >= 8) outb(0xA0, 0x20);
  outb(0x20, 0x20);
}
void fault(int kind, u32 addr) { faults++; puts_("<fault "); pdec(kind); puts_(" at "); phex(addr, 8); puts_(">\n"); }

static int getkey(void) {        /* next make code (ignores breaks and E0 prefixes) */
  for (;;) {
    irq_off();
    if (kq_tail != kq_head) { u8 s = kq[kq_tail++ & 63]; irq_on(); if (s == 0xE0 || (s & 0x80)) continue; return s; }
    wfi();                       /* sleeps until the IRQ is pending, then irq_on takes it */
    irq_on();
  }
}
static void delay_ticks(u32 n) { u32 t = ticks + n; while ((int)(ticks - t) < 0) wfi(); }

/* ------------------------------------------------------------ tests */
static int ata_wait(void) { for (int i = 0; i < 100000; i++) { u8 s = inb(0x1F7); if (!(s & 0x80)) return s; } return 0xFF; }

static void test_ata(void) {
  u16 id[256];
  outb(0x3F6, 0x02);                              /* nIEN: poll */
  outb(0x1F6, 0xA0);
  outb(0x1F7, 0xEC);
  u8 st = ata_wait();
  if (!(st & 0x08)) { ok("ATA: no drive / IDENTIFY failed", 0); return; }
  for (int i = 0; i < 256; i++) id[i] = IOW(0x1F0);
  char model[41];
  for (int i = 0; i < 20; i++) { model[2 * i] = id[27 + i] >> 8; model[2 * i + 1] = id[27 + i] & 0xFF; }
  model[40] = 0; for (int i = 39; i >= 0 && model[i] == ' '; i--) model[i] = 0;
  u32 sectors = id[60] | ((u32)id[61] << 16);
  puts_("ATA model '"); puts_(model); puts_("' sectors "); pdec(sectors); puts_(" CHS "); pdec(id[1]); putc_('/'); pdec(id[3]); putc_('/'); pdec(id[6]); putc_('\n');
  int good = model[0] == 'A' && model[4] == 'P' && model[7] == 'F';
  ok("ATA IDENTIFY", good);
  /* read LBA 0 */
  u8 sec[512];
  outb(0x1F2, 1); outb(0x1F3, 0); outb(0x1F4, 0); outb(0x1F5, 0); outb(0x1F6, 0xE0); outb(0x1F7, 0x20);
  st = ata_wait();
  for (int i = 0; i < 256; i++) { u16 w = IOW(0x1F0); sec[2 * i] = w; sec[2 * i + 1] = w >> 8; }
  puts_("HD LBA0: "); for (int i = 0; i < 8; i++) { phex(sec[i], 2); putc_(' '); } puts_("sig "); phex(sec[510] | (sec[511] << 8), 4); putc_('\n');
  ok("ATA READ SECTORS", (st & 0x08) && !(ata_wait() & 0x08));
  /* write LBA 5 (2 sectors) then read back */
  outb(0x1F2, 2); outb(0x1F3, 5); outb(0x1F4, 0); outb(0x1F5, 0); outb(0x1F6, 0xE0); outb(0x1F7, 0x30);
  for (int s = 0; s < 2; s++) { ata_wait(); for (int i = 0; i < 256; i++) IOW(0x1F0) = (u16)(i * 3 + s * 7); }
  outb(0x1F7, 0xE7); ata_wait();
  outb(0x1F2, 2); outb(0x1F3, 5); outb(0x1F4, 0); outb(0x1F5, 0); outb(0x1F6, 0xE0); outb(0x1F7, 0x20);
  int bad = 0;
  for (int s = 0; s < 2; s++) { ata_wait(); for (int i = 0; i < 256; i++) if (IOW(0x1F0) != (u16)(i * 3 + s * 7)) bad++; }
  ok("ATA WRITE + readback", bad == 0);
  /* CHS addressing: C0 H0 S1 = LBA 0 */
  outb(0x1F2, 1); outb(0x1F3, 1); outb(0x1F4, 0); outb(0x1F5, 0); outb(0x1F6, 0xA0); outb(0x1F7, 0x20);
  ata_wait(); u16 w0 = IOW(0x1F0); for (int i = 1; i < 256; i++) (void)IOW(0x1F0);
  ok("ATA CHS read", w0 == (sec[0] | (sec[1] << 8)));
  outb(0x3F6, 0x00);
}

static void test_fdc(void) {
  u8 media = inb(0x302), st = inb(0x307);
  puts_("FDC media "); pdec(media); puts_(" status "); phex(st, 2); putc_('\n');
  if (!(st & 0x40)) { ok("FDC: no disk", 1); return; }
  volatile u8 *buf = (volatile u8 *)0x20000;
  u32 a = 0x20000;
  outb(0x300, a); outb(0x301, a >> 8); outb(0x302, a >> 16); outb(0x303, a >> 24);
  outb(0x304, 2); outb(0x305, 0); outb(0x306, 0); outb(0x307, 1);
  st = inb(0x307);
  puts_("FD LBA0: "); for (int i = 0; i < 8; i++) { phex(buf[i], 2); putc_(' '); } puts_("sig "); phex(buf[510] | (buf[511] << 8), 4); putc_('\n');
  ok("FDC DMA read", !(st & 1) && !(st & 0x20));
  outb(0x304, 1); outb(0x305, 0xFF); outb(0x306, 0xFF); outb(0x307, 1);
  ok("FDC out-of-range -> error", inb(0x307) & 1);
}

static void test_cmos(void) {
  outb(0x70, 0x0B); u8 b = inb(0x71);
  u8 t[7]; const u8 regs[7] = {0x32, 0x09, 0x08, 0x07, 0x04, 0x02, 0x00};
  for (int i = 0; i < 7; i++) { outb(0x70, regs[i]); t[i] = inb(0x71); }
  puts_("RTC "); phex(t[0], 2); phex(t[1], 2); putc_('-'); phex(t[2], 2); putc_('-'); phex(t[3], 2); putc_(' ');
  phex(t[4], 2); putc_(':'); phex(t[5], 2); putc_(':'); phex(t[6], 2); puts_(" regB "); phex(b, 2); putc_('\n');
  ok("RTC BCD + 24h", (b & 6) == 2 && t[0] >= 0x19 && t[0] <= 0x21 && (t[2] & 0xF) <= 9);
  outb(0x70, 0x40); outb(0x71, 0x5A); outb(0x70, 0x40);
  ok("CMOS battery RAM", inb(0x71) == 0x5A);
}

static void test_pit(void) {
  /* channel 0: 1000 Hz rate generator */
  outb(0x43, 0x34); outb(0x40, 1193 & 0xFF); outb(0x40, 1193 >> 8);
  outb(0x21, 0xFC); outb(0xA1, 0xEF);           /* IRQ0, IRQ1, IRQ12 */
  irq_on();
  u32 t0 = ticks;
  outb(0x70, 0x00); u8 s0 = inb(0x71);
  delay_ticks(100);
  u32 n = ticks - t0;
  outb(0x43, 0x00); u8 lo = inb(0x40), hi = inb(0x40);
  u32 cnt = lo | (hi << 8);
  puts_("PIT: 100 ticks, latched count "); pdec(cnt); puts_(", RTC sec "); phex(s0, 2); putc_('\n');
  ok("PIT IRQ0 at 1000 Hz", n >= 100 && cnt <= 1193 && cnt > 0);
  /* instructions per tick: port F8 counts millions */
  u8 mhz = inb(0xF1);
  puts_("CPU "); pdec(mhz); puts_(" MHz, turbo "); pdec(inb(0xF2)); putc_('\n');
  /* speaker: channel 2 square wave 440 Hz for 50 ms */
  outb(0x43, 0xB6); outb(0x42, (1193182 / 440) & 0xFF); outb(0x42, (1193182 / 440) >> 8);
  outb(0x61, inb(0x61) | 3); delay_ticks(50); outb(0x61, inb(0x61) & ~3);
  ok("speaker beep", 1);
  /* 0x61 bit 4 toggles */
  u8 a = inb(0x61) & 0x10; int toggles = 0;
  for (int i = 0; i < 2000; i++) { u8 b = inb(0x61) & 0x10; if (b != a) { toggles++; a = b; } }
  ok("port 61h refresh toggle", toggles > 0);
}

static void test_retrace(void) {
  int in = 0, out = 0;
  for (int i = 0; i < 200000; i++) { if (inb(0x3DA) & 8) in++; else out++; }
  ok("VGA retrace bit toggles", in > 0 && out > 0);
}

static void test_mouse(void) {
  outb(0x64, 0xA8);
  outb(0x64, 0xD4); outb(0x60, 0xF4);
  delay_ticks(5);
  int ack = 0; while (mq_tail != mq_head) if (mq[mq_tail++ & 63] == 0xFA) ack = 1;
  ok("PS/2 mouse enable ack", ack);
}

static void gfx(void) {
  /* mode 13h */
  outb(0x3E0, 0x13);
  volatile u8 *fb = (volatile u8 *)0xA0000;
  for (int y = 0; y < 200; y++) for (int x = 0; x < 320; x++) fb[y * 320 + x] = (x < 256 && y < 100) ? x : (y < 150 ? (x + y) & 0xFF : (x / 20) + 32);
  outb(0x3C8, 1); outb(0x3C9, 63); outb(0x3C9, 0); outb(0x3C9, 63);   /* DAC 1 = magenta */
  outb(0xE9, 'G'); outb(0xE9, '\n');
  getkey();
  /* mode 4: CGA 320x200x4 */
  outb(0x3E0, 0x04); outb(0x3D9, 0x30);
  volatile u8 *cga = (volatile u8 *)0xB8000;
  for (int y = 0; y < 200; y++) for (int x = 0; x < 80; x++) cga[((y & 1) << 13) + (y >> 1) * 80 + x] = ((x / 20) * 0x55) ^ (y & 0x10 ? 0x0F : 0);
  getkey();
  /* mode 6: 640x200x2 */
  outb(0x3E0, 0x06); outb(0x3D9, 0x0F);
  for (int y = 0; y < 200; y++) for (int x = 0; x < 80; x++) cga[((y & 1) << 13) + (y >> 1) * 80 + x] = (x + y) & 1 ? 0xAA : 0x55;
  getkey();
  outb(0x3E0, 0x03);
}

int main(void) {
  /* font into the character generator RAM: 32-byte stride */
  volatile u8 *fr = (volatile u8 *)0x11000000;
  for (int g = 0; g < 256; g++) for (int r = 0; r < 32; r++) fr[g * 32 + r] = r < 16 ? font8x16[g * 16 + r] : 0;
  outb(0x3E0, 0x03);
  cls();
  attr = 0x1F; puts_(" ARM-PC device test ROM                                                         "); attr = 0x07;
  puts_("\n");
  ok("board id 'A'", inb(0xF0) == 'A');
  volatile u8 *hole = (volatile u8 *)0xC0000;
  u8 h0 = hole[0x123]; hole[0x123] = 0;
  ok("adapter hole reads FF, ignores writes", h0 == 0xFF && hole[0x123] == 0xFF);
  ok("font RAM read back", fr[0x41 * 32 + 5] == font8x16[0x41 * 16 + 5]);
  volatile u32 *nowhere = (volatile u32 *)0x20000000;
  u32 f0 = faults; (void)*nowhere;
  ok("unmapped access -> data abort", faults == f0 + 1);
  test_pit();
  test_cmos();
  test_retrace();
  test_ata();
  test_fdc();
  test_mouse();
  /* colours */
  for (int i = 0; i < 16; i++) { attr = (i << 4) | (15 - i); puts_(" ab "); } attr = 0x07; putc_('\n');
  attr = 0x8E; puts_("blinking"); attr = 0x07; puts_("  box: \xC9\xCD\xCD\xBB \xC4\xC4\xC5\xC4 \xDB\xB2\xB1\xB0\n");
  puts_("Press keys; ESC continues to graphics.\n");
  for (;;) {
    int k = getkey();
    puts_("key "); phex(k, 2); putc_(' ');
    while (mq_tail != mq_head) { puts_("m"); phex(mq[mq_tail++ & 63], 2); putc_(' '); }
    if (k == 0x01) break;
  }
  putc_('\n');
  gfx();
  cls();
  puts_("TEST ROM DONE ticks=");
  pdec(ticks); putc_('\n');
  outb(0xF4, faults == 1 ? 0 : 1);           /* exit code (headless) */
  for (;;) wfi();
}
