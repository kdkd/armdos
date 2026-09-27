/*
 * run.c - G, T and P: running the program under DEBUG's control.
 *
 * The ARM926 has no single-step flag, so breakpoints are BKPT instructions
 * planted in the program's code: BKPT raises a prefetch abort, which the BIOS
 * turns into INT 0Eh (bios/start.S), which DEBUG hooks.  T (trace) works out
 * where the next instruction will take the program - it evaluates the
 * condition codes and computes branch targets, loads into pc, LDM with pc,
 * BX/BLX (with the ARM/Thumb switch), data processing into pc, Thumb POP {pc}
 * and friends - plants one temporary breakpoint there, runs, and removes it.
 * P does the same but steps over BL/BLX/SVC by stopping after them.
 *
 * The switch between DEBUG and the program is a frame swap in the INT 0Eh
 * handler; see trap.S.
 */
#include <string.h>
#include "dbg.h"

volatile int running;
static armdos_vect_t old0e;
static uint32_t dbg_cpsr;

#define BKPT_ARM   0xE1200D7Bu          /* bkpt #0xDB */
#define BKPT_THUMB 0xBEDBu              /* bkpt #0xDB */

static struct bp { uint32_t addr, orig; uint8_t thumb; } bps[24];
static int nbps;

/* the registers at a ^C inside a DOS call of the program (see int23 in debug.c) */
uint32_t cstop[NREGS];
volatile int ctrlc_stop;

int plant(uint32_t at)
{
    int t = at & 1;
    uint32_t a = at & ~1u;
    if ((!t && (a & 3)) || !ram_ok(a, t ? 2 : 4) || nbps == (int)(sizeof bps / sizeof bps[0])) return 0;
    struct bp *b = &bps[nbps++];
    b->addr = a;
    b->thumb = t;
    if (t) { b->orig = *(volatile uint16_t *)a; *(volatile uint16_t *)a = BKPT_THUMB; }
    else { b->orig = *(volatile uint32_t *)a; *(volatile uint32_t *)a = BKPT_ARM; }
    return 1;
}

static void unplant(struct bp *b)
{
    if (b->thumb) *(volatile uint16_t *)b->addr = b->orig;
    else *(volatile uint32_t *)b->addr = b->orig;
}

void unplant_all(void)
{
    while (nbps) unplant(&bps[--nbps]);
}

static int is_bkpt(uint32_t pc, int thumb)
{
    if (thumb) return ram_ok(pc, 2) && (*(volatile uint16_t *)pc & 0xFF00) == 0xBE00;
    return ram_ok(pc, 4) && !(pc & 3) && (*(volatile uint32_t *)pc & 0xFFF000F0u) == 0xE1200070u;
}

static void back_to_debug(struct armregs *f, uint32_t why)
{
    f->r0 = why;
    f->pc = (uint32_t)dbg_trap + 4;
    f->sp = dbg_ret_sp;
    f->cpsr = dbg_cpsr;
    running = 0;
}

/* INT 0Eh: prefetch abort = BKPT (or a jump into nowhere) */
static void pabt(struct armregs *f)
{
    uint32_t mode = f->cpsr & CPSR_MODE;
    int thumb = (f->cpsr & CPSR_T) != 0;
    if (mode != MODE_SVC && !thumb && f->pc == (uint32_t)dbg_trap) {
        /* DEBUG asks to run the program */
        dbg_cpsr = f->cpsr;
        memcpy(&f->r0, ureg, sizeof ureg);
        uint32_t m = f->cpsr & CPSR_MODE;
        if (m != MODE_USR && m != MODE_SYS) f->cpsr = (f->cpsr & ~CPSR_MODE) | MODE_SYS;
        running = 1;
        return;
    }
    if (running && mode != MODE_SVC && is_bkpt(f->pc, thumb)) {
        if (ctrlc_stop) { memcpy(ureg, cstop, sizeof ureg); ctrlc_stop = 0; }
        else memcpy(ureg, &f->r0, sizeof ureg);
        back_to_debug(f, STOP_BP);
        return;
    }
    if (mode == MODE_SVC) {
        /* a breakpoint of ours reached by an interrupt handler: drop it, retry */
        for (int i = nbps - 1; i >= 0; i--)
            if (bps[i].addr == f->pc) { unplant(&bps[i]); bps[i].addr = 0xFFFFFFFFu; return; }
    }
    if (old0e) armdos_callold(old0e, f);
}

void run_install(void)
{
    old0e = armdos_getvect(0x0E);
    armdos_setvect(0x0E, pabt);
}

void run_remove(void)
{
    armdos_setvect(0x0E, old0e);
}

