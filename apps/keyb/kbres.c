/*
 * kbres.c - KEYB.COM's resident part: the keyboard intercept, KEYB's INT 2Fh
 * interface and the state-logic interpreter.
 *
 * A re-creation in C of MS-DOS 4.00's KEYB (CMD/KEYB/KEYBI9.ASM
 * KEYB_STATE_PROCESSOR, KEYBI9C.ASM, KEYBI2F.ASM; Microsoft, MIT licence):
 * the same interpreter for the same KEYBOARD.SYS tables (IFF ANDF ELSEF ENDIFF
 * XLATT OPTION SET_FLAG PUT_ERROR_CHAR IFKBD GOTO BEEP RESET_NLS), the same
 * shadow flags (the BIOS's shift flags, EITHER_SHIFT/CTL/ALT and the dead-key
 * NLS flags), BUFFER_FILL, the Ctrl+Alt+F1/F2 hot keys and INT 2Fh AD80h-AD82h.
 *
 * ARM-PC difference: DOS 4's KEYB replaced the whole INT 9 handler (a copy of
 * the AT BIOS's with calls to the state processor). The ARM-PC BIOS calls INT
 * 15h AH=4Fh for every byte from the keyboard before it translates it, so KEYB
 * hooks that instead: the BIOS keeps the shift states, Pause, Ctrl-Break,
 * Alt+keypad and the US translation, and KEYB takes (returns CF clear for) the
 * make codes its tables translate, where KEYBI9C.ASM called the processor.
 * Nothing here may call the C library.
 */
#include "keyb.h"
#include <stddef.h>

struct keybsd keyb_sd RES = { .sig = { 'K', 'E', 'Y', 'B' } };

static uint8_t e0_seen RES, e1_left RES, beep_pending RES;

#define BDA(o)  (*(volatile uint8_t *)(0x400 + (o)))
#define BDAW(o) (*(volatile uint16_t *)(0x400 + (o)))
#define KB_FLAG   0x17
#define KB_FLAG_1 0x18
#define KB_FLAG_2 0x97
#define KB_FLAG_3 0x96

/* KB_FLAG */
#define RIGHT_SHIFT 0x01
#define LEFT_SHIFT  0x02
#define CTL_SHIFT   0x04
#define ALT_SHIFT   0x08
/* KB_FLAG_1 */
#define SYS_SHIFT   0x04
#define HOLD_STATE  0x08
/* KB_FLAG_3 */
#define R_CTL_SHIFT 0x04
#define R_ALT_SHIFT 0x08
/* EXT_KB_FLAG */
#define EITHER_SHIFT 0x80
#define EITHER_CTL   0x40
#define EITHER_ALT   0x20
#define SCAN_MATCH   0x08
/* translate tables */
#define ASCII_ONLY  0x80
#define TYPE_2_TAB  0x40
#define ZERO_SCAN   0x20
#define EXIT_IF_FOUND 0x80

static inline uint16_t rd16(const uint8_t *p) { return p[0] | (p[1] << 8); }

RESFN int keyb_cp_index(unsigned cp)
{
    switch (cp) {
    case 437: return 0;
    case 850: return 1;
    case 860: return 2;
    case 863: return 3;
    case 865: return 4;
    }
    return 7;
}

/* ARM-PC system board port F6h: the layout KEYB has active, for the web page's
   on-screen keyboard (ARCH.md 4.6) */
RESFN void keyb_publish(void)
{
    uint8_t v = 0;
    if (keyb_sd.country == 0xFF && keyb_sd.layout_id) v = keyb_sd.layout_id | (keyb_cp_index(keyb_sd.invoked_cp) << 5);
    armdos_outb(0xF6, v);
}

/* ------------------------------------------------------ BUFFER_FILL */

RESFN static void buffer_fill(uint16_t ax)
{
    if ((ax & 0xFF) == 0xFF || (ax >> 8) == 0xFF) return;   /* ignore / -1 pseudo scan */
    uint16_t tail = BDAW(0x1C), next = tail + 2;
    if (next >= BDAW(0x82)) next = BDAW(0x80);
    if (next == BDAW(0x1A)) { beep_pending = 1; return; }  /* buffer full */
    *(volatile uint16_t *)(0x400 + tail) = ax;
    BDAW(0x1C) = next;
}

