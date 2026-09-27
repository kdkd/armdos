/* ansi.h - ANSI.SYS for ARM-DOS: shared declarations (resident part and INIT) */
#ifndef ANSI_H
#define ANSI_H

#include <stdint.h>
#include <armdos.h>

/* Everything the resident driver needs lives in sections that the SDK's link
   script collects with *(.text .text.*), in link order: ints.o, ansi.o, then
   mark.o (ansi_res_end), then init.o. INIT returns ansi_res_end as the break
   address, so only the resident part stays in memory (as DOS 4's CON$INIT
   returns its own address). Variables therefore go in .text.* sections too.
   (The flags are given as "ax": gas insists on it for .text.* sections, and
   the "@" turns the flags GCC appends into a comment. There is no memory
   protection: the variables are written all the same.) */
#define RES   __attribute__((section(".text.ansi_data,\"ax\",%progbits @")))
#define RESRO __attribute__((section(".text.ansi_rodata,\"ax\",%progbits @")))

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

struct req_rw {                 /* cmd 4 8 9 */
    struct reqhdr h;
    uint8_t  media;             /* +0D */
    uint32_t addr;              /* +0E */
    uint16_t count;             /* +12 */
} PACKED;

struct req_ndread {             /* cmd 5 */
    struct reqhdr h;
    uint8_t  ch;                /* +0D */
} PACKED;

struct req_gioctl {             /* cmd 19 */
    struct reqhdr h;
    uint8_t  category;          /* +0D */
    uint8_t  minor;             /* +0E */
    uint16_t si, di;            /* +0F +11 */
    uint32_t data;              /* +13 */
} PACKED;

#define RS_ERROR 0x8000
#define RS_BUSY  0x0200
#define RS_DONE  0x0100
#define DE_BADCMD 3

int int10(struct armregs *r);
int int16(struct armregs *r);
int int21(struct armregs *r);

/* resident state INIT sets up */
extern struct devhdr ansi_header;
extern struct devhdr *ansi_oldcon;
extern uint8_t ansi_switch_x, ansi_switch_l, ansi_switch_k, ansi_ext16;
extern armdos_vect_t ansi_old10, ansi_old2f;
void ansi_int10(struct armregs *f);
void ansi_int1b(struct armregs *f);
void ansi_int29(struct armregs *f);
void ansi_int2f(struct armregs *f);
void ansi_strategy(struct reqhdr *r);
void ansi_interrupt(void);
extern char ansi_res_end[];
void ansi_init(struct req_init *q);

#define IVT_SLOT(n) (((volatile uint32_t *)0)[n])
#define BDA8(o)  (*(volatile uint8_t  *)(0x400 + (o)))
#define BDA16(o) (*(volatile uint16_t *)(0x400 + (o)))

#endif
