/*
 * display.c - DISPLAY.SYS for ARM-DOS 4.00: the resident code page switching
 * console driver.
 *
 * A re-creation in C of MS-DOS 4.00's DEV/DISPLAY (DISPLAY.ASM, CPS-FUNC.INC,
 * INT10COM.INC, INT2FCOM.INC, F-PARSER.INC; Microsoft, MIT licence), behaviour
 * for behaviour: a "CON" device that passes all console I/O to the CON driver
 * it was loaded over and adds the code page functions of generic IOCTL
 * category 3 - 4Ch/4Dh prepare (the font file arrives by IOCTL write, as MODE
 * CON CP PREPARE sends it, and is parsed as it streams in), 4Ah select, 6Ah
 * query selected, 6Bh query prepared list - with DOS 4's rules (a prepare list
 * longer than the slots or with duplicates fails, -1 keeps a slot, a failed
 * prepare leaves its slot unprepared, replacing the active code page leaves no
 * code page selected, select tells KEYB through INT 2Fh AD81h), the INT 2Fh
 * AD00h-AD03h/AD10h interface KEYB uses, and the INT 10h hook that puts the
 * selected font back after every mode set and font load.
 *
 * ARM-PC: the font goes into the VGA character generator RAM at 11000000h
 * (ARCH.md 3); 8x16 for 16-line text, rows 1-14 of the same glyphs for
 * 14-line text (as the ARM-PC BIOS makes its 14-line font), 8x8 for 43/50
 * lines. INT 10h AX=1130h returns the selected code page's 8x8/8x16 font
 * instead of the ROM's, so 43/50-line modes built with it (ANSI.SYS) follow
 * too. The screen's code page is published on system board port F7h (ARCH.md
 * 4.6) for the web page.
 */
#include "display.h"

__attribute__((section(".devhdr"), used))
struct devhdr display_header = {
    (struct devhdr *)0xFFFFFFFFu, 0xC053, 0, disp_strategy, disp_interrupt,
    { 'C', 'O', 'N', ' ', ' ', ' ', ' ', ' ' }
};

struct devhdr *disp_oldcon RES;
armdos_vect_t disp_old10 RES, disp_old2f RES;
uint16_t disp_hwcp RES = NOCP;
uint16_t disp_nslots RES, disp_nfonts RES;
struct slot *disp_slots RES;
uint16_t disp_active RES = NOCP;        /* CPD_ACTIVE: NOCP until a select */

static struct reqhdr *req RES;
static uint8_t in10 RES;

/* ---------------------------------------------------------- the fonts */

static inline uint16_t rd16(const uint8_t *p) { return p[0] | (p[1] << 8); }
static inline uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static inline void wr16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }

static struct slot *find_slot(uint16_t cp)
{
    if (cp == NOCP) return 0;
    for (unsigned i = 0; i < disp_nslots; i++)
        if (disp_slots[i].cp == cp && disp_slots[i].ok) return &disp_slots[i];
    return 0;
}

/* TEST_CP: is the code page there (hardware or prepared)? */
static int cp_valid(uint16_t cp) { return cp != NOCP && (cp == disp_hwcp || find_slot(cp)); }

static int mono(void) { return BDA8(0x49) == 7; }

/* INVOKE_DATA: the font of the selected code page into the character generator */
static void load_font(void)
{
    if (mono()) return;
    const uint8_t *f16, *f8;
    struct slot *s = disp_active != disp_hwcp ? find_slot(disp_active) : 0;
    if (s) { f16 = s->f16; f8 = s->f8; }
    else {                                      /* the hardware code page: the ROM's fonts */
        struct armregs r = { 0 };
        r.r0 = 0x1130; r.r1 = 0x0600;
        in10++; disp_old10(&r); in10--;
        f16 = (const uint8_t *)r.r6;
        r.r0 = 0x1130; r.r1 = 0x0300;
        in10++; disp_old10(&r); in10--;
        f8 = (const uint8_t *)r.r6;
    }
    int h = BDA8(0x85) ? BDA8(0x85) : 16;
    for (int c = 0; c < 256; c++) {
        volatile uint8_t *d = FONTRAM + c * 32;
        for (int y = 0; y < 32; y++) {
            uint8_t v = 0;
            if (h == 8) { if (y < 8) v = f8[c * 8 + y]; }
            else if (h == 14) { if (y < 14) v = f16[c * 16 + y + 1]; }
            else if (y < 16) v = f16[c * 16 + y];
            d[y] = v;
        }
    }
}

static int cp_index(uint16_t cp)
{
    switch (cp) { case 437: return 0; case 850: return 1; case 860: return 2; case 863: return 3; case 865: return 4; }
    return 7;
}