/* ERROR_BEEP: a short tone on the speaker (PIT channel 2), timed by port 61h's
   15 us refresh toggle */
RESFN static void error_beep(void)
{
    armdos_outb(0x43, 0xB6);
    armdos_outb(0x42, 0x33); armdos_outb(0x42, 0x05);     /* 1193182 / 0x533 = 896 Hz */
    uint8_t old = armdos_inb(0x61);
    armdos_outb(0x61, old | 3);
    uint8_t t = armdos_inb(0x61) & 0x10;
    for (int n = 0; n < 5000; ) {                          /* 5000 x 15 us = 75 ms */
        uint8_t u = armdos_inb(0x61) & 0x10;
        if (u != t) { t = u; n++; }
    }
    armdos_outb(0x61, old & ~3);
}

/* ------------------------------------------------- the state processor */

/* the first state with this id in a translate section (states of other
   keyboard types were left out when the section was loaded) */
RESFN static const uint8_t *find_state(const uint8_t *sect, int id)
{
    if (!sect) return 0;
    const uint8_t *p = sect + 4;
    for (;;) {
        uint16_t len = rd16(p);
        if (!len) return 0;
        if (p[2] == id) return p;
        p += len;
    }
}

/* XLATT: search the state's tables; 1 and *out = buffer entry if found */
RESFN static int translate(const uint8_t *st, uint8_t scan, uint16_t *out)
{
    const uint8_t *t = st + 7;
    for (;;) {
        uint16_t len = rd16(t);
        if (!len) return 0;
        uint8_t opts = t[2];
        if (opts & TYPE_2_TAB) {
            unsigned n = t[3], sz = (opts & (ASCII_ONLY | ZERO_SCAN)) ? 2 : 3;
            const uint8_t *e = t + 4;
            for (unsigned i = 0; i < n; i++, e += sz) {
                if (e[0] != scan) continue;
                uint16_t ah = scan, al = e[1];
                if (!(opts & (ASCII_ONLY | ZERO_SCAN))) ah = e[2];
                if (opts & ZERO_SCAN) ah = 0;
                *out = (ah << 8) | al;
                return 1;
            }
        } else if (scan >= t[3] && scan <= t[4]) {
            unsigned i = scan - t[3];
            uint16_t v = (opts & (ASCII_ONLY | ZERO_SCAN)) ? (scan << 8) | t[5 + i] : rd16(t + 5 + 2 * i);
            if (opts & ZERO_SCAN) v &= 0xFF;
            *out = v;
            return 1;
        }
        t += len;
    }
}

/* KEYB_STATE_PROCESSOR: 1 = the key was handled (end of INT 9), 0 = continue
   with the US translation (the BIOS) */
