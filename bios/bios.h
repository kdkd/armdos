/* ARM/AT BIOS — shared definitions */
#ifndef BIOS_H
#define BIOS_H

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

#define BIOS_VERSION   "1.00"
#define BIOS_DATE      "09/24/88"

/* ---------------------------------------------------------------- frame */

struct armregs {
    uint32_t r0, r1, r2, r3, r4, r5, r6, r7, r8, r9, r10, r11, r12;
    uint32_t sp, lr, pc, cpsr, intno;
    uint32_t _ksp, _pad;
};

#define CF 0x20000000u
#define ZF 0x40000000u

#define AX(f)  ((f)->r0 & 0xFFFF)
#define AH(f)  (((f)->r0 >> 8) & 0xFF)
#define AL(f)  ((f)->r0 & 0xFF)
#define BH(f)  (((f)->r1 >> 8) & 0xFF)
#define BL(f)  ((f)->r1 & 0xFF)
#define CH(f)  (((f)->r2 >> 8) & 0xFF)
#define CL(f)  ((f)->r2 & 0xFF)
#define DH(f)  (((f)->r3 >> 8) & 0xFF)
#define DL(f)  ((f)->r3 & 0xFF)

static inline void set_ah(struct armregs *f, uint8_t v) { f->r0 = (f->r0 & ~0xFF00u) | ((uint32_t)v << 8); }
static inline void set_al(struct armregs *f, uint8_t v) { f->r0 = (f->r0 & ~0xFFu) | v; }
static inline void set_ax(struct armregs *f, uint16_t v) { f->r0 = v; }
static inline void set_bh(struct armregs *f, uint8_t v) { f->r1 = (f->r1 & ~0xFF00u) | ((uint32_t)v << 8); }
static inline void set_bl(struct armregs *f, uint8_t v) { f->r1 = (f->r1 & ~0xFFu) | v; }
static inline void set_ch(struct armregs *f, uint8_t v) { f->r2 = (f->r2 & ~0xFF00u) | ((uint32_t)v << 8); }
static inline void set_cl(struct armregs *f, uint8_t v) { f->r2 = (f->r2 & ~0xFFu) | v; }
static inline void set_dh(struct armregs *f, uint8_t v) { f->r3 = (f->r3 & ~0xFF00u) | ((uint32_t)v << 8); }
static inline void set_dl(struct armregs *f, uint8_t v) { f->r3 = (f->r3 & ~0xFFu) | v; }
static inline void set_cf(struct armregs *f, int on) { if (on) f->cpsr |= CF; else f->cpsr &= ~CF; }
static inline void set_zf(struct armregs *f, int on) { if (on) f->cpsr |= ZF; else f->cpsr &= ~ZF; }

typedef void (*int_handler)(struct armregs *);

/* ------------------------------------------------------------- memory */

#define IVT         ((volatile uint32_t *)0x00000000)
#define BDA8(o)     (*(volatile uint8_t  *)(0x400 + (o)))
#define BDA16(o)    (*(volatile uint16_t *)(0x400 + (o)))
#define BDA32(o)    (*(volatile uint32_t *)(0x400 + (o)))

#define BDA_EQUIP        0x10
#define EQUIP_GAME       0x1000  /* equipment word bit 12: game adapter (201h) */
#define JOY_PORT         0x201
#define BDA_MEMKB        0x13
#define BDA_KBFLAGS      0x17
#define BDA_KBFLAGS2     0x18
#define BDA_KBHEAD       0x1A
#define BDA_KBTAIL       0x1C
#define BDA_KBBUF        0x1E   /* .. 0x3D */
#define BDA_FDSTATUS     0x41
#define BDA_VMODE        0x49
#define BDA_VCOLS        0x4A
#define BDA_VPAGESIZE    0x4C
#define BDA_VPAGESTART   0x4E
#define BDA_CURPOS       0x50   /* 8 words */
#define BDA_CURSHAPE     0x60
#define BDA_VPAGE        0x62
#define BDA_CRTC         0x63
#define BDA_CRTMODE      0x65
#define BDA_CGAPAL       0x66
#define BDA_TICKS        0x6C
#define BDA_MIDNIGHT     0x70
#define BDA_BREAK        0x71
#define BDA_WARMBOOT     0x72
#define BDA_HDSTATUS     0x74
#define BDA_HDCOUNT      0x75
#define BDA_KBSTART      0x80
#define BDA_KBEND        0x82
#define BDA_VROWS        0x84
#define BDA_CHARHEIGHT   0x85
#define BDA_VIDCTL       0x87
#define BDA_KBFLAGS3     0x96
#define BDA_KBLEDS       0x97

#define TEXTMEM     ((volatile uint16_t *)0xB8000)
#define MDAMEM      ((volatile uint16_t *)0xB0000)   /* MDA / Hercules text */
#define VGAMEM      ((volatile uint8_t  *)0xA0000)
#define CGAMEM      ((volatile uint8_t  *)0xB8000)
#define FONTRAM     ((volatile uint8_t  *)0x11000000)

#define EXT_MEM_START   0x00100000u
#define HMA_END         0x00110000u
#define RAM_END         0x01000000u   /* the most RAM the board takes (16 MB of SIMMs) */
extern uint32_t ram_end;                 /* what POST found installed (post.c size_memory) */

