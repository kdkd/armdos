/*
 * ems.c - LIM EMS 4.0 expanded memory for the x86 program (INT 67h).
 *
 * A pool of 16 KB logical pages in the ARM PC's extended memory (taken from
 * ELBOW's heap when the program first allocates), a 64 KB page frame at
 * E000h (four physical pages) that the x86 page tables map onto the pool:
 * mapping a page only rewrites 64 page-table entries, so programs that map
 * in their interrupt handlers (music players mixing from EMS) pay almost
 * nothing.  The driver is visible the two usual ways: the device name
 * "EMMXXXX0" at offset 0Ah of the INT 67h vector's segment, and opening the
 * device EMMXXXX0 (dos.c opens NUL for it).
 *
 * Functions: 40h-4Eh, 50h (map multiple), 51h (reallocate), 53h/54h
 * (handle names, directory), 58h (mappable array), 59h (hardware info,
 * raw pages), 5Ah (allocate standard/raw).  Everything else says 84h.
 *
 * Copyright (c) 2026 Europa Micro Systems. Part of ARM-DOS (apps/x86).
 */
#include <stdlib.h>
#include "x86.h"
#include "dos86.h"
#include "jit.h"

#define EMS_PAGES    160                 /* 2.5 MB */
#define EMS_HANDLES  128
#define EMS_FRAME    0xE000u
#define PG           16384u

int ems_enabled = 1;
static uint8_t *pool;                    /* EMS_PAGES * 16 KB, allocated on first use */
static uint8_t owner[EMS_PAGES];         /* handle + 1 owning each pool page, 0 = free */
static struct {
    uint8_t used;
    uint16_t npages;
    uint8_t *pages[EMS_PAGES];           /* (indices into the pool, as pointers) */
    char name[8];
    int16_t saved[4]; uint8_t has_saved;
} h[EMS_HANDLES];
static int16_t map_h[4], map_p[4];        /* what each physical page shows (-1: nothing) */

void ems_init(void)
{
    /* the device header name the INT 67h detection looks for */
    uint8_t *p = hptr(LIN(HLE_SEG, 0x000A));
    memcpy(p, "EMMXXXX0", 8);
    for (int i = 0; i < 4; i++) { map_h[i] = -1; map_p[i] = 0; }
    h[0].used = 1;                       /* handle 0: the operating system's, no pages */
}

static int free_pages(void)
{
    int n = 0;
    for (int i = 0; i < EMS_PAGES; i++) if (!owner[i]) n++;
    return n;
}

static void set_phys(int phys, int hd, int lp)
{
    uint32_t base = LIN(EMS_FRAME, 0) + phys * PG;
    jit_invalidate_range(base, PG);
    uintptr_t b;
    if (hd < 0) { b = (uintptr_t)mem; map_h[phys] = -1; }
    else { b = (uintptr_t)h[hd].pages[lp] - base; map_h[phys] = hd; map_p[phys] = lp; }
    for (uint32_t pg = base >> 8; pg < (base + PG) >> 8; pg++) { rpt[pg] = b; wpt[pg] = b; }
}

static int valid(int hd) { return hd >= 0 && hd < EMS_HANDLES && h[hd].used; }

static int grow(int hd, int n)           /* give handle hd n pages in total */
{
    if (n > h[hd].npages) {
        if (!pool) {
            pool = malloc((size_t)EMS_PAGES * PG + 256);
            if (!pool) return 0x80;
            pool = (uint8_t *)(((uintptr_t)pool + 255) & ~(uintptr_t)255);
        }
        if (n - h[hd].npages > free_pages()) return n > EMS_PAGES ? 0x87 : 0x88;
        for (int i = 0; i < EMS_PAGES && h[hd].npages < n; i++)
            if (!owner[i]) { owner[i] = hd + 1; h[hd].pages[h[hd].npages++] = pool + (size_t)i * PG; }
    } else {
        while (h[hd].npages > n) {
            uint8_t *p = h[hd].pages[--h[hd].npages];
            owner[(p - pool) / PG] = 0;
            for (int k = 0; k < 4; k++) if (map_h[k] == hd && map_p[k] == h[hd].npages) set_phys(k, -1, 0);
        }
    }
    return 0;
}

static int alloc_handle(int n, uint16_t *out)
{
    for (int i = 1; i < EMS_HANDLES; i++)
        if (!h[i].used) {
            h[i].used = 1; h[i].npages = 0; memset(h[i].name, 0, 8); h[i].has_saved = 0;
            int e = grow(i, n);
            if (e) { h[i].used = 0; return e; }
            *out = i;
            return 0;
        }
    return 0x85;
}

static int map(int phys, int lp, int hd)
{
    if (phys < 0 || phys > 3) return 0x8B;
    if (!valid(hd)) return 0x83;
    if (lp == 0xFFFF) { set_phys(phys, -1, 0); return 0; }
    if (lp >= h[hd].npages) return 0x8A;
    set_phys(phys, hd, lp);
    return 0;
}

