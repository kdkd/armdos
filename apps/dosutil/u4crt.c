/*
 * u4crt.c - a minimal start-up for the DOS 4 utilities, replacing the SDK's
 * (libdos/startup.c: argv, environ, malloc/XMS heap, stdio) which these
 * programs do not use - it keeps them near the size of the 8086 originals.
 * Like a .COM/.EXE of 1988 they shrink their memory block (INT 21h AH=4Ah)
 * to what they need and take more from DOS (AH=48h) if they want it.
 */
#include "u4.h"

struct psp *_armdos_psp;
uint8_t *u4_heap;
__attribute__((weak)) unsigned u4_heap_bytes = 0;
extern int main(void);

void __armdos_start(struct psp *psp, uint8_t *base, uint8_t *blockend, uint8_t *stacktop)
{
    (void)base;
    _armdos_psp = psp;
    u4_heap = (uint8_t *)(((uint32_t)stacktop + 15) & ~15u);
    uint32_t keep = (uint32_t)(u4_heap - (uint8_t *)psp) + ((u4_heap_bytes + 15) & ~15u);
    if ((uint8_t *)psp + keep < blockend) {
        struct armregs r;
        u4_clr(&r);
        r.r0 = 0x4A00;
        r.r1 = keep >> 4;
        r.r8 = (uint32_t)psp >> 4;
        u4_int21(&r);
    }
    u4_exit(main());
}