/* ---------------------------------------------------------------- I/O */

#define IO(p) (*(volatile uint8_t *)(0x10000000u + (p)))
static inline uint8_t inb(uint32_t p) { return IO(p); }
static inline void outb(uint32_t p, uint8_t v) { IO(p) = v; }
static inline uint16_t inw(uint32_t p) { return *(volatile uint16_t *)(0x10000000u + p); }
static inline void outw(uint32_t p, uint16_t v) { *(volatile uint16_t *)(0x10000000u + p) = v; }

/* ------------------------------------------------------------ start.S */

void wfi(void);
uint32_t irq_save(void);
void irq_restore(uint32_t);
void irq_enable(void);
void irq_disable(void);
uint32_t read_cpuid(void);
int bios_int(int n, struct armregs *r);
void bios_enter_sys(uint32_t pc, uint32_t sp, uint32_t r3, uint32_t irq_on) __attribute__((noreturn));
extern const uint8_t font8x16[4096];
extern const uint8_t font8x8[2048];

/* ---------------------------------------------------------- services */

void int10_handler(struct armregs *f);
void int11_handler(struct armregs *f);
void int12_handler(struct armregs *f);
void int13_handler(struct armregs *f);
void int14_handler(struct armregs *f);
void int15_handler(struct armregs *f);
void int16_handler(struct armregs *f);
void int17_handler(struct armregs *f);
void int18_handler(struct armregs *f);
void int19_handler(struct armregs *f);
void int1a_handler(struct armregs *f);
void int08_handler(struct armregs *f);
void int09_handler(struct armregs *f);
void int74_handler(struct armregs *f);
void irq_default_handler(struct armregs *f);
void fault_handler(struct armregs *f);
void iret_handler(struct armregs *f);

/* video.c */
void video_set_mode(int mode);
void video_putc(int c);                 /* teletype, page 0, attribute kept */
void video_putc_attr(int c, int attr);  /* teletype, writes attr on printable */
void video_puts(const char *s);
void video_goto(int row, int col);
void video_get_cursor(int *row, int *col);
void video_write_at(int row, int col, const char *s, int attr);
void video_fill(int row, int col, int n, int ch, int attr);
void video_cursor_shape(int start, int end);
void video_scroll(int up, int lines, int attr, int r1, int c1, int r2, int c2);
void video_load_text_font(void);
/* the display card: probed once at POST */
#define VID_VGA       0
#define VID_MDA       1         /* a monochrome adapter, no Hercules status toggling */
#define VID_HERCULES  2
extern int video_card;          /* VID_* */
int  video_detect(void);        /* probes 3D4h / 3B4h / 3BAh, sets video_card */
int  video_mono(void);          /* 1 on an MDA or Hercules */
int  video_default_mode(void);  /* 3 on the VGA, 7 on a mono card */
int  video_is_text(void);
void video_cursor_default(void);    /* the normal underline cursor for the mode */

/* kbd.c */
void kbd_init(void);
int  kbd_peek(void);                    /* -1 if empty, else scan<<8|ascii */
int  kbd_get(void);                     /* waits */
void kbd_flush(void);
int  kbd_stuff(uint16_t key);

/* timer.c */
void timer_init(void);
uint32_t ticks(void);
void delay_ticks(uint32_t n);
void beep(int hz, int ms);
void speaker(int hz);
uint8_t cmos_read(uint8_t reg);
void cmos_write(uint8_t reg, uint8_t v);
int bcd2bin(int v);
int bin2bcd(int v);

/* disk.c */
void disk_init(void);
int  disk_read_lba(int drive, uint32_t lba, int count, void *buf);
int  disk_write_lba(int drive, uint32_t lba, int count, const void *buf);
int  floppy_present(void);
int  floppy_media(void);
uint32_t hd_sectors(void);                 /* the master's (C:); 0 = none */
uint32_t hd_sectors_of(int unit);          /* 0 master, 1 slave (D:) */
extern char hd_model[2][41];

/* post.c / setup.c */
void bios_main(void);
void setup_utility(void);
void boot_system(void) __attribute__((noreturn));

/* lib.c */
void *memset(void *d, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);
void *memmove(void *d, const void *s, size_t n);
int memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
int vsnprintf(char *buf, size_t n, const char *fmt, va_list ap);
int snprintf(char *buf, size_t n, const char *fmt, ...);
void kprintf(const char *fmt, ...);     /* to the screen via teletype */
void dprintf(const char *fmt, ...);     /* to the debug port E9 */

/* CMOS layout (ARM/AT) */
#define CMOS_FLOPPY     0x10    /* high nibble drive A type: 4 = 1.44M */
#define CMOS_HD         0x12
#define CMOS_EQUIP      0x14
#define CMOS_BASEMEM_LO 0x15
#define CMOS_BASEMEM_HI 0x16
#define CMOS_EXTMEM_LO  0x17
#define CMOS_EXTMEM_HI  0x18
#define CMOS_BOOT       0x38    /* 0: A: then C:, 1: C: then A:, 2: C: only */
#define CMOS_FLAGS      0x39    /* bit0 quick boot, bit1 no POST beep, bit2 NumLock off, bit3 silent drives */
#define CMOS_VALID      0x3A    /* 0xA5 when the settings above have been written */
#define CMOS_COLOR      0x3B    /* setup colour scheme */

#endif