/* the code page of the font on the screen, for the page's keyboard (port F7h) */
void disp_publish(void)
{
    uint16_t cp = cp_valid(disp_active) ? disp_active : disp_hwcp;
    armdos_outb(0xF7, cp == NOCP ? 0 : cp_index(cp));
}

/* SIGNAL_KBD_INVK: tell KEYB (if it is there) the new code page; 0 = accepted */
static int signal_keyb(uint16_t cp)
{
    struct armregs r = { 0 };
    r.r0 = 0xAD80;
    int2f(&r);
    if ((r.r0 & 0xFF) != 0xFF) return 0;
    r.r0 = 0xAD81; r.r1 = cp; r.cpsr = 0;
    return int2f(&r);
}

/* INVOKE_CP: 0, or the device error code */
static int invoke(uint16_t cp)
{
    if (cp == NOCP || !cp_valid(cp)) return DE_NOTPREP;
    if (cp != disp_active) { disp_active = cp; load_font(); disp_publish(); }
    if (signal_keyb(cp)) return DE_KEYB;
    return 0;
}

/* --------------------------------------------- prepare: the font file parser */

/* The font file (EGA.CPI, "FONT" format) arrives in pieces by IOCTL write.
   The parser follows the file's own pointers: a queue of the records still to
   come (file header, info header, code page entry headers, code page info
   headers, screen font headers, font bitmaps), in file order. */
enum { R_FILEHDR = 1, R_FIH, R_CPEH, R_CPIH, R_SFH, R_DATA16, R_DATA8, R_SKIP };
struct prec { uint32_t off; uint16_t len; uint8_t kind; uint8_t slot; uint16_t aux; };
static struct prec pq[8] RES;
static uint8_t npq RES;
static uint8_t rb[32] RES;
static uint32_t pos RES;
static uint8_t prep RES;                /* 1: between 4Ch and 4Dh; 2: a refresh */
static uint8_t prep_bad RES;
static uint16_t cp_left RES;            /* code page entries still to come */
static uint16_t want[MAX_SLOTS] RES;    /* the code page being loaded into each slot */

static void push(uint32_t off, uint16_t len, int kind, int slot, uint16_t aux)
{
    if (off < pos || npq >= sizeof pq / sizeof pq[0]) { prep_bad = 1; return; }
    unsigned i = npq++;
    while (i && pq[i - 1].off > off) { pq[i] = pq[i - 1]; i--; }
    pq[i].off = off; pq[i].len = len; pq[i].kind = kind; pq[i].slot = slot; pq[i].aux = aux;
}

static void record_done(struct prec *r)
{
    switch (r->kind) {
    case R_FILEHDR:
        if (rb[0] != 0xFF || rb[1] != 'F' || rb[2] != 'O' || rb[3] != 'N' || rb[4] != 'T' || rd16(rb + 16) < 1 || rb[18] != 1) { prep_bad = 1; return; }
        push(rd32(rb + 19), 2, R_FIH, 0, 0);
        return;
    case R_FIH:
        cp_left = rd16(rb);
        if (cp_left) push(r->off + 2, 0x1C, R_CPEH, 0, 0);
        return;
    case R_CPEH: {
        cp_left--;
        uint32_t next = rd32(rb + 2), cpih = rd32(rb + 24);
        uint16_t cp = rd16(rb + 16);
        int slot = 0xFF;
        static const char ega[8] RESRO = { 'E', 'G', 'A', ' ', ' ', ' ', ' ', ' ' };
        int match = rd16(rb + 6) == 1;
        for (int k = 0; k < 8; k++) if (rb[8 + k] != ega[k]) match = 0;
        if (match) for (unsigned i = 0; i < disp_nslots; i++) if (want[i] == cp) slot = i;
        if (slot != 0xFF) push(cpih, 6, R_CPIH, slot, 0);
        if (cp_left && next) push(next, 0x1C, R_CPEH, 0, 0);
        return;
    }
    case R_CPIH:
        if (rd16(rb + 2)) push(r->off + 6, 6, R_SFH, r->slot, rd16(rb + 2));
        return;
    case R_SFH: {
        unsigned h = rb[0], w = rb[1], n = rd16(rb + 4);
        uint32_t len = h * n * ((w + 7) / 8);
        int kind = R_SKIP;
        if (w == 8 && n == 256 && h == 16) kind = R_DATA16;
        else if (w == 8 && n == 256 && h == 8) kind = R_DATA8;
        if (len) push(r->off + 6, len > 0xFFFF ? 0xFFFF : len, kind, r->slot, 0);
        if (r->aux > 1) push(r->off + 6 + len, 6, R_SFH, r->slot, r->aux - 1);
        return;
    }
    case R_DATA16: disp_slots[r->slot].got |= 1; return;
    case R_DATA8: disp_slots[r->slot].got |= 2; return;
    }
}