/* run the program until it stops; the breakpoints are removed afterwards */
uint32_t go(void)
{
    set_psp(user_psp);
    uint32_t why = dbg_run();
    running = 0;
    ctrlc_stop = 0;
    unplant_all();
    struct armregs r = { 0 };
    r.r0 = 0x5000;
    r.r1 = my_psp;
    _armdos_int21(&r);
    return why;
}

/* ------------------------------------------------ where does it go next */

static int cond_ok(uint32_t cond, uint32_t cpsr)
{
    int n = (cpsr >> 31) & 1, z = (cpsr >> 30) & 1, c = (cpsr >> 29) & 1, v = (cpsr >> 28) & 1;
    switch (cond) {
    case 0: return z;           case 1: return !z;
    case 2: return c;           case 3: return !c;
    case 4: return n;           case 5: return !n;
    case 6: return v;           case 7: return !v;
    case 8: return c && !z;     case 9: return !c || z;
    case 10: return n == v;     case 11: return n != v;
    case 12: return !z && n == v; case 13: return z || n != v;
    default: return 1;
    }
}

static uint32_t rv(int n, uint32_t pcval) { return n == 15 ? pcval : ureg[n]; }

static uint32_t shifted(uint32_t val, int type, uint32_t amt, int byreg, uint32_t c)
{
    if (byreg) {
        amt &= 0xFF;
        if (!amt) return val;
        switch (type) {
        case 0: return amt >= 32 ? 0 : val << amt;
        case 1: return amt >= 32 ? 0 : val >> amt;
        case 2: return amt >= 32 ? (uint32_t)((int32_t)val >> 31) : (uint32_t)((int32_t)val >> amt);
        default: amt &= 31; return amt ? (val >> amt) | (val << (32 - amt)) : val;
        }
    }
    switch (type) {
    case 0: return val << amt;
    case 1: return amt ? val >> amt : 0;
    case 2: return (uint32_t)((int32_t)val >> (amt ? amt : 31));
    default: return amt ? (val >> amt) | (val << (32 - amt)) : (c << 31) | (val >> 1);
    }
}

static int popcount(uint32_t v) { int n = 0; while (v) { n += v & 1; v >>= 1; } return n; }

static int next_arm(uint32_t pc, uint32_t *npc)
{
    uint32_t w = peek32(pc), cpsr = ureg[R_CPSR], cond = w >> 28, pc8 = pc + 8;
    uint32_t c = (cpsr >> 29) & 1;
    int rd = (w >> 12) & 15, rn = (w >> 16) & 15, rm = w & 15;
    *npc = pc + 4;
    if (cond == 15) {
        if ((w & 0xFE000000u) == 0xFA000000u) {
            *npc = (pc8 + (((int32_t)(w << 8)) >> 6) + ((w >> 23) & 2)) | 1;
        }
        return 1;
    }
    if (!cond_ok(cond, cpsr)) return 1;
    if ((w & 0x0FFFFFC0u) == 0x012FFF00u && (w & 0x30)) {       /* bx / bxj / blx rm */
        *npc = rv(rm, pc8);
        if (!(*npc & 1)) *npc &= ~3u;
        return 1;
    }
    if ((w & 0x0E000000u) == 0x0A000000u) {                      /* b / bl */
        *npc = pc8 + (((int32_t)(w << 8)) >> 6);
        return 1;
    }
    if ((w & 0x0E108000u) == 0x08108000u) {                      /* ldm with pc */
        uint32_t base = rv(rn, pc8), n = popcount(w & 0xFFFF), a;
        switch ((w >> 23) & 3) {
        case 0: a = base; break;                                /* da */
        case 1: a = base + 4 * (n - 1); break;                  /* ia */
        case 2: a = base - 4; break;                            /* db */
        default: a = base + 4 * n; break;                       /* ib */
        }
        *npc = peek32(a);
        if (!(*npc & 1)) *npc &= ~3u;
        return 1;
    }
    if ((w & 0x0C500000u) == 0x04100000u && rd == 15 && (w & 0x02000010u) != 0x02000010u) {  /* ldr pc */
        uint32_t off;
        if (w & 0x02000000u) off = shifted(rv(rm, pc8), (w >> 5) & 3, (w >> 7) & 31, 0, c);
        else off = w & 0xFFF;
        uint32_t a = rv(rn, pc8);
        if (w & 0x01000000u) a = (w & 0x00800000u) ? a + off : a - off;
        *npc = peek32(a & ~3u);
        if (a & 3) { uint32_t r = (a & 3) * 8; *npc = (*npc >> r) | (*npc << (32 - r)); }
        if (!(*npc & 1)) *npc &= ~3u;
        return 1;
    }
    if ((w & 0x0C000000u) == 0 && rd == 15 && (w & 0x0E000090u) != 0x00000090u) {  /* data processing */
        int op = (w >> 21) & 15;
        if (op >= 8 && op <= 11) return 1;                      /* tst..cmn, mrs/msr, misc */
        uint32_t o2;
        if (w & 0x02000000u) {
            uint32_t rot = (w >> 7) & 30, imm = w & 0xFF;
            o2 = rot ? (imm >> rot) | (imm << (32 - rot)) : imm;
        } else if (w & 0x10) {
            o2 = shifted(rv(rm, pc + 12), (w >> 5) & 3, ureg[(w >> 8) & 15], 1, c);
        } else {
            o2 = shifted(rv(rm, pc8), (w >> 5) & 3, (w >> 7) & 31, 0, c);
        }
        uint32_t a = rv(rn, (w & 0x02000010u) == 0x10 ? pc + 12 : pc8), r;
        switch (op) {
        case 0: r = a & o2; break;          case 1: r = a ^ o2; break;
        case 2: r = a - o2; break;          case 3: r = o2 - a; break;
        case 4: r = a + o2; break;          case 5: r = a + o2 + c; break;
        case 6: r = a - o2 - !c; break;     case 7: r = o2 - a - !c; break;
        case 12: r = a | o2; break;         case 13: r = o2; break;
        case 14: r = a & ~o2; break;        default: r = ~o2; break;
        }
        *npc = r & ~3u;
        return 1;
    }
    return 1;
}

