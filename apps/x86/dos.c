/*
 * dos.c - INT 21h for x86 programs, translated to ARM-DOS's INT 21h.
 *
 * The x86 registers go to r0-r6 (ARCH.md 5); wherever DOS takes a seg:off
 * pointer (DS:DX, DS:SI, ES:DI, ES:BX) the offset register is replaced by
 * the flat host address of that x86 location; CF (and ZF where DOS returns
 * it) come back into the x86 FLAGS.  Handles are ARM-DOS handles.  Memory
 * (48h-4Ah), vectors (25h/35h), the PSP (50h/51h/62h), EXEC and terminate
 * are the x86 world's own (proc.c).
 *
 * Copyright (c) 2026 Europa Micro Systems. Part of ARM-DOS (apps/x86).
 */
#include <strings.h>
#include "dos86.h"
#include "jit.h"

extern uint16_t x86_take_exit(void);
extern int x86_exec(const char *path, int al, uint32_t pblin);
extern void x86_create_psp(uint16_t seg, int child);
extern void x86_terminate_cs(int code);

#define FLAT(seg, off) ((uint32_t)hptr(LIN(seg, off)))
#define DS_ cpu.sreg[SEG_DS]
#define ES_ cpu.sreg[SEG_ES]

enum { P_BX = 1, P_CX = 2, P_DX = 4 };

static void w16(uint32_t lin, uint16_t v) { uint8_t *p = hptr(lin); p[0] = v; p[1] = v >> 8; }
static void w32(uint32_t lin, uint32_t v) { w16(lin, v); w16(lin + 2, v >> 16); }

void x86_note_hle_write(uint32_t lin, uint32_t len)
{
    if (jit_enabled && len) jit_invalidate_range(lin, len);
}

/* ^C seen during the call: the x86 INT 23h (default: end the program) */
static int handle_ctrlc(void)
{
    ctrlc_hit = 0;
    if (!hle_hooked(0x23)) { x86_terminate(0, 1); return HLE_SWITCH; }
    uint32_t v = rd32(0x23 * 4);
    uint16_t sp0 = rSP;
    X86 keep;
    get_flags();
    keep = cpu;
    if (x86_nested_call(v >> 16, v & 0xFFFF, 1) < 0) { x86_run_deferred(); return HLE_SWITCH; }
    int abort = 0;
    if (rSP == (uint16_t)(sp0 - 2)) { abort = get_flags() & F_CF; rSP += 2; }
    /* back at the INT 21h with the registers the handler left */
    uint64_t ic = cpu.icount;
    X86 now = cpu;
    cpu = keep;
    for (int i = 0; i < 8; i++) if (i != 4) cpu.r.e[i] = now.r.e[i];     /* (SP: as before) */
    set_sreg(SEG_DS, now.sreg[SEG_DS]);
    set_sreg(SEG_ES, now.sreg[SEG_ES]);
    cpu.icount = ic;
    if (abort) { x86_terminate(0, 1); return HLE_SWITCH; }
    return HLE_RETRY;
}

/* INT 24h hooked by the x86 program: called by the kernel through our ARM
 * INT 24h (irq.c) while one of our INT 21h calls is in progress */
uint32_t dos86_crit_x86(struct armregs *f)
{
    X86 keep;
    get_flags();
    keep = cpu;
    rAX = f->r0; rDI = f->r5;
    rBP = DOSDATA_SEG; rSI = 0;                      /* (a device header would be BP:SI) */
    uint32_t v = rd32(0x24 * 4);
    int r = x86_nested_call(v >> 16, v & 0xFFFF, 1);
    uint32_t al = rAL;
    uint64_t ic = cpu.icount;
    cpu = keep;
    cpu.icount = ic;
    if (r < 0) { crit_abort = 1; return 3; }
    if (al == 2) { crit_abort = 1; return 3; }
    return al;
}

static int stdin_is_console(void)
{
    struct armregs r;
    memset(&r, 0, sizeof r);
    r.r0 = 0x4400; r.r1 = 0;
    if (arm21(&r)) return 0;
    return (r.r3 & 0x81) == 0x81;
}
static int handle_is_device(uint16_t h)
{
    struct armregs r;
    memset(&r, 0, sizeof r);
    r.r0 = 0x4400; r.r1 = h;
    if (arm21(&r)) return 0;
    return (r.r3 & 0x80) != 0;
}
static int input_ready(void)
{
    struct armregs r;
    memset(&r, 0, sizeof r);
    r.r0 = 0x0B00;
    arm21(&r);
    if (ctrlc_hit) return 0;             /* (the caller raises INT 23h first) */
    return (r.r0 & 0xFF) != 0;
}

