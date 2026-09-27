/*
 * x86.h - X86.EXE, the ARM-DOS 8086 Compatibility Box: shared definitions.
 *
 * The emulated x86 lives in a 1 MB + 64 KB real-mode address space ("linear"
 * addresses 0 - 10FFEFh) held in a buffer from extended memory.  Two page
 * tables with 256-byte pages translate linear addresses to host (ARM)
 * addresses: host = rpt[lin >> 8] + lin.  Most pages point into the buffer;
 * the BIOS data area (400h-4FFh) and video memory (A0000h-BFFFFh) point at
 * the machine's real ones, so a program that pokes B800:0000 writes the real
 * text screen.  wpt[] is the same for stores, except that pages holding
 * translated code carry tag bit 0 (stores go the slow way and invalidate).
 */
#ifndef X86_H
#define X86_H

#include <stdint.h>
#include <string.h>

#define X86_MEMSIZE   0x110000u          /* 1 MB + 64 KB (HMA, A20 on) */
#define X86_PAGES     0x2000u            /* page tables cover 2 MB (32-bit offsets wrap into a dummy page) */
#define X86_LINMASK   0x1FFFFFu

enum { SEG_ES, SEG_CS, SEG_SS, SEG_DS, SEG_FS, SEG_GS };

#define F_CF 0x0001u
#define F_PF 0x0004u
#define F_AF 0x0010u
#define F_ZF 0x0040u
#define F_SF 0x0080u
#define F_TF 0x0100u
#define F_IF 0x0200u
#define F_DF 0x0400u
#define F_OF 0x0800u
#define F_ARITH (F_CF | F_PF | F_AF | F_ZF | F_SF | F_OF)

/* lazy flags: lf_op = kind | size << 4 (size 0 = byte, 1 = word, 2 = dword) */
enum { LF_NONE, LF_ADD, LF_ADC, LF_SUB, LF_SBB, LF_LOGIC, LF_INC, LF_DEC, LF_SHL, LF_SHR, LF_SAR };   /* shifts: a = value, b = count 1..size-1 */

typedef struct X86 {
    union { uint32_t e[8]; uint16_t w[16]; uint8_t b[32]; } r;   /* EAX ECX EDX EBX ESP EBP ESI EDI */
    uint32_t eip;
    uint32_t flags;              /* EFLAGS; F_ARITH bits stale while lf_op != LF_NONE */
    uint32_t lf_op, lf_a, lf_b, lf_res;
    uint32_t sbase[8];           /* segment bases (sreg << 4) */
    uint16_t sreg[8];
    uint32_t cr0;
    uint32_t prev_eip;           /* start of the instruction being executed */
    volatile uint32_t stop;      /* ask the run loop to return */
    uint64_t icount;             /* x86 instructions executed (interpreter) */
    uint32_t jit_icount;         /* x86 instructions executed in translated code (wraps; summed by jit.c) */
} X86;

extern X86 cpu;

/* register views */
#define rAX cpu.r.w[0]
#define rCX cpu.r.w[2]
#define rDX cpu.r.w[4]
#define rBX cpu.r.w[6]
#define rSP cpu.r.w[8]
#define rBP cpu.r.w[10]
#define rSI cpu.r.w[12]
#define rDI cpu.r.w[14]
#define rAL cpu.r.b[0]
#define rAH cpu.r.b[1]
#define rCL cpu.r.b[4]
#define rCH cpu.r.b[5]
#define rDL cpu.r.b[8]
#define rDH cpu.r.b[9]
#define rBL cpu.r.b[12]
#define rBH cpu.r.b[13]
#define rEAX cpu.r.e[0]
#define rECX cpu.r.e[1]
#define rEDX cpu.r.e[2]
#define rEBX cpu.r.e[3]
#define rESP cpu.r.e[4]
#define rEBP cpu.r.e[5]
#define rESI cpu.r.e[6]
#define rEDI cpu.r.e[7]

/* ---- memory ------------------------------------------------------------ */
extern uint8_t *mem;                     /* host address of linear 0 in the buffer */
extern uintptr_t rpt[X86_PAGES];         /* host = rpt[lin >> 8] + lin */
extern uintptr_t wpt[X86_PAGES];         /* same; bit 0 set = page holds translated code */

void mem_init(uint8_t *buf);
void mem_slow_write(uint32_t lin, uint32_t v, int bytes);   /* code pages, page crossings */

static inline uint8_t *hptr(uint32_t lin) { return (uint8_t *)(rpt[(lin & X86_LINMASK) >> 8] + (lin & X86_LINMASK)); }