/* one piece of the file; 0, or a device error code */
static int feed(const uint8_t *p, unsigned n)
{
    for (unsigned k = 0; k < n; k++, pos++) {
        if (!npq || prep_bad) continue;
        struct prec *r = &pq[0];
        if (pos < r->off) continue;
        unsigned i = pos - r->off;
        if (r->kind == R_DATA16) disp_slots[r->slot].f16[i] = p[k];
        else if (r->kind == R_DATA8) disp_slots[r->slot].f8[i] = p[k];
        else if (r->kind != R_SKIP && i < sizeof rb) rb[i] = p[k];
        if (i + 1 == r->len) {
            struct prec done = *r;
            npq--;
            for (unsigned j = 0; j < npq; j++) pq[j] = pq[j + 1];
            record_done(&done);
        }
    }
    if (prep_bad) return DE_DEVERR;
    /* all the code page entries seen: a code page that is not in the file is
       an error now (DOS 4's DISPLAY fails the write) */
    if (!cp_left && npq == 0)
        for (unsigned i = 0; i < disp_nslots; i++)
            if (want[i] != NOCP && disp_slots[i].got != 3) return DE_DEVERR;
    return 0;
}

/* ------------------------------------------------ generic IOCTL, category 3 */

/* 4Ch: begin prepare. Packet: length, number of code pages, the code pages
   (-1 = leave that slot alone); none = refresh the active font. */
static int prepare_begin(uint8_t *d)
{
    unsigned n = rd16(d + 2);
    if (!n) {                                   /* MODE CON CP REFRESH */
        if (disp_active == NOCP || !cp_valid(disp_active)) return DE_DEVERR;
        if (signal_keyb(disp_active)) return DE_KEYB;
        load_font();
        prep = 2;                               /* 4Dh ends it without a file */
        return 0;
    }
    if (n > disp_nslots) return DE_DEVERR;
    unsigned real = 0;
    for (unsigned i = 0; i < n; i++) {
        uint16_t c = rd16(d + 4 + 2 * i);
        if (c == NOCP) continue;
        real++;
        for (unsigned k = i + 1; k < n; k++) if (rd16(d + 4 + 2 * k) == c) return DE_DEVERR;
    }
    if (!real) return DE_DEVERR;
    for (unsigned i = 0; i < disp_nslots; i++) want[i] = NOCP;
    for (unsigned i = 0; i < n; i++) {
        uint16_t c = rd16(d + 4 + 2 * i);
        if (c == NOCP) continue;
        want[i] = c;
        disp_slots[i].cp = c;
        disp_slots[i].ok = 0;
        disp_slots[i].got = 0;
    }
    pos = 0; npq = 0; prep_bad = 0; cp_left = 1;
    push(0, 23, R_FILEHDR, 0, 0);
    prep = 1;
    return 0;
}

static int prepare_end(void)
{
    if (!prep) return DE_GENFAIL;
    if (prep == 2) { prep = 0; return 0; }     /* after a refresh */
    prep = 0;
    int bad = prep_bad;
    for (unsigned i = 0; i < disp_nslots; i++) {
        if (want[i] == NOCP) continue;
        if (disp_slots[i].got == 3) disp_slots[i].ok = 1;
        else { disp_slots[i].cp = NOCP; bad = 1; }
        want[i] = NOCP;
    }
    if (!cp_valid(disp_active)) disp_active = NOCP;
    disp_publish();
    return bad ? DE_GENFAIL : 0;
}

static int gen_ioctl(struct req_gioctl *q)
{
    uint8_t *d = (uint8_t *)q->data;
    switch (q->minor) {
    case 0x4C: return prepare_begin(d);
    case 0x4D: return prepare_end();
    case 0x4A:                                  /* select */
        if (rd16(d) != 2) return DE_DEVERR;
        return invoke(rd16(d + 2));
    case 0x6A:                                  /* query selected */
        wr16(d, 2);
        if (!cp_valid(disp_active)) { wr16(d + 2, NOCP); return DE_NOTPREP; }
        wr16(d + 2, disp_active);
        return 0;
    case 0x6B: {                                /* query the prepared list */
        unsigned nhw = disp_hwcp != NOCP;
        uint8_t *p = d + 2;
        wr16(d, (nhw + disp_nslots + 2) * 2);
        wr16(p, nhw); p += 2;
        if (nhw) { wr16(p, disp_hwcp); p += 2; }
        wr16(p, disp_nslots); p += 2;
        for (unsigned i = 0; i < disp_nslots; i++, p += 2) wr16(p, disp_slots[i].ok ? disp_slots[i].cp : NOCP);
        return 0;
    }
    }
    return -1;                                  /* not ours */
}

