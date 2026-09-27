/*
 * armdos.h - ARM-DOS specific definitions (see ARCH.md).
 *
 * The register frame, the interrupt primitives, the PSP / MCB / EXE header
 * layouts, port I/O and a few extras. The Microsoft-C style headers (dos.h,
 * conio.h, bios.h, ...) are built on top of this one.
 */
#ifndef _ARMDOS_H
#define _ARMDOS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- the register frame (ARCH.md section 5) --------------------------------
 * Layout is ABI: the BIOS SVC/IRQ stubs build it, int86 and handlers use it. */
struct armregs {
    uint32_t r0;    /* AX */
    uint32_t r1;    /* BX */
    uint32_t r2;    /* CX */
    uint32_t r3;    /* DX */
    uint32_t r4;    /* SI */
    uint32_t r5;    /* DI */
    uint32_t r6;    /* BP */
    uint32_t r7;    /* "DS" when a segment VALUE is passed (ARCH.md 5.1) */
    uint32_t r8;    /* "ES" when a segment VALUE is passed (ARCH.md 5.1) */
    uint32_t r9, r10, r11, r12;
    uint32_t sp;    /* caller's r13 */
    uint32_t lr;    /* caller's r14 */
    uint32_t pc;    /* where the caller resumes (after the SVC) */
    uint32_t cpsr;  /* caller's CPSR: bit29 C = CF, bit30 Z = ZF */
    uint32_t intno; /* which INT this is */
};

#define ARMREGS_CPSR_OFFSET 64

#define ARM_CPSR_N  0x80000000u
#define ARM_CPSR_Z  0x40000000u   /* ZF */
#define ARM_CPSR_C  0x20000000u   /* CF */
#define ARM_CPSR_V  0x10000000u
#define ARM_CPSR_I  0x00000080u
#define ARM_CPSR_F  0x00000040u
#define ARM_CPSR_T  0x00000020u

/* Byte/word views of the x86-named registers inside a frame. */
#define AX(f)  ((f)->r0 & 0xFFFFu)
#define AL(f)  ((f)->r0 & 0xFFu)
#define AH(f)  (((f)->r0 >> 8) & 0xFFu)
#define BX(f)  ((f)->r1 & 0xFFFFu)
#define BL(f)  ((f)->r1 & 0xFFu)
#define BH(f)  (((f)->r1 >> 8) & 0xFFu)
#define CX(f)  ((f)->r2 & 0xFFFFu)
#define CL(f)  ((f)->r2 & 0xFFu)
#define CH(f)  (((f)->r2 >> 8) & 0xFFu)
#define DX(f)  ((f)->r3 & 0xFFFFu)
#define DL(f)  ((f)->r3 & 0xFFu)
#define DH(f)  (((f)->r3 >> 8) & 0xFFu)

#define ARMREGS_SET_CARRY(f)   ((f)->cpsr |= ARM_CPSR_C)
#define ARMREGS_CLEAR_CARRY(f) ((f)->cpsr &= ~ARM_CPSR_C)
#define ARMREGS_CARRY(f)       (((f)->cpsr & ARM_CPSR_C) != 0)
#define ARMREGS_ZERO(f)        (((f)->cpsr & ARM_CPSR_Z) != 0)

/* ---- raising interrupts -------------------------------------------------
 * All of these load r0-r8 from *r, execute SVC #n, store r0-r8 and the CPSR
 * (in r->cpsr) back, and return the carry flag (1 = CF set). r9-r12 are
 * neither passed nor returned. */
int _armdos_intr(int intno, struct armregs *r);   /* any n (256-entry svc table) */
int _armdos_int10(struct armregs *r);
int _armdos_int13(struct armregs *r);
int _armdos_int16(struct armregs *r);
int _armdos_int1a(struct armregs *r);
int _armdos_int21(struct armregs *r);
int _armdos_int2f(struct armregs *r);
int _armdos_int33(struct armregs *r);

/* Call a far "entry point" (e.g. the XMS driver): loads r0-r6 from *r, BLX
 * entry, stores r0-r6 back, returns r0. */
uint32_t _armdos_farcall(void *entry, struct armregs *r);

/* ---- interrupt vectors ----------------------------------------------------
 * A vector is a C function taking the frame. Install with _dos_setvect or
 * armdos_setvect (INT 21h AH=25h); chain with armdos_callold(). Handlers run
 * in SVC mode on the SVC stack; hardware IRQ handlers run with IRQs disabled
 * and must send EOI themselves (outp(0x20, 0x20)). */
