/* display.h - DISPLAY.SYS for ARM-DOS: shared declarations (resident part and INIT) */
#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>
#include <armdos.h>

/* As in ANSI.SYS (apps/ansi/ansi.h): the resident part is ints.o, display.o,
   then mark.o (display_res_end); INIT (init.o) follows and is discarded. The
   prepared fonts are placed at display_res_end by INIT. Variables therefore go
   in .text.* sections too. */
#define RES   __attribute__((section(".text.display_data,\"ax\",%progbits @")))
#define RESRO __attribute__((section(".text.display_rodata,\"ax\",%progbits @")))
#define PACKED __attribute__((packed))

/* device driver interface (ARCH.md 14.4, 16) */
struct reqhdr {
    uint8_t  len, unit, cmd;
    uint16_t status;
    uint8_t  reserved[8];
} PACKED;

struct devhdr {
    struct devhdr *next;
    uint16_t attr, pad;
    void (*strategy)(struct reqhdr *);
    void (*entry)(void);
    char name[8];
};

struct req_init {               /* cmd 0 */
    struct reqhdr h;
    uint8_t  units;             /* +0D */
    uint32_t brk;               /* +0E in: limit, out: break address */
    uint32_t arg;               /* +12 in: rest of the DEVICE= line */
    uint8_t  drive;             /* +16 */
    uint16_t cfgerr;            /* +17 out: nonzero -> "Error in CONFIG.SYS line n" */
} PACKED;

struct req_rw {                 /* cmd 3 4 8 9 12 */
    struct reqhdr h;
    uint8_t  media;             /* +0D */
    uint32_t addr;              /* +0E */
    uint16_t count;             /* +12 */
} PACKED;

struct req_gioctl {             /* cmd 19 */
    struct reqhdr h;
    uint8_t  category;          /* +0D */
    uint8_t  minor;             /* +0E */
    uint16_t si, di;            /* +0F +11 */
    uint32_t data;              /* +13 */
} PACKED;

#define RS_ERROR 0x8000
#define RS_DONE  0x0100
#define DE_BADCMD  3
#define DE_NOTPREP 7            /* "code page not prepared" (unknown media) */
#define DE_KEYB    8            /* the keyboard refused the code page */
#define DE_DEVERR  10           /* write fault: device error */
#define DE_GENFAIL 12

int int21(struct armregs *r);
int int2f(struct armregs *r);

#define MAX_SLOTS 12
#define NOCP 0xFFFF

/* one prepared code page: the 8x16 and 8x8 fonts (14-line text uses rows 1-14
   of the 8x16 glyphs, as the ARM-PC BIOS makes its own 14-line font) */
struct slot {
    uint16_t cp;                /* NOCP = empty */
    uint8_t  ok;                /* both fonts loaded */
    uint8_t  got;               /* bit 0: 16, bit 1: 8 */
    uint8_t  f16[256 * 16];
    uint8_t  f8[256 * 8];
};

extern struct devhdr display_header;
extern struct devhdr *disp_oldcon;
extern armdos_vect_t disp_old10, disp_old2f;
extern uint16_t disp_hwcp;              /* NOCP: none */
extern uint16_t disp_nslots;
extern uint16_t disp_nfonts;            /* the "m" of CON=(EGA,437,(n,m)) */
extern struct slot *disp_slots;
extern uint16_t disp_active;
void disp_int10(struct armregs *f);
void disp_int2f(struct armregs *f);
void disp_strategy(struct reqhdr *r);
void disp_interrupt(void);
void disp_publish(void);
extern char display_res_end[];
void disp_init(struct req_init *q);

#define IVT_SLOT(n) (((volatile uint32_t *)0)[n])
#define BDA8(o)  (*(volatile uint8_t  *)(0x400 + (o)))
#define BDA16(o) (*(volatile uint16_t *)(0x400 + (o)))
#define FONTRAM  ((volatile uint8_t *)0x11000000)

#endif
