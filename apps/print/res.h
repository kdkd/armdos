/*
 * res.h - the interface between PRINT's resident part (res.c, resasm.S,
 * linked first and kept by INT 21h AH=31h) and its transient part (print.c).
 */
#ifndef PRINT_RES_H
#define PRINT_RES_H
#include <stdint.h>
#include "armdos.h"

#define MAXFILELEN      64
#define DEF_QUEUE       10
#define MIN_BUF         512
#define MAX_BUF         (16 * 1024)
#define MAX_QUEUE       32
#define WSTACK          2048            /* the worker's own stack (ISTACK) */
#define ERRCNT1         1500
#define ERRCNT2         20000
#define E_BUSY          9
#define E_QFULL         8
#define E_NAMELONG      12

/* ARM-DOS device header (ARCH.md 14, kernel/inc/kabi.h) */
struct devhdr {
    struct devhdr *next;
    uint16_t attr, pad;
    void (*strategy)(void *req);
    void (*interrupt)(void);
    char name[8];
};
#define DEVA_CHAR   0x8000
#define DEVA_OPCL   0x0800

typedef void (*vect_t)(struct armregs *);

/* resident data the transient sets up */
extern uint8_t r_slicecnt, r_timeslice, r_maxtick, r_busytick, r_queuelen;
extern uint16_t r_blksiz;
extern char r_listname[8];
extern struct devhdr *r_listdev;
extern uint8_t r_flag17_14;
extern uint16_t r_int17num, r_int14num;
extern char *r_filequeue, *r_endqueue, *r_queuetail;
extern uint8_t *r_buffer, *r_endptr, *r_nxtchr, *r_wstack;
extern volatile uint8_t *r_indos;
extern vect_t r_next1c, r_next28, r_next2f, r_next17, r_next14;
extern uint16_t r_my_psp;
extern char r_pchar;

/* the resident handlers */
void r_int1c(struct armregs *f);
void r_int28(struct armregs *f);
void r_int2f(struct armregs *f);
void r_int17(struct armregs *f);
void r_int14(struct armregs *f);
/* move the queue and buffer down to the end of the resident part and
 * terminate-and-stay-resident; never returns */
__attribute__((noreturn)) void r_keep(void);

extern char r_end[];                    /* resend.c: the end of the resident part */
#endif
