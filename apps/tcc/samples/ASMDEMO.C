/* ASMDEMO.C - inline ARM assembler.
 *
 * TCC understands GCC-style __asm__ statements with ARM instructions,
 * so C code can reach the processor directly: CP15 registers, the
 * program status register, instructions C has no operator for - and
 * the operating system itself, since "INT 21h" is just "svc #0x21".
 */
#include <stdio.h>

/* CP15 register 0: the processor's main ID register */
static unsigned cpu_id(void)
{
    unsigned id;
    __asm__ volatile ("mrc p15, 0, %0, c0, c0, 0" : "=r" (id));
    return id;
}

/* the current program status register: mode bits, IRQ mask, flags */
static unsigned cpsr(void)
{
    unsigned psr;
    __asm__ volatile ("mrs %0, cpsr" : "=r" (psr));
    return psr;
}

/* count leading zeros: one ARMv5 instruction */
static unsigned clz(unsigned x)
{
    unsigned n;
    __asm__ ("clz %0, %1" : "=r" (n) : "r" (x));
    return n;
}

/* 32 x 32 -> 64 bit multiply in one instruction */
static unsigned long long mul64(unsigned a, unsigned b)
{
    unsigned lo, hi;
    __asm__ ("umull %0, %1, %2, %3" : "=&r" (lo), "=&r" (hi) : "r" (a), "r" (b));
    return ((unsigned long long)hi << 32) | lo;
}

/* INT 21h, AH=09h: print a '$'-terminated string (DS:DX -> r3) */
static void dos_print(const char *s)
{
    __asm__ volatile (
        "mov r3, %0\n\t"
        "mov r0, #0x0900\n\t"
        "svc #0x21"
        : : "r" (s) : "r0", "r1", "r2", "r3", "memory");
}

int main(void)
{
    unsigned id = cpu_id(), psr = cpsr();
    static const char *modes[] = { "USR", "FIQ", "IRQ", "SVC", "?", "?", "?", "ABT",
                                   "?", "?", "?", "UND", "?", "?", "?", "SYS" };

    printf("CPU ID register  : %08X  (implementer '%c', part %03X, revision %u)\n",
           id, (int)(id >> 24), (id >> 4) & 0xFFF, id & 15);
    printf("CPSR             : %08X  (%s mode, IRQs %s)\n",
           psr, modes[psr & 15], (psr & 0x80) ? "off" : "on");
    printf("clz(0x00F00000)  : %u\n", clz(0x00F00000));
    {   /* (newlib-nano's printf has no %llu: print the halves in hex) */
        unsigned long long p = mul64(0x87654321, 1000);
        printf("0x87654321 * 1000: 0x%X%08X\n", (unsigned)(p >> 32), (unsigned)p);
    }
    dos_print("And this line was printed by svc #0x21 with AH=09h.\r\n$");
    return 0;
}