/* copy a table the ARM kernel points at (u16 length + data) into DOS data */
static uint32_t table_to_x86(uint32_t flat, uint16_t off, uint32_t max)
{
    const uint8_t *s = (const uint8_t *)flat;
    uint32_t n = 2 + (s[0] | (s[1] << 8));
    if (n > max) n = max;
    memcpy(hptr(LIN(DOSDATA_SEG, off)), s, n);
    return ((uint32_t)DOSDATA_SEG << 16) | off;
}

static void fix_casemap(uint8_t *info)
{
    uint32_t v = ((uint32_t)HLE_SEG << 16) | ROM_CASEMAP;
    memcpy(info + 0x12, &v, 4);
}

static void setup_psp_switch(uint16_t seg)
{
    /* 50h: the ARM side follows if seg is one of our processes */
    cur_psp = seg;
    for (struct xproc *p = cur_proc; p; p = p->parent) {
        if (p->psp == seg) {
            struct armregs r;
            memset(&r, 0, sizeof r);
            r.r0 = 0x5000;
            r.r1 = p->arm_psp;
            if (!p->arm_psp) { struct xproc *q = p; while (q->parent) q = q->parent; (void)q; r.r1 = 0; }
            if (r.r1) arm21(&r);
            break;
        }
    }
}