/* ------------------------------------------------------ the interface */

void disp_strategy(struct reqhdr *r) { req = r; }

static void pass(struct reqhdr *r)
{
    disp_oldcon->strategy(r);
    disp_oldcon->entry();
}

void disp_interrupt(void)
{
    struct reqhdr *r = req;
    int e;
    switch (r->cmd) {
    case 0:
        disp_init((struct req_init *)r);
        return;
    case 12: {                                  /* IOCTL write: the font file, while preparing */
        struct req_rw *q = (struct req_rw *)r;
        if (prep != 1) { r->status = RS_DONE | RS_ERROR | DE_GENFAIL; return; }
        e = feed((const uint8_t *)q->addr, q->count);
        r->status = e ? (RS_DONE | RS_ERROR | e) : RS_DONE;
        return;
    }
    case 19: {
        struct req_gioctl *q = (struct req_gioctl *)r;
        if (q->category != 3 || (e = gen_ioctl(q)) < 0) { pass(r); return; }
        r->status = e ? (RS_DONE | RS_ERROR | e) : RS_DONE;
        return;
    }
    default:
        pass(r);
        return;
    }
}

/* INT 10h: the selected font survives mode sets and ROM font loads; the ROM
   font pointers (AX=1130h) are the selected code page's */
void disp_int10(struct armregs *f)
{
    int ah = (f->r0 >> 8) & 0xFF, al = f->r0 & 0xFF;
    if (in10) { disp_old10(f); return; }
    int soft = disp_active != disp_hwcp && find_slot(disp_active);
    if (ah == 0x11 && al == 0x30 && soft) {
        int bh = (f->r1 >> 8) & 0xFF;
        disp_old10(f);
        struct slot *s = find_slot(disp_active);
        f->r6 = (uint32_t)((bh == 2 || bh == 3) ? s->f8 : s->f16);
        return;
    }
    disp_old10(f);
    if (!soft) return;
    if (ah == 0x00 || (ah == 0x11 && (al == 0x01 || al == 0x02 || al == 0x04 || al == 0x11 || al == 0x12 || al == 0x14)))
        load_font();
}

/* INT 2Fh AH=ADh AL=00h-03h, 10h: the console's code page interface */
void disp_int2f(struct armregs *f)
{
    unsigned ax = f->r0 & 0xFFFF;
    if ((ax >> 8) != 0xAD || ((ax & 0xFF) > 3 && (ax & 0xFF) != 0x10)) {
        if (disp_old2f) disp_old2f(f); else f->cpsr |= ARM_CPSR_C;
        return;
    }
    int cf = 0;
    switch (ax & 0xFF) {
    case 0x00:                                  /* installed */
        f->r0 = 0xFFFF; f->r1 = 0x0100;
        break;
    case 0x01:                                  /* select BX */
        if (prep || invoke(f->r1 & 0xFFFF)) cf = 1;
        break;
    case 0x02:                                  /* the selected code page */
        if (!cp_valid(disp_active)) { f->r0 = 1; cf = 1; }
        else f->r1 = disp_active;
        break;
    case 0x03: {                                /* designated, fonts, hardware; the lists */
        unsigned nhw = disp_hwcp != NOCP;
        if ((3 + nhw + disp_nslots) * 2 > (f->r2 & 0xFFFF)) { cf = 1; break; }
        uint8_t *p = (uint8_t *)f->r5;
        wr16(p, disp_nslots); wr16(p + 2, disp_nfonts); wr16(p + 4, nhw);
        p += 6;
        if (nhw) { wr16(p, disp_hwcp); p += 2; }
        for (unsigned i = 0; i < disp_nslots; i++, p += 2) wr16(p, disp_slots[i].ok ? disp_slots[i].cp : NOCP);
        break;
    }
    case 0x10:                                  /* is there a font for the current mode? */
        break;
    }
    if (cf) f->cpsr |= ARM_CPSR_C; else f->cpsr &= ~ARM_CPSR_C;
}

/* for the compiler's structure initialisation (resident: load_font and
   signal_keyb use it) */
void *memset(void *d, int c, unsigned n)
{
    uint8_t *p = d;
    while (n--) *p++ = c;
    return d;
}