typedef void (*armdos_vect_t)(struct armregs *f);

armdos_vect_t armdos_getvect(int intno);             /* INT 21h AH=35h */
void armdos_setvect(int intno, armdos_vect_t handler); /* INT 21h AH=25h */

/* Chain to a previous handler with the same frame. A null old vector behaves
 * like the BIOS does for an empty IVT slot: return with carry set. */
static inline void armdos_callold(armdos_vect_t old, struct armregs *f)
{
    if (old) old(f); else f->cpsr |= ARM_CPSR_C;
}

/* The IVT itself is at physical address 0. */
#define ARMDOS_IVT ((armdos_vect_t volatile *)0)

/* ---- PSP (ARCH.md section 9) ---------------------------------------------- */
struct psp {
    uint16_t int20;          /* 0x00 Thumb "svc #0x20" = 0xDF20 */
    uint16_t memtop;         /* 0x02 segment beyond the memory block */
    uint8_t  res04;          /* 0x04 */
    uint8_t  res05[5];       /* 0x05 */
    uint32_t int22;          /* 0x0A terminate address (flat) */
    uint32_t int23;          /* 0x0E Ctrl-C address */
    uint32_t int24;          /* 0x12 critical-error address */
    uint16_t parent;         /* 0x16 parent PSP segment */
    uint8_t  jft[20];        /* 0x18 job file table */
    uint16_t envseg;         /* 0x2C environment segment */
    uint32_t savedsp;        /* 0x2E */
    uint16_t jftsize;        /* 0x32 */
    uint32_t jftptr;         /* 0x34 flat */
    uint32_t prevpsp;        /* 0x38 */
    uint8_t  res3c[4];       /* 0x3C */
    uint16_t dosver;         /* 0x40 */
    uint8_t  res42[14];      /* 0x42 */
    uint8_t  call21[8];      /* 0x50 svc #0x21 ; bx lr */
    uint8_t  res58[4];       /* 0x58 */
    uint8_t  fcb1[16];       /* 0x5C */
    uint8_t  fcb2[20];       /* 0x6C */
    uint8_t  cmdtail[128];   /* 0x80 length, text, 0x0D; default DTA */
} __attribute__((packed));

/* ---- MCB (ARCH.md section 10) -------------------------------------------- */
struct mcb {
    char     type;           /* 'M' or 'Z' */
    uint16_t owner;          /* PSP segment, 0 free, 8 DOS */
    uint16_t size;           /* paragraphs, excluding the MCB */
    uint8_t  res[3];
    char     name[8];
} __attribute__((packed));

/* ---- EXE header (ARCH.md section 8) -------------------------------------- */
struct armexe {
    char     sig[4];         /* "AR1\0" */
    uint16_t hdrsize;        /* 64 */
    uint16_t flags;          /* ARMEXE_F_* */
    uint32_t image_off;
    uint32_t image_size;
    uint32_t bss_size;
    uint32_t stack_size;
    uint32_t entry;
    uint32_t reloc_off;
    uint32_t reloc_count;
    uint32_t min_extra;
    uint32_t max_extra;
    uint32_t reserved[5];
};
#define ARMEXE_F_THUMB  0x0001
#define ARMEXE_F_XMS    0x0002

/* ---- segments and pointers ------------------------------------------------ */
#define ARMDOS_SEG2PTR(seg)   ((void *)((uint32_t)(uint16_t)(seg) << 4))
#define ARMDOS_PTR2SEG(p)     ((uint16_t)((uint32_t)(p) >> 4))

/* ---- ISA I/O ports (ARCH.md section 3/4) --------------------------------- */
#define ARMDOS_IOBASE 0x10000000u
static inline uint8_t armdos_inb(unsigned port)
{ return *(volatile uint8_t *)(ARMDOS_IOBASE + (port & 0xFFFF)); }
static inline void armdos_outb(unsigned port, uint8_t v)
{ *(volatile uint8_t *)(ARMDOS_IOBASE + (port & 0xFFFF)) = v; }
/* 16-bit port access: the ATA data port supports a real halfword access;
 * elsewhere a word access is two byte accesses (port, port+1) as on the ISA
 * bus of a PC. */
static inline uint16_t armdos_inw(unsigned port)
{
    if ((port & 0xFFFF) == 0x1F0)
        return *(volatile uint16_t *)(ARMDOS_IOBASE + 0x1F0);
    return (uint16_t)(armdos_inb(port) | (armdos_inb(port + 1) << 8));
}
static inline void armdos_outw(unsigned port, uint16_t v)
{
    if ((port & 0xFFFF) == 0x1F0) {
        *(volatile uint16_t *)(ARMDOS_IOBASE + 0x1F0) = v;
        return;
    }
    armdos_outb(port, (uint8_t)v);
    armdos_outb(port + 1, (uint8_t)(v >> 8));
}

