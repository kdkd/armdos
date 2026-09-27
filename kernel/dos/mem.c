/*
 * mem.c - the memory arena (DOS/ALLOC.ASM): a chain of MCBs, first/best/
 * last fit, free blocks coalesced as they are scanned, and DOS's error
 * codes (7 arena trashed, 8 not enough memory with BX = largest, 9 bad block).
 */
#include "dos.h"

static int sig_ok(unsigned seg)
{
    char c = MCB(seg)->sig;
    return c == 'M' || c == 'Z';
}

static unsigned mcb_next(unsigned seg) { return seg + MCB(seg)->size + 1; }

/* merge the free blocks that follow `seg` into it; -1 if the arena is bad */
static int coalesce(unsigned seg)
{
    struct mcb *m = MCB(seg);
    while (m->sig != 'Z') {
        unsigned n = mcb_next(seg);
        if (!sig_ok(n)) return -1;
        struct mcb *x = MCB(n);
        if (x->owner != 0) return 0;
        m->size += x->size + 1;
        m->sig = x->sig;
    }
    return 0;
}

int mem_check(void)
{
    unsigned seg = LOL.first_mcb;
    if (!seg) return -1;
    for (int guard = 0; guard < 0x10000; guard++) {
        if (!sig_ok(seg)) return -1;
        if (MCB(seg)->sig == 'Z') return 0;
        seg = mcb_next(seg);
    }
    return -1;
}

/* split block seg (size >= paras) so that it is exactly paras long */
static void split(unsigned seg, unsigned paras)
{
    struct mcb *m = MCB(seg);
    if (m->size == paras) return;
    unsigned rest = m->size - paras - 1;
    unsigned n = seg + paras + 1;
    struct mcb *x = MCB(n);
    x->sig = m->sig;
    x->owner = 0;
    x->size = rest;
    memset(x->res, 0, 3);
    memset(x->name, 0, 8);
    m->sig = 'M';
    m->size = paras;
}

int mem_alloc(unsigned paras, unsigned *pseg, unsigned *plargest)
{
    unsigned seg = LOL.first_mcb, first = 0, best = 0, last = 0, largest = 0;
    if (!seg || !sig_ok(seg)) return -E_ARENA;
    for (;;) {
        struct mcb *m = MCB(seg);
        if (m->owner == 0) {
            if (coalesce(seg) < 0) return -E_ARENA;
            unsigned sz = m->size;
            if (sz > largest) largest = sz;
            if (paras <= sz) {
                if (!first) first = seg;
                if (!best || MCB(best)->size > sz) best = seg;
                last = seg;
            }
        }
        if (m->sig == 'Z') break;
        seg = mcb_next(seg);
        if (!sig_ok(seg)) return -E_ARENA;
    }
    if (!first) { if (plargest) *plargest = largest; return -E_NOMEM; }
    unsigned use;
    if (alloc_strategy > 1) {
        /* last fit: the top of the block */
        struct mcb *m = MCB(last);
        if (m->size == paras) use = last;
        else {
            unsigned lower = m->size - paras - 1;
            use = last + lower + 1;
            struct mcb *x = MCB(use);
            x->sig = m->sig;
            x->size = paras;
            memset(x->res, 0, 3);
            memset(x->name, 0, 8);
            m->sig = 'M';
            m->size = lower;
            m->owner = 0;
        }
    } else {
        use = alloc_strategy == 1 ? best : first;
        split(use, paras);
    }
    MCB(use)->owner = cur_psp;
    *pseg = use + 1;
    return 0;
}

int mem_free(unsigned seg)
{
    if (!seg || !sig_ok(seg - 1)) return -E_BADBLOCK;
    MCB(seg - 1)->owner = 0;
    return 0;
}

int mem_resize(unsigned seg, unsigned paras, unsigned *maxp)
{
    unsigned m = seg - 1;
    if (!seg || !sig_ok(m)) return -E_ARENA;
    if (coalesce(m) < 0) return -E_ARENA;
    struct mcb *b = MCB(m);
    if (paras > b->size) { *maxp = b->size; return -E_NOMEM; }
    split(m, paras);
    b->owner = cur_psp;
    return 0;
}

unsigned mem_largest(void)
{
    unsigned seg, largest = 0;
    if (mem_alloc(0xFFFF, &seg, &largest) == 0) { mem_free(seg); return 0xFFFF; }
    return largest;
}

void mem_free_owner(unsigned owner)
{
    unsigned seg = LOL.first_mcb;
    if (!seg) return;
    for (int guard = 0; guard < 0x10000; guard++) {
        if (!sig_ok(seg)) return;
        struct mcb *m = MCB(seg);
        if (m->owner == owner) m->owner = 0;
        if (m->sig == 'Z') return;
        seg = mcb_next(seg);
    }
}

void mem_set_owner(unsigned seg, unsigned owner)
{
    if (seg && sig_ok(seg - 1)) MCB(seg - 1)->owner = owner;
}

void mem_make_arena(uint16_t first, uint16_t sysparas, uint16_t end)
{
    struct mcb *m = MCB(first);
    m->sig = 'M';
    m->owner = MCB_OWNER_DOS;
    m->size = sysparas;
    memset(m->res, 0, 3);
    memset(m->name, 0, 8);
    unsigned f = first + sysparas + 1;
    struct mcb *x = MCB(f);
    x->sig = 'Z';
    x->owner = 0;
    x->size = end - f - 1;
    memset(x->res, 0, 3);
    memset(x->name, 0, 8);
}

/* SYSINIT is done with the top of memory: give it to the arena */
void mem_extend(uint16_t end)
{
    unsigned seg = LOL.first_mcb;
    while (MCB(seg)->sig != 'Z') { seg = mcb_next(seg); if (!sig_ok(seg)) return; }
    struct mcb *m = MCB(seg);
    unsigned top = seg + m->size + 1;
    if (top >= end) return;
    if (m->owner == 0) { m->size = end - seg - 1; return; }
    m->sig = 'M';
    struct mcb *x = MCB(top);
    x->sig = 'Z';
    x->owner = 0;
    x->size = end - top - 1;
    memset(x->res, 0, 3);
    memset(x->name, 0, 8);
}

void mem_functions(struct armregs *f)
{
    unsigned seg, largest = 0;
    int e;
    switch (AH(f)) {
    case 0x48:
        e = mem_alloc(BX(f), &seg, &largest);
        if (e == -E_NOMEM) { f->r1 = largest; sys_err(f, E_NOMEM); }
        else if (e < 0) sys_err(f, -e);
        else f->r0 = seg;
        break;
    case 0x49:
        e = mem_free(f->r8 & 0xFFFF);
        if (e < 0) sys_err(f, -e);
        break;
    case 0x4A:
        e = mem_resize(f->r8 & 0xFFFF, BX(f), &largest);
        if (e == -E_NOMEM) { f->r1 = largest; sys_err(f, E_NOMEM); }
        else if (e < 0) sys_err(f, -e);
        break;
    case 0x58:
        if (AL(f) == 0) f->r0 = alloc_strategy;
        else if (AL(f) == 1) alloc_strategy = BL(f);
        else { exterr.locus = 5; sys_err(f, E_INVFN); }
        break;
    }
}