static inline uint32_t rd8(uint32_t lin) { return *(uint8_t *)(rpt[lin >> 8] + lin); }
static inline uint32_t rd16(uint32_t lin)
{
    if ((lin & 0xFF) != 0xFF) { const uint8_t *p = (const uint8_t *)(rpt[lin >> 8] + lin); return p[0] | (p[1] << 8); }
    return rd8(lin) | (rd8((lin + 1) & X86_LINMASK) << 8);
}
static inline uint32_t rd32(uint32_t lin)
{
    if ((lin & 0xFF) <= 0xFC) {
        const uint8_t *p = (const uint8_t *)(rpt[lin >> 8] + lin);
        return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
    }
    return rd16(lin) | (rd16((lin + 2) & X86_LINMASK) << 16);
}
static inline void wr8(uint32_t lin, uint32_t v)
{
    uintptr_t b = wpt[lin >> 8];
    if (__builtin_expect(b & 1, 0)) { mem_slow_write(lin, v, 1); return; }
    *(uint8_t *)(b + lin) = v;
}
static inline void wr16(uint32_t lin, uint32_t v)
{
    uintptr_t b = wpt[lin >> 8];
    if (__builtin_expect((b & 1) || (lin & 0xFF) == 0xFF, 0)) { mem_slow_write(lin, v, 2); return; }
    uint8_t *p = (uint8_t *)(b + lin); p[0] = v; p[1] = v >> 8;
}
static inline void wr32(uint32_t lin, uint32_t v)
{
    uintptr_t b = wpt[lin >> 8];
    if (__builtin_expect((b & 1) || (lin & 0xFF) > 0xFC, 0)) { mem_slow_write(lin, v, 4); return; }
    uint8_t *p = (uint8_t *)(b + lin); p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24;
}

/* segment:offset helpers (real mode) */
#define LIN(seg, off) ((((uint32_t)(uint16_t)(seg)) << 4) + (uint16_t)(off))
static inline uint8_t *segptr(uint16_t seg, uint16_t off) { return hptr(LIN(seg, off)); }

/* ---- cpu.c ------------------------------------------------------------- */
void cpu_reset(void);
int  cpu_run(void);                     /* runs until cpu.stop; returns the stop reason */
uint32_t get_flags(void);               /* materialise EFLAGS */
void set_flags(uint32_t f);
void set_cf(int c);
void set_zf(int z);
void set_sreg(int s, uint16_t v);
void cpu_push16(uint32_t v);
uint32_t cpu_pop16(void);
void cpu_interrupt(int n, uint32_t ret_ip);   /* push FLAGS/CS/IP and vector through the x86 IVT */
void cpu_far_jump(uint16_t cs, uint32_t ip);
extern volatile uint32_t irq_pending;   /* bits: see irq.c */
extern uint32_t hle_stub_base;          /* linear address of the INT n stubs: F000:E000 */
#define HLE_SEG     0xF000u
#define HLE_OFF(n)  (0xE000u + (n) * 4u)
#define HLE_VEC(n)  ((HLE_SEG << 16) | HLE_OFF(n))
extern int jit_enabled;

/* ---- hle.c: services ----------------------------------------------------- */
enum { HLE_DONE, HLE_RETRY, HLE_SWITCH };   /* SWITCH: the handler changed the context (EXEC, exit) */
int  hle_int(int n);
void x86_terminate_cs(int code);                    /* native INT n; HLE_RETRY re-executes the INT */
void hle_special(int code);             /* 0F FE nn traps */
int  hle_hooked(int n);                 /* the x86 IVT entry n is not ours */
uint32_t io_in(uint32_t port, int size);
void io_out(uint32_t port, uint32_t v, int size);
void cpu_unsupported(const char *what);

/* ---- irq.c --------------------------------------------------------------- */
#define PEND_IRQ0   0x0001u
#define PEND_IRQ1   0x0002u
#define PEND_HWIRQ  0x00F0u      /* IRQ 3, 4, 5, 7 (serial ports, LPT/sound): bits 4-7 */
#define PEND_1C     0x10000u
#define PEND_1B     0x20000u
#define PEND_23     0x40000u
#define PEND_MOUSE  0x80000u
#define PEND_TF     0x40000000u
#define PEND_INHIBIT 0x20000000u
#define PEND_DEBUG  0x10000000u      /* /TRACE2, /RING: per-instruction hooks */
#define PEND_STOP   0x80000000u
/* irq_pending is also written by the ARM IRQ handlers (irq.c): the main
   thread must set bits with IRQs off, or an IRQ taken between its load and
   store is lost (its PIC level stays in service for ever) */
static inline void pend_set(uint32_t bit)
{
    uint32_t s, t;
    __asm__ volatile("mrs %0, cpsr\n\torr %1, %0, #0x80\n\tmsr cpsr_c, %1" : "=r"(s), "=r"(t) :: "memory");
    irq_pending |= bit;
    __asm__ volatile("msr cpsr_c, %0" :: "r"(s) : "memory");
}
void irq_install(void);
void irq_remove(void);
void irq_deliver(void);                  /* called by the run loop when irq_pending && IF */
void irq_flush_to_bios(void);            /* before a blocking native call */
extern volatile int x86_active;          /* the x86 CPU is running (not blocked in a native call) */
/* the ARM PC's "alt CPU" LED (system-board port 0xF5): 1 while x86 code runs,
   0 while ELBOW is in ARM code (DOS/BIOS services, IRQs, exit) */
extern volatile uint8_t alt_led;
void alt_led_set(int on);
void irq_default_08(void);
void irq_default_09(void);
void irq_default_hw(int vec);              /* INT 0Bh/0Ch/0Dh/0Fh chained to the original */
extern volatile int last_port60_read;    /* the x86 read port 60h since the last IRQ1 */

/* ---- misc ------------------------------------------------------------------ */
void x86_msg(const char *s);             /* to stderr (handle 2) */
void x86_fatal(const char *fmt, ...) __attribute__((noreturn));
void dbg(const char *fmt, ...);
extern int opt_trace;

#endif