int ems_int67(void)
{
    uint32_t ah = rAH, al = rAL;
    int st = 0;
    if (!ems_enabled) { rAH = 0x84; return HLE_DONE; }
    switch (ah) {
    case 0x40: break;                                            /* status */
    case 0x41: rBX = EMS_FRAME; break;                           /* page frame */
    case 0x42: rBX = free_pages(); rDX = EMS_PAGES; break;       /* unallocated / total pages */
    case 0x43: {                                                 /* allocate */
        uint16_t hd;
        if (rBX == 0) { st = 0x89; break; }
        st = alloc_handle(rBX, &hd);
        if (!st) rDX = hd;
        break;
    }
    case 0x44: st = map(al, rBX, rDX); break;                    /* map / unmap */
    case 0x45: {                                                 /* deallocate */
        int hd = rDX;
        if (!valid(hd)) { st = 0x83; break; }
        if (h[hd].has_saved) { st = 0x86; break; }
        grow(hd, 0);
        if (hd) h[hd].used = 0;
        break;
    }
    case 0x46: rAL = 0x40; break;                                /* version 4.0 */
    case 0x47: {                                                 /* save page map */
        int hd = rDX;
        if (!valid(hd)) { st = 0x83; break; }
        if (h[hd].has_saved) { st = 0x8D; break; }
        for (int k = 0; k < 4; k++) h[hd].saved[k] = map_h[k] < 0 ? -1 : (map_h[k] << 8 | map_p[k]);
        h[hd].has_saved = 1;
        break;
    }
    case 0x48: {                                                 /* restore page map */
        int hd = rDX;
        if (!valid(hd)) { st = 0x83; break; }
        if (!h[hd].has_saved) { st = 0x8E; break; }
        for (int k = 0; k < 4; k++) { int v = h[hd].saved[k]; if (v < 0) set_phys(k, -1, 0); else set_phys(k, v >> 8, v & 0xFF); }
        h[hd].has_saved = 0;
        break;
    }
    case 0x4B: {                                                 /* handle count */
        int n = 0;
        for (int i = 0; i < EMS_HANDLES; i++) if (h[i].used) n++;
        rBX = n;
        break;
    }
    case 0x4C:                                                   /* pages of a handle */
        if (!valid(rDX)) st = 0x83; else rBX = h[rDX].npages;
        break;
    case 0x4D: {                                                 /* all handles' pages at ES:DI */
        int n = 0;
        uint32_t d = LIN(cpu.sreg[SEG_ES], rDI);
        for (int i = 0; i < EMS_HANDLES; i++)
            if (h[i].used) { wr16(d + n * 4, i); wr16(d + n * 4 + 2, h[i].npages); n++; }
        rBX = n;
        break;
    }
    case 0x4E: {                                                 /* get/set page map (the whole frame) */
        if (al == 3) { rAL = 16; break; }
        if (al == 0 || al == 2) {
            uint32_t d = LIN(cpu.sreg[SEG_ES], rDI);
            for (int k = 0; k < 4; k++) { wr16(d + k * 4, map_h[k] < 0 ? 0xFFFF : map_h[k]); wr16(d + k * 4 + 2, map_p[k]); }
        }
        if (al == 1 || al == 2) {
            uint32_t s = LIN(cpu.sreg[SEG_DS], rSI);
            for (int k = 0; k < 4; k++) {
                int hd = rd16(s + k * 4), lp = rd16(s + k * 4 + 2);
                if (hd == 0xFFFF || !valid(hd) || lp >= h[hd].npages) set_phys(k, -1, 0); else set_phys(k, hd, lp);
            }
        }
        if (al > 3) st = 0x8F;
        break;
    }
    case 0x50: {                                                 /* map multiple: CX pairs at DS:SI */
        uint32_t s = LIN(cpu.sreg[SEG_DS], rSI);
        for (unsigned i = 0; i < rCX && !st; i++) {
            int lp = rd16(s + i * 4), ph = rd16(s + i * 4 + 2);
            if (al == 1) ph = (ph - EMS_FRAME) / (PG >> 4);
            st = map(ph, lp, rDX);
        }
        break;
    }
    case 0x51: {                                                 /* reallocate */
        if (!valid(rDX)) { st = 0x83; break; }
        st = grow(rDX, rBX);
        rBX = h[rDX].npages;
        break;
    }
    case 0x53:                                                   /* handle name */
        if (!valid(rDX)) { st = 0x83; break; }
        if (al == 0) memcpy(hptr(LIN(cpu.sreg[SEG_ES], rDI)), h[rDX].name, 8);
        else if (al == 1) memcpy(h[rDX].name, hptr(LIN(cpu.sreg[SEG_DS], rSI)), 8);
        else st = 0x8F;
        break;
    case 0x54:
        if (al == 2) { rBX = EMS_HANDLES; break; }
        st = 0x8F;
        break;
    case 0x58:                                                   /* mappable physical address array */
        if (al == 0) {
            uint32_t d = LIN(cpu.sreg[SEG_ES], rDI);
            for (int k = 0; k < 4; k++) { wr16(d + k * 4, EMS_FRAME + k * (PG >> 4)); wr16(d + k * 4 + 2, k); }
        }
        if (al <= 1) rCX = 4; else st = 0x8F;
        break;
    case 0x59:
        if (al == 1) { rBX = free_pages(); rDX = EMS_PAGES; }
        else st = 0xA4;                                          /* hardware info: access denied (as under a VCPI host) */
        break;
    case 0x5A: {                                                 /* allocate (standard / raw), 0 pages allowed */
        uint16_t hd;
        st = alloc_handle(rBX, &hd);
        if (!st) rDX = hd;
        break;
    }
    default: st = 0x84;
    }
    if (st && opt_trace) dbg("  EMS AH=%02lX: error %02X\n", (unsigned long)ah, st);
    rAH = st;
    return HLE_DONE;
}