/* Log a string on the debug console (port E9h). */
static inline void armdos_debug(const char *s)
{ while (*s) armdos_outb(0xE9, (uint8_t)*s++); }

/* Exit the emulator (headless tests; ARM-PC system board port F4h). */
static inline void armdos_emu_exit(int code) { armdos_outb(0xF4, (uint8_t)code); }

/* ---- interrupts on/off (CLI/STI) ----------------------------------------- */
static inline void armdos_disable(void)
{
    uint32_t t;
    __asm__ volatile("mrs %0, cpsr\n\torr %0, %0, #0x80\n\tmsr cpsr_c, %0" : "=r"(t) :: "memory");
}
static inline void armdos_enable(void)
{
    uint32_t t;
    __asm__ volatile("mrs %0, cpsr\n\tbic %0, %0, #0x80\n\tmsr cpsr_c, %0" : "=r"(t) :: "memory");
}
/* Wait for interrupt (CP15 c7,c0,4): the idle instruction (like HLT). */
static inline void armdos_halt(void)
{
    __asm__ volatile("mcr p15, 0, %0, c7, c0, 4" :: "r"(0) : "memory");
}

/* ---- the BIOS data area ----------------------------------------------------- */
#define ARMDOS_BDA ((volatile uint8_t *)0x400)
#define ARMDOS_BIOS_TICKS (*(volatile uint32_t *)0x46C)
/* the text buffer of the current mode: B0000h in mode 7 (the Hercules/MDA card,
   emu/dev/hercules.mjs), else B8000h. Re-read it after a mode change. */
#define ARMDOS_TEXT_VRAM ((volatile uint16_t *)(ARMDOS_BDA[0x49] == 7 ? 0xB0000u : 0xB8000u))
#define ARMDOS_VGA_VRAM  ((volatile uint8_t *)0xA0000)
/* 1 if a VGA is there (INT 10h AX=1A00h answers AL=1Ah with a VGA display code,
   BL = 7/8); 0 on the Hercules/MDA card option, whose BIOS has no AH=1Ah. */
static inline int armdos_vga_present(void)
{
    struct armregs r = { 0 };
    r.r0 = 0x1A00;
    _armdos_int10(&r);
    return (r.r0 & 0xFF) == 0x1A && ((r.r1 & 0xFF) == 7 || (r.r1 & 0xFF) == 8);
}

/* ---- C runtime state (set up by crt0) -------------------------------------- */
extern struct psp *_armdos_psp;       /* flat pointer to our PSP */
extern uint8_t    *_armdos_base;      /* load base (image offset 0) */
extern uint8_t    *_armdos_blockend;  /* end of our DOS memory block (exclusive) */
extern uint8_t    *_armdos_heap_start;/* where the heap started */
extern const char *_armdos_progpath;  /* argv[0] (full path from the environment) */

/* Heap tuning (define these in your program to override the weak defaults):
 *   unsigned _armdos_xms_kb = 0;      KB of XMS to grab when conventional
 *                                     memory runs out; 0 = the largest free
 *                                     block (default); ~0u = never use XMS.
 *   unsigned _armdos_heap_grow = 4096; granule for growing the DOS block. */
extern unsigned _armdos_xms_kb;
extern unsigned _armdos_heap_grow;
/*   unsigned _armdos_raw_extmem = 1;  with no XMS driver, continue the heap
 *                                     in raw extended memory (INT 15h AH=88h,
 *                                     from 1 MB up); default 0 = off. */
extern unsigned _armdos_raw_extmem;

/* XMS (ARCH.md section 10). Returns the XMS entry point, or 0 if no driver. */
void *armdos_xms_entry(void);
/* Bytes of heap currently obtained from XMS (0 if none). */
unsigned long armdos_xms_heap_size(void);

/* DOS error code (AX after a failed call) -> errno value. */
int _armdos_errno(unsigned doserr);
/* Set errno from a DOS error code and return -1. */
int _armdos_seterr(unsigned doserr);

/* Per-handle translation mode used by read()/write() (see io.h setmode). */
int _armdos_getmode(int fd);          /* O_TEXT or O_BINARY */

#ifdef __cplusplus
}
#endif
#endif /* _ARMDOS_H */
