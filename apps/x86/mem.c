/*
 * mem.c - the x86 address space: page tables, the real BDA/video mappings,
 * slow-path stores (translated-code invalidation, page crossings).
 *
 * Copyright (c) 2026 Europa Micro Systems. Part of ARM-DOS (apps/x86).
 */
#include "x86.h"
#include "jit.h"

uint8_t *mem;
uintptr_t rpt[X86_PAGES];
uintptr_t wpt[X86_PAGES];
static uint8_t dummy_page[256] __attribute__((aligned(256)));

void mem_init(uint8_t *buf)
{
    mem = buf;
    for (uint32_t i = 0; i < X86_PAGES; i++) {
        uint32_t lin = i << 8;
        uintptr_t b;
        if (lin >= X86_MEMSIZE) b = (uintptr_t)dummy_page - lin;          /* beyond 10FFFFh: scratch */
        else if (lin >= 0xA0000 && lin < 0xC0000) b = 0;                  /* video RAM: the real one */
        else if (lin == 0x400) b = 0;                                     /* BIOS data area: the real one */
        else b = (uintptr_t)buf;
        rpt[i] = b;
        wpt[i] = b;
    }
}

/* A store the fast path refused: the page holds translated code (tag bit 0)
 * or the value crosses a page. */
uint32_t watch_lin = 0xFFFFFFFFu;
void mem_watch(uint32_t lin) { watch_lin = lin; wpt[lin >> 8] |= 1; }

void mem_slow_write(uint32_t lin, uint32_t v, int bytes)
{
    for (int k = 0; k < bytes; k++, v >>= 8) {
        uint32_t l = (lin + k) & X86_LINMASK;
        uintptr_t b = wpt[l >> 8];
        if (l == watch_lin) dbg("[WATCH %05lX <- %02lX at %04X:%04lX]\n", (unsigned long)l, (unsigned long)(v & 0xFF), cpu.sreg[SEG_CS], (unsigned long)cpu.prev_eip);
        if (b & 1) {
            b &= ~(uintptr_t)1;
            uint8_t *p = (uint8_t *)(b + l);
            if (*p != (uint8_t)v) { *p = v; jit_invalidate(l); }
            continue;
        }
        *(uint8_t *)(b + l) = v;
    }
}
