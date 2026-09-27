/*
 * dos.h - Microsoft C compatible DOS interface for ARM-DOS.
 *
 * Differences from 16-bit MS C (see sdk/README.md):
 *  - REGS fields are 32 bits (x.ax is the whole r0). The byte registers are
 *    laid out so h.al/h.ah are bytes 0/1 of x.ax, h.bl/h.bh of x.bx, etc.
 *  - Pointers are flat. Where x86 code passes a pointer in DS:DX, put the
 *    pointer in x.dx (the segment is ignored). SREGS exists; .ds and .es are
 *    only used for calls that take a segment VALUE (r7 = DS, r8 = ES), e.g.
 *    AH=49h/4Ah take the block segment in ES.
 *  - far/near/huge/interrupt are empty. An interrupt handler is a normal C
 *    function taking the register frame:  void handler(struct armregs *f).
 */
#ifndef _ARMDOS_DOS_H
#define _ARMDOS_DOS_H

#include <stddef.h>
#include <stdint.h>
#include "armdos.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- keywords from the segmented world ------------------------------------ */
#ifndef far
#define far
#endif
#ifndef near
#define near
#endif
#ifndef huge
#define huge
#endif
#define _far
#define _near
#define _huge
#define __far
#define __near
#define __huge
#define interrupt
#define _interrupt
#define __interrupt
#define _cdecl
#define __cdecl
#define cdecl
#define _pascal
#define pascal
#define _loadds
#define _saveregs

/* ---- registers --------------------------------------------------------- */
struct WORDREGS {
    unsigned int ax, bx, cx, dx, si, di;
    unsigned int cflag;     /* nonzero if carry was set */
    unsigned int flags;     /* the whole CPSR after the call */
};
struct BYTEREGS {
    unsigned char al, ah, _ax2, _ax3;
    unsigned char bl, bh, _bx2, _bx3;
    unsigned char cl, ch, _cx2, _cx3;
    unsigned char dl, dh, _dx2, _dx3;
};
union REGS {
    struct WORDREGS x;
    struct WORDREGS w;      /* some compilers call it w */
    struct BYTEREGS h;
};
struct SREGS {
    unsigned int es, cs, ss, ds;    /* es -> r8, ds -> r7; cs/ss unused */
};

int int86(int intno, union REGS *in, union REGS *out);
int int86x(int intno, union REGS *in, union REGS *out, struct SREGS *seg);
int intdos(union REGS *in, union REGS *out);
int intdosx(union REGS *in, union REGS *out, struct SREGS *seg);
int bdos(int func, unsigned int dx, unsigned int al);
void segread(struct SREGS *seg);    /* all zero: there are no segments */

/* ---- far pointers ---------------------------------------------------------- */
#define MK_FP(seg, off) ((void *)(((uint32_t)(uint16_t)(seg) << 4) + (uint32_t)(off)))
#define FP_SEG(fp)      ((unsigned)(((uint32_t)(fp)) >> 4))
#define FP_OFF(fp)      ((unsigned)(((uint32_t)(fp)) & 0xFu))
#define _MK_FP MK_FP
#define _FP_SEG FP_SEG
#define _FP_OFF FP_OFF

#define peek(seg, off)       (*(volatile unsigned short *)MK_FP(seg, off))
#define peekb(seg, off)      (*(volatile unsigned char *)MK_FP(seg, off))
#define poke(seg, off, v)    (*(volatile unsigned short *)MK_FP(seg, off) = (unsigned short)(v))
#define pokeb(seg, off, v)   (*(volatile unsigned char *)MK_FP(seg, off) = (unsigned char)(v))

/* ---- ports and interrupts ------------------------------------------------ */
static inline int inp(unsigned port) { return armdos_inb(port); }
static inline unsigned inpw(unsigned port) { return armdos_inw(port); }
static inline int outp(unsigned port, int v) { armdos_outb(port, (uint8_t)v); return v; }
static inline unsigned outpw(unsigned port, unsigned v) { armdos_outw(port, (uint16_t)v); return v; }
#define inportb(p)      inp(p)
#define outportb(p, v)  outp(p, v)
#define inport(p)       inpw(p)
#define outport(p, v)   outpw(p, v)
#define _inp inp
#define _inpw inpw
#define _outp outp
#define _outpw outpw