RESFN static int state_processor(uint8_t scan)
{
    struct keybsd *sd = &keyb_sd;
    if (!sd->table_ok || sd->country != 0xFF || !sd->logic) return 0;
    uint8_t f[7];
    f[0] = BDA(KB_FLAG); f[1] = BDA(KB_FLAG_1); f[2] = BDA(KB_FLAG_2); f[3] = BDA(KB_FLAG_3);
    uint8_t ext = 0;
    if (f[0] & (RIGHT_SHIFT | LEFT_SHIFT)) ext |= EITHER_SHIFT;
    if ((f[0] & CTL_SHIFT) || (f[3] & R_CTL_SHIFT)) ext |= EITHER_CTL;
    if ((f[0] & ALT_SHIFT) || (f[3] & R_ALT_SHIFT)) ext |= EITHER_ALT;
    f[4] = ext;
    f[5] = sd->nls1; f[6] = sd->nls2;

    const uint8_t *L = sd->logic;
    unsigned si = 4, nest = 0, level = 0;
    int take_else = 0, result = 1;
    uint8_t option = 0;
    for (int guard = 0; guard < 20000; guard++) {
        uint8_t op = L[si], cmd = op >> 4;
        switch (cmd) {
        case 0x0:                                   /* IFF */
            if (nest == level) {
                int v = f[op & 7] & L[si + 1];
                if ((op & 8) ? !v : v) { level++; take_else = 0; } else take_else = 1;
            }
            nest++; si += 2; continue;
        case 0x1:                                   /* ANDF */
            if (nest == level) {
                int v = f[op & 7] & L[si + 1];
                if (!((op & 8) ? !v : v)) { take_else = 1; level--; }
            }
            si += 2; continue;
        case 0x2:                                   /* ELSEF */
            if (level == nest) level--;
            else if (take_else) {
                nest--;
                if (level == nest) { level++; take_else = 0; }
                nest++;
            }
            si += 1; continue;
        case 0x3:                                   /* ENDIFF */
            if (level == nest) level--;
            nest--; si += 1; continue;
        case 0x4:                                   /* XLATT */
            if (level == nest) {
                uint16_t v;
                const uint8_t *st = find_state(sd->active, L[si + 1]);
                int found = 0;
                if (st) { f[4] &= ~SCAN_MATCH; found = translate(st, scan, &v); }
                if (!found && (st = find_state(sd->common, L[si + 1]))) { f[4] &= ~SCAN_MATCH; found = translate(st, scan, &v); }
                if (found) {
                    buffer_fill(v);
                    f[4] |= SCAN_MATCH;
                    if (option & EXIT_IF_FOUND) goto out;
                }
            }
            si += 2; continue;
        case 0x5:                                   /* OPTION */
            if (level == nest) { if (op & 8) option &= ~L[si + 1]; else option |= L[si + 1]; }
            si += 2; continue;
        case 0x6:                                   /* SET_FLAG: flag tables are in the common section */
            if (nest == level) {
                const uint8_t *st = find_state(sd->common, L[si + 1]);
                if (st) {
                    f[4] &= ~SCAN_MATCH;
                    unsigned n = rd16(st + 7);
                    const uint8_t *e = st + 9;
                    for (unsigned i = 0; i < n; i++, e += 3) {
                        if (e[0] != scan) continue;
                        f[5] = 0; f[6] = 0;
                        if (e[1] < 7) f[e[1]] |= e[2];
                        f[4] |= SCAN_MATCH;
                        if (option & EXIT_IF_FOUND) goto out;
                        break;
                    }
                }
            }
            si += 2; continue;
        case 0x7:                                   /* PUT_ERROR_CHAR */
            if (nest == level) {
                const uint8_t *st = find_state(sd->active, L[si + 1]);
                if (!st) st = find_state(sd->common, L[si + 1]);
                if (st) buffer_fill(rd16(st + 5));
            }
            si += 2; continue;
        case 0x8:                                   /* IFKBD */
            if (nest == level) { if (rd16(L + si + 1) & sd->keyb_type) { level++; take_else = 0; } else take_else = 1; }
            nest++; si += 3; continue;
        case 0x9:                                   /* GOTO / EXIT_INT_9 / EXIT_STATE_LOGIC */
            if (nest == level) {
                int sub = op & 0x0F;
                if (sub == 1) goto out;
                if (sub == 2) { result = 0; goto out; }
                if (sub) goto out;
                si += (int16_t)rd16(L + si + 1);
                level = 0; nest = 0;
            }
            si += 3; continue;
        case 0xA:                                   /* BEEP */
            if (nest == level) beep_pending = 1;
            si += 1; continue;
        case 0xB:                                   /* RESET_NLS */
            if (nest == level) { f[5] = 0; f[6] = 0; }
            si += 1; continue;
        default:                                    /* FATAL_ERROR */
            goto out;
        }
    }
out:
    sd->nls1 = f[5]; sd->nls2 = f[6];
    return result;
}

/* ------------------------------------------------------- INT 15h AH=4Fh */