static int next_thumb(uint32_t pc, uint32_t *npc)
{
    uint32_t hw = peek16(pc), cpsr = ureg[R_CPSR], pc4 = pc + 4;
    *npc = (pc + 2) | 1;
    if ((hw & 0xF800) == 0xF000) {
        uint32_t h2 = peek16(pc + 2);
        int32_t off = (((int32_t)((hw & 0x7FF) << 21)) >> 9) + ((h2 & 0x7FF) << 1);
        if ((h2 & 0xF800) == 0xF800) *npc = (pc4 + off) | 1;
        else if ((h2 & 0xF801) == 0xE800) *npc = (pc4 + off) & ~3u;
        return 1;
    }
    if ((hw & 0xF000) == 0xD000) {
        uint32_t cond = (hw >> 8) & 15;
        if (cond < 14 && cond_ok(cond, cpsr)) *npc = (pc4 + (((int32_t)((hw & 0xFF) << 24)) >> 23)) | 1;
        return 1;
    }
    if ((hw & 0xF800) == 0xE000) { *npc = (pc4 + (((int32_t)((hw & 0x7FF) << 21)) >> 20)) | 1; return 1; }
    if ((hw & 0xFF00) == 0x4700) {                              /* bx / blx rm */
        int rm = (hw >> 3) & 15;
        *npc = rm == 15 ? pc4 & ~3u : ureg[rm];
        if (!(*npc & 1)) *npc &= ~3u;
        return 1;
    }
    if ((hw & 0xFC00) == 0x4400 && ((hw & 7) | ((hw >> 4) & 8)) == 15 && ((hw >> 8) & 3) != 1) {
        int rm = (hw >> 3) & 15;
        uint32_t v = rm == 15 ? pc4 : ureg[rm];
        *npc = ((((hw >> 8) & 3) == 0 ? pc4 + v : v) & ~1u) | 1;
        return 1;
    }
    if ((hw & 0xFF00) == 0xBD00) {                              /* pop {.., pc} */
        *npc = peek32(ureg[R_SP] + 4 * popcount(hw & 0xFF));
        if (!(*npc & 1)) *npc &= ~3u;
        return 1;
    }
    return 1;
}

/* the address (| 1 for Thumb state) of the instruction after the one at pc */
int next_pc(uint32_t *npc)
{
    uint32_t pc = ureg[R_PC];
    if (ureg[R_CPSR] & CPSR_T) return next_thumb(pc & ~1u, npc);
    return next_arm(pc & ~3u, npc);
}

int insn_size(uint32_t pc, int thumb)
{
    if (!thumb) return 4;
    return (peek16(pc) & 0xF800) == 0xF000 && (peek16(pc + 2) & 0xE800) == 0xE800 ? 4 : 2;
}

/* BL, BLX, SVC: P stops after them */
int is_call(uint32_t pc, int thumb, uint32_t *after)
{
    if (thumb) {
        uint32_t hw = peek16(pc);
        int sz = insn_size(pc, 1);
        *after = (pc + sz) | 1;
        return sz == 4 || (hw & 0xFF87) == 0x4780 || (hw & 0xFF00) == 0xDF00;
    }
    uint32_t w = peek32(pc);
    *after = pc + 4;
    if ((w >> 28) == 15) return (w & 0xFE000000u) == 0xFA000000u;
    return (w & 0x0F000000u) == 0x0B000000u || (w & 0x0FFFFFF0u) == 0x012FFF30u || (w & 0x0F000000u) == 0x0F000000u;
}