static inline void _enable(void)  { armdos_enable(); }
static inline void _disable(void) { armdos_disable(); }
#define enable()  _enable()
#define disable() _disable()

/* Vectors: MS C's "void (interrupt far *)()" is a frame-taking function. */
typedef armdos_vect_t _dos_vect_t;
armdos_vect_t _dos_getvect(unsigned intno);
void _dos_setvect(unsigned intno, armdos_vect_t handler);
/* _chain_intr: call the old handler with the same frame (it returns here). */
static inline void _chain_intr(armdos_vect_t old, struct armregs *f) { armdos_callold(old, f); }
#define getvect(n)     _dos_getvect(n)
#define setvect(n, h)  _dos_setvect(n, h)

/* ---- DOS globals (set by the start-up code) ------------------------------ */
extern unsigned int  _psp;          /* PSP segment */
extern unsigned char _osmajor, _osminor;
extern unsigned int  _osversion;

/* ---- _dos_ functions (return 0 or the DOS error code) --------------------- */
#define _A_NORMAL 0x00
#define _A_RDONLY 0x01
#define _A_HIDDEN 0x02
#define _A_SYSTEM 0x04
#define _A_VOLID  0x08
#define _A_SUBDIR 0x10
#define _A_ARCH   0x20
#define FA_RDONLY _A_RDONLY
#define FA_HIDDEN _A_HIDDEN
#define FA_SYSTEM _A_SYSTEM
#define FA_LABEL  _A_VOLID
#define FA_DIREC  _A_SUBDIR
#define FA_ARCH   _A_ARCH

struct find_t {                     /* exactly the DOS DTA */
    char reserved[21];
    char attrib;
    unsigned short wr_time;
    unsigned short wr_date;
    unsigned long size;
    char name[13];
} __attribute__((packed));

struct dosdate_t {
    unsigned char day;      /* 1-31 */
    unsigned char month;    /* 1-12 */
    unsigned short year;    /* 1980-2099 */
    unsigned char dayofweek;/* 0 = Sunday */
};
struct dostime_t {
    unsigned char hour, minute, second, hsecond;
};
struct diskfree_t {
    unsigned total_clusters, avail_clusters, sectors_per_cluster, bytes_per_sector;
};

unsigned _dos_findfirst(const char *path, unsigned attrib, struct find_t *f);
unsigned _dos_findnext(struct find_t *f);
void     _dos_getdate(struct dosdate_t *d);
unsigned _dos_setdate(struct dosdate_t *d);
void     _dos_gettime(struct dostime_t *t);
unsigned _dos_settime(struct dostime_t *t);
void     _dos_getdrive(unsigned *drive);             /* 1 = A: */
void     _dos_setdrive(unsigned drive, unsigned *ndrives);
unsigned _dos_getdiskfree(unsigned drive, struct diskfree_t *df);
unsigned _dos_open(const char *path, unsigned mode, int *handle);
unsigned _dos_creat(const char *path, unsigned attrib, int *handle);
unsigned _dos_creatnew(const char *path, unsigned attrib, int *handle);
unsigned _dos_close(int handle);
unsigned _dos_read(int handle, void *buf, unsigned count, unsigned *nread);
unsigned _dos_write(int handle, const void *buf, unsigned count, unsigned *nwritten);
unsigned _dos_getfileattr(const char *path, unsigned *attrib);
unsigned _dos_setfileattr(const char *path, unsigned attrib);
unsigned _dos_getftime(int handle, unsigned *date, unsigned *time);
unsigned _dos_setftime(int handle, unsigned date, unsigned time);
void     _dos_keep(unsigned status, unsigned paragraphs);
unsigned _dos_allocmem(unsigned paragraphs, unsigned *seg);
unsigned _dos_freemem(unsigned seg);
unsigned _dos_setblock(unsigned paragraphs, unsigned seg, unsigned *maxsize);

/* Last-error info (INT 21h AH=59h). */
struct DOSERROR { int exterror; char eclass, action, locus; };
int dosexterr(struct DOSERROR *e);

#ifdef __cplusplus
}
#endif
#endif