int dos_int21(void)
{
    struct armregs r;
    uint32_t ah = rAH, al = rAL;
    int ptr = 0, setcf = ah >= 0x2F, blocking = 0;
    uint32_t in4 = 0;
    x86_regs_to_arm(&r);

    switch (ah) {
    /* ---- the x86 world's own */
    case 0x00: x86_terminate_cs(0); return HLE_SWITCH;
    case 0x4C: x86_terminate(al, 0); return HLE_SWITCH;
    case 0x31: x86_keep(rDX, al); return HLE_SWITCH;
    case 0x25: w32(al * 4, ((uint32_t)DS_ << 16) | rDX); return HLE_DONE;
    case 0x35: { uint32_t v = rd32(al * 4); set_sreg(SEG_ES, v >> 16); rBX = v; return HLE_DONE; }
    case 0x1A: set_dta(DS_, rDX); return HLE_DONE;
    case 0x2F: set_sreg(SEG_ES, dta_far >> 16); rBX = dta_far; return HLE_DONE;
    case 0x34: set_sreg(SEG_ES, DOSDATA_SEG); rBX = DD_INDOS; return HLE_DONE;
    case 0x52: set_sreg(SEG_ES, DOSDATA_SEG); rBX = DD_LOL; return HLE_DONE;
    case 0x50: setup_psp_switch(rBX); return HLE_DONE;
    case 0x51: case 0x62: rBX = cur_psp; return HLE_DONE;
    case 0x26: x86_create_psp(rDX, 0); return HLE_DONE;
    case 0x55: x86_create_psp(rDX, 1); cur_psp = rDX; return HLE_DONE;
    case 0x4D: rAX = x86_take_exit(); return HLE_DONE;
    case 0x63: set_sreg(SEG_DS, DOSDATA_SEG); rSI = DD_DBCS; rAL = 0; set_cf(0); return HLE_DONE;
    case 0x48: {
        uint16_t seg, big;
        int e = arena_alloc(rBX, &seg, &big);
        if (e) { rAX = -e; rBX = big; set_cf(1); } else { rAX = seg; set_cf(0); }
        return HLE_DONE;
    }
    case 0x49: {
        int e = arena_free(ES_);
        if (e) { rAX = -e; set_cf(1); } else set_cf(0);
        return HLE_DONE;
    }
    case 0x4A: {
        uint16_t big = 0;
        int e = arena_resize(ES_, rBX, &big);
        if (e) { rAX = -e; rBX = big; set_cf(1); } else set_cf(0);
        return HLE_DONE;
    }
    case 0x58:
        if (al == 0) { rAX = arena_strategy; set_cf(0); }
        else if (al == 1) { arena_strategy = rBX & 0x43; set_cf(0); }
        else if (al == 2) { rAL = 0; set_cf(0); }
        else if (al == 3) { set_cf(0); }
        else { rAX = 1; set_cf(1); }
        return HLE_DONE;
    case 0x4B: {
        if (al != 0 && al != 1 && al != 3) { rAX = 1; set_cf(1); return HLE_DONE; }
        char path[128];
        const char *s = (const char *)hptr(LIN(DS_, rDX));
        int i = 0;
        for (; s[i] && i < 127; i++) path[i] = s[i];
        path[i] = 0;
        x86_active = 1;
        return x86_exec(path, al, LIN(ES_, rBX));
    }
    case 0x0C: {
        struct armregs f;
        memset(&f, 0, sizeof f);
        f.r0 = 0x0C00;
        arm21(&f);
        if (al == 0x01 || al == 0x06 || al == 0x07 || al == 0x08 || al == 0x0A) {
            rAH = al;
            int res = dos_int21();
            if (res == HLE_RETRY) rAX = 0x0C00 | al;
            else if (res == HLE_DONE && al != 0x06) { /* AH stays 0Ch in DOS */ }
            return res;
        }
        return HLE_DONE;
    }

    /* ---- console */
    case 0x01: case 0x07: case 0x08:
        if (stdin_is_console() && !input_ready()) {
            if (ctrlc_hit) return handle_ctrlc();
            if (!irq_pending) __asm__ volatile("mcr p15, 0, %0, c7, c0, 4" :: "r"(0) : "memory");
            return HLE_RETRY;
        }
        blocking = 1;
        break;
    case 0x0A: r.r3 = FLAT(DS_, rDX); ptr = P_DX; blocking = 1; break;
    case 0x09: r.r3 = FLAT(DS_, rDX); ptr = P_DX; break;
    case 0x02: case 0x03: case 0x04: case 0x05: case 0x06: case 0x0B:
    case 0x0D: case 0x0E: case 0x19: case 0x2A: case 0x2B: case 0x2C: case 0x2D: case 0x2E:
        break;

    /* ---- FCBs */
    case 0x0F: case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: case 0x15: case 0x16: case 0x17:
    case 0x21: case 0x22: case 0x23: case 0x24: case 0x27: case 0x28:
        r.r3 = FLAT(DS_, rDX); ptr = P_DX;
        break;
    case 0x29:
        r.r4 = in4 = FLAT(DS_, rSI);
        r.r5 = FLAT(ES_, rDI);
        break;

    /* ---- pointers in DS:DX */
    case 0x39: case 0x3A: case 0x3B: case 0x3C: case 0x3D: case 0x41: case 0x43: case 0x4E:
    case 0x5A: case 0x5B: case 0x69:
        r.r3 = FLAT(DS_, rDX); ptr = P_DX;
        if (ah == 0x3D) {
            /* the EMS driver's device (ems.c): "EMMXXXX0" opens as NUL */
            extern int ems_enabled;
            const char *n = (const char *)r.r3, *b = n;
            for (const char *q = n; *q; q++) if (*q == '\\' || *q == ':') b = q + 1;
            static const char nul[] = "NUL";
            if (ems_enabled && !strncasecmp(b, "EMMXXXX0", 8) && (!b[8] || b[8] == '.')) r.r3 = (uint32_t)nul;
        }
        break;
    case 0x3F:
        r.r3 = FLAT(DS_, rDX); ptr = P_DX;
        blocking = handle_is_device(rBX);
        break;
    case 0x40:
        r.r3 = FLAT(DS_, rDX); ptr = P_DX;
        break;
    case 0x56: r.r3 = FLAT(DS_, rDX); r.r5 = FLAT(ES_, rDI); ptr = P_DX; break;
    case 0x47: r.r4 = FLAT(DS_, rSI); break;
    case 0x60: r.r4 = FLAT(DS_, rSI); r.r5 = FLAT(ES_, rDI); break;
    case 0x6C: r.r4 = FLAT(DS_, rSI); break;
    case 0x38:
        if (rDX != 0xFFFF) { r.r3 = FLAT(DS_, rDX); ptr = P_DX; }
        break;
    case 0x44: {
        static uint8_t blk[64];
        if (al >= 2 && al <= 5) { r.r3 = FLAT(DS_, rDX); ptr = P_DX; }
        else if (al == 0x0C || al == 0x0D) {
            uint8_t *p = hptr(LIN(DS_, rDX));
            ptr = P_DX;
            r.r3 = (uint32_t)p;
            if (al == 0x0D && (rCL == 0x41 || rCL == 0x61)) {
                memcpy(blk, p, 13);
                uint32_t fp = blk[9] | (blk[10] << 8) | (blk[11] << 16) | ((uint32_t)blk[12] << 24);
                uint32_t fl = FLAT(fp >> 16, fp & 0xFFFF);
                memcpy(blk + 9, &fl, 4);
                r.r3 = (uint32_t)blk;
            }
        }
        break;
    }
    case 0x65: {
        if (al >= 0x20 && al <= 0x23) {
            if (al == 0x21 || al == 0x22) { r.r3 = FLAT(DS_, rDX); ptr = P_DX; }
        } else if (al >= 0xA0 && al <= 0xA2) {
            if (al != 0xA0) { r.r3 = FLAT(DS_, rDX); ptr = P_DX; }
        } else {
            uint8_t buf[64];
            uint32_t cx = rCX;
            if (cx > sizeof buf) cx = sizeof buf;
            r.r5 = (uint32_t)buf; r.r2 = cx;
            int cf = arm21(&r);
            if (ctrlc_hit) return handle_ctrlc();
            if (cf) { rAX = r.r0; set_cf(1); return HLE_DONE; }
            if (al == 1) { if (cx >= 3 + 0x16) fix_casemap(buf + 3); }
            else if (cx >= 5) {
                uint32_t flat;
                memcpy(&flat, buf + 1, 4);
                uint32_t fp;
                switch (al) {
                case 2: fp = table_to_x86(flat, DD_UPCASE, 130); break;
                case 4: fp = table_to_x86(flat, DD_FUPCASE, 130); break;
                case 5: fp = table_to_x86(flat, DD_FTERM, 32); break;
                case 6: fp = table_to_x86(flat, DD_COLLATE, 258); break;
                default: fp = table_to_x86(flat, DD_DBCS65, 16); break;
                }
                memcpy(buf + 1, &fp, 4);
            }
            memcpy(hptr(LIN(ES_, rDI)), buf, cx);
            rCX = r.r2;
            set_cf(0);
            return HLE_DONE;
        }
        break;
    }
    case 0x5D:
        if (al == 0x0A) { set_cf(0); return HLE_DONE; }
        if (al == 0x06) { rAX = 1; set_cf(1); return HLE_DONE; }
        break;
    default:
        break;
    }

    extern int trace_kb;
    if (trace_kb && (ah == 0x3F || ah == 0x0A || ah == 0x01 || ah == 0x07 || ah == 0x08)) {
        dbg("[21:%02X h%d at %04X:%04lX DS:77B=%02X stack:", ah, rBX, cpu.sreg[SEG_CS], (unsigned long)cpu.prev_eip, rd8(cpu.sbase[SEG_DS] + 0x77B));
        for (int i = 0; i < 12; i++) dbg(" %04X", rd16(cpu.sbase[SEG_SS] + ((rSP + i * 2) & 0xFFFF)));
        dbg("] ");
        extern int ring_on; extern void ring_dump(void);
        if (ring_on && ah == 0x3F) ring_dump();
    }
    if (blocking) { x86_active = 0; irq_flush_to_bios(); }
    int cf = arm21(&r);
    if (blocking) x86_active = 1;
    if (ctrlc_hit) return handle_ctrlc();
    if (crit_abort) { crit_abort = 0; x86_terminate(0, 2); return HLE_SWITCH; }

    /* results */
    rAX = r.r0;
    if (!(ptr & P_BX)) rBX = r.r1;
    if (!(ptr & P_CX)) rCX = r.r2;
    if (!(ptr & P_DX)) rDX = r.r3;
    if (setcf) set_cf(cf);
    switch (ah) {
    case 0x06: set_zf((r.cpsr & ARM_CPSR_Z) != 0); break;
    case 0x29: rSI += r.r4 - in4; break;
    case 0x1B: case 0x1C: {
        const uint8_t *m = (const uint8_t *)r.r1;
        *hptr(LIN(DOSDATA_SEG, DD_MEDIA)) = m ? *m : 0xF8;
        set_sreg(SEG_DS, DOSDATA_SEG); rBX = DD_MEDIA;
        break;
    }
    case 0x1F: case 0x32:
        if ((r.r0 & 0xFF) == 0 && r.r1) {
            uint8_t *d = hptr(LIN(DOSDATA_SEG, DD_DPB));
            memcpy(d, (const void *)r.r1, 0x21);
            uint32_t nul = ((uint32_t)DOSDATA_SEG << 16) | (DD_LOL + 0x22), last = 0xFFFFFFFFu;
            memcpy(d + 0x13, &nul, 4);
            memcpy(d + 0x19, &last, 4);
            set_sreg(SEG_DS, DOSDATA_SEG); rBX = DD_DPB;
        }
        break;
    case 0x38:
        if (!cf && rDX != 0xFFFF) fix_casemap(hptr(LIN(DS_, rDX)));
        break;
    case 0x3C: case 0x3D: case 0x3E: case 0x45: case 0x46: case 0x5A: case 0x5B: case 0x6C:
        if (!cf) jft_sync();
        break;
    case 0x3F:
        if (!cf) x86_note_hle_write(LIN(DS_, rDX), rAX);
        break;
    case 0x14: case 0x21: case 0x27: case 0x4E: case 0x4F: case 0x11: case 0x12:
        x86_note_hle_write(LIN(dta_far >> 16, dta_far & 0xFFFF), ah == 0x27 ? 65535 : 128);
        break;
    case 0x30:
        /* DOS 4.00 (the version the program sees) */
        break;
    }
    return HLE_DONE;
}