RESFN static int key(uint8_t code)
{
    struct keybsd *sd = &keyb_sd;
    if (code == 0xE0) { e0_seen = 1; return 0; }
    if (code == 0xE1) { e1_left = 5; return 0; }          /* Pause: E1 1D 45 E1 9D C5 */
    if (e1_left) { e1_left--; return 0; }
    int e0 = e0_seen;
    e0_seen = 0;
    if (code & 0x80) return 0;                            /* break codes: the BIOS's */
    uint8_t sc = code;
    /* shift and toggle keys (KEYBI9C.ASM K6): the BIOS keeps the flags */
    if (sc == 0x2A || sc == 0x36 || sc == 0x1D || sc == 0x38 || sc == 0x3A || sc == 0x45 || sc == 0x46) return 0;
    if (sc == 0x52 && !(BDA(KB_FLAG) & (CTL_SHIFT | ALT_SHIFT))) return 0;  /* Insert toggles */
    (void)e0;
    uint8_t fl = BDA(KB_FLAG);
    if (BDA(KB_FLAG_1) & HOLD_STATE) return 0;             /* the BIOS ends a pause */
    if (sc > 88) return 0;
    if ((fl & ALT_SHIFT) && !(BDA(KB_FLAG_1) & SYS_SHIFT)) {
        if (fl & CTL_SHIFT) {
            if (sc == 0x53) return 0;                     /* Ctrl-Alt-Del */
            if (sc == sd->hot_on) { sd->country = 0; keyb_publish(); return 1; }
            if (sc == sd->hot_off) { sd->country = 0xFF; keyb_publish(); return 1; }
        }
        return state_processor(sc);
    }
    if (fl & CTL_SHIFT) {
        if (sc == 0x37) return 0;                         /* Ctrl-PrtSc */
        return state_processor(sc);
    }
    return state_processor(sc);
}

RESFN void keyb_int15(struct armregs *f)
{
    if (((f->r0 >> 8) & 0xFF) != 0x4F) { keyb_sd.old15(f); return; }
    beep_pending = 0;
    if (key(f->r0 & 0xFF)) {
        f->cpsr &= ~ARM_CPSR_C;                           /* handled: the BIOS stops here */
        if (beep_pending) error_beep();
        return;
    }
    if (beep_pending) error_beep();
    keyb_sd.old15(f);                                     /* others may want it too */
}

/* ------------------------------------------------------------- INT 2Fh */

RESFN void keyb_int2f(struct armregs *f)
{
    unsigned ax = f->r0 & 0xFFFF;
    struct keybsd *sd = &keyb_sd;
    if ((ax >> 8) != 0xAD || (ax & 0xFF) < 0x80 || (ax & 0xFF) > 0x82) {
        if (sd->old2f) sd->old2f(f); else f->cpsr |= ARM_CPSR_C;
        return;
    }
    f->cpsr &= ~ARM_CPSR_C;
    switch (ax & 0xFF) {
    case 0x80:                                  /* installed? */
        f->r0 = 0xFFFF;
        f->r1 = 0x0100;                         /* version 1.00 */
        f->r5 = (uint32_t)sd;
        f->r8 = 0;
        break;
    case 0x81: {                                /* a new code page was selected on CON */
        unsigned cp = f->r1 & 0xFFFF;
        for (unsigned i = 0; i < sd->nsect; i++)
            if (rd16(sd->sect[i] + 2) == cp) {
                sd->active = sd->sect[i];
                sd->invoked_cp = cp;
                keyb_publish();
                return;
            }
        f->r0 = 1;
        f->cpsr |= ARM_CPSR_C;
        break;
    }
    case 0x82:                                  /* BL = 0: US, 0FFh: the national layout */
        if ((f->r1 & 0xFF) == 0 || (f->r1 & 0xFF) == 0xFF) { sd->country = f->r1 & 0xFF; keyb_publish(); }
        else f->cpsr |= ARM_CPSR_C;
        break;
    }
}

/* the transient part's last act: copy the tables behind the resident code
   (over the transient code, which is no longer needed) and stay resident */
RESFN void keyb_stay(const void *src, uint32_t len, uint32_t paras)
{
    const uint8_t *s = src;
    uint8_t *d = (uint8_t *)keyb_res_end;
    for (uint32_t i = 0; i < len; i++) d[i] = s[i];
    register uint32_t r0 __asm__("r0") = 0x3100;
    register uint32_t r3 __asm__("r3") = paras;
    __asm__ volatile("svc #0x21" : : "r"(r0), "r"(r3) : "memory");
    for (;;) ;
}
