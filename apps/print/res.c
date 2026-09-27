/*
 * res.c - PRINT's resident part (CMD/PRINT/PRINT_R.ASM of MS-DOS 4.0, MIT
 * licence, (C) Microsoft Corp., re-created in C).
 *
 * Everything here lives in ".text.unlikely.pr*" sections, which the SDK's
 * link.ld places right after _start, ahead of the transient code; resend.c
 * marks the end.  So the TSR keeps only this code and data, followed by the
 * queue, the buffer and the worker stack (moved down by r_keep() when the
 * transient part is done, as PRINT_R.ASM's MoveTrans makes room).  Nothing
 * here may call the C library or anything else outside these sections
 * (compiled -fno-builtin -fno-tree-loop-distribute-patterns, checked by
 * tests/run.mjs with nm).
 *
 * The spooler: INT 1Ch counts down TIMESLICE ticks, then (when neither
 * InDOS nor the critical-error flag is set) runs one activation - read a
 * block of the current file, or send the buffered characters to the list
 * device.  INT 28h (DOS idle) runs activations while COMMAND.COM waits for
 * a key.  INT 2Fh AH=01h is the interface the transient part uses.
 */
#include "res.h"

#define RC __attribute__((section(".text.unlikely.prres")))
#define RD __attribute__((section(".text.unlikely.prresd,\"ax\",%progbits @")))
#define RK __attribute__((section(".text.unlikely.prresk")))

int  r_i21(struct armregs *r);                          /* resasm.S */
void r_callstk(void (*fn)(void), void *sp);             /* resasm.S */

struct __attribute__((packed)) req_io {
    uint8_t len, unit, cmd;
    uint16_t status;
    uint8_t res[8];
    uint8_t media;
    uint32_t addr;
    uint16_t count, start;
};
#define RS_ERROR 0x8000
#define RS_BUSY  0x0200

/* ------------------------------------------------------------ data */
RD uint8_t r_slicecnt = 10, r_timeslice = 10, r_maxtick = 2, r_busytick = 1;
RD uint8_t r_queuelen = DEF_QUEUE;
RD uint16_t r_blksiz = MIN_BUF;
RD char r_listname[8] = { 'P', 'R', 'N', ' ', ' ', ' ', ' ', ' ' };
RD struct devhdr *r_listdev = 0;
RD uint8_t r_flag17_14 = 0;
RD uint16_t r_int17num = 0, r_int14num = 0;
RD char *r_filequeue = 0, *r_endqueue = 0, *r_queuetail = 0;
RD uint8_t *r_buffer = 0, *r_endptr = 0, *r_nxtchr = 0, *r_wstack = 0;
RD volatile uint8_t *r_indos = 0;
RD vect_t r_next1c = 0, r_next28 = 0, r_next2f = 0, r_next17 = 0, r_next14 = 0;
RD uint16_t r_my_psp = 0;
RD char r_pchar = '\\';

RD static volatile uint8_t busy = 0, sofint = 0;
RD static volatile uint8_t tickcnt = 0, ticksub = 0;
RD static uint8_t currfil = 0, queuelock = 0, colpos = 0, canflg = 0, acanocrd = 0, ambcan = 0;
RD static int8_t pabort = 0;
RD static int currhand = -1;
RD static uint16_t errcnt = 0;
RD static vect_t herrint = 0;
RD static uint16_t his_psp = 0;
RD static uint8_t contxt = 0;
RD static uint8_t bytebuf = 0;
RD static struct req_io ioreq = { 0 };
RD static char acname[16] = { 0 };

RK static const char errmes[] = "\r\n\r\n**********\r\n";
RK static const char belmes[] = "\r\x0C\x07";
RK static const char m_notfound[] = "File not found\r\n";
RK static const char m_errread[] = " error reading file\r\n";
RK static const char m_file[] = "\r\n\nFile ";
RK static const char m_canceled[] = " canceled by operator";
RK static const char m_allcan[] = "\r\n\nAll files canceled by operator";
RK static const char m_fat[] = "File allocation table bad drive ";
/* EXTEND19..31, the INT 24h error texts */
RK static const char m_err24[] =
    "Write protect error\0Invalid unit\0Not ready\0Invalid device request\0"
    "Data error\0Invalid device request parameters\0Seek error\0Invalid media type\0"
    "Sector not found\0Printer out of paper error\0Write fault error\0"
    "Read fault error\0General failure\0";

/* ------------------------------------------------------- helpers */
RC static void rz(struct armregs *r)
{
    volatile uint32_t *p = (volatile uint32_t *)r;
    for (int i = 0; i < (int)(sizeof *r / 4); i++) p[i] = 0;
}

RC static void rmove(void *d, const void *s, uint32_t n)
{
    volatile uint8_t *dd = d;
    const volatile uint8_t *ss = s;
    if (dd < ss) for (uint32_t i = 0; i < n; i++) dd[i] = ss[i];
    else while (n) { n--; dd[n] = ss[n]; }
}

RC static vect_t r_getvect(int n)
{
    struct armregs r;
    rz(&r); r.r0 = 0x3500 | n; r_i21(&r);
    return (vect_t)r.r1;
}

RC static void r_setvect(int n, vect_t h)
{
    struct armregs r;
    rz(&r); r.r0 = 0x2500 | n; r.r3 = (uint32_t)h; r_i21(&r);
}

/* DO_21: ^C checking off around the call (OffSave / OnSave) */
RC static int dos_noctlc(struct armregs *r)
{
    struct armregs b;
    rz(&b); b.r0 = 0x3302; b.r3 = 0; r_i21(&b);
    uint32_t old = b.r3 & 0xFF;
    int cf = r_i21(r);
    rz(&b); b.r0 = 0x3302; b.r3 = old; r_i21(&b);
    return cf;
}

RC static void context_switch(void)
{
    if (contxt) return;
    struct armregs r;
    rz(&r); r.r0 = 0x5100; dos_noctlc(&r);
    his_psp = r.r1 & 0xFFFF;
    rz(&r); r.r0 = 0x5000; r.r1 = r_my_psp; dos_noctlc(&r);
    contxt = 1;
}

RC static void context_back(void)
{
    if (!contxt) return;
    struct armregs r;
    rz(&r); r.r0 = 0x5000; r.r1 = his_psp; dos_noctlc(&r);
    contxt = 0;
}

/* My21: in PRINT's own PSP context, so the files it opens are its own */
RC static int my21(struct armregs *r) { context_switch(); return dos_noctlc(r); }

/* ---------------------------------------------- the list device */
RC static void docall(int cmd)
{
    volatile uint8_t *p = (volatile uint8_t *)&ioreq;
    for (unsigned i = 0; i < sizeof ioreq; i++) p[i] = 0;
    ioreq.len = 22;
    ioreq.cmd = cmd;
    ioreq.addr = (uint32_t)&bytebuf;
    ioreq.count = 1;
    r_listdev->strategy(&ioreq);
    r_listdev->interrupt();
}

/* PSTAT: 1 if the device can take a character */
RC static int pstat(void)
{
    errcnt++;
    docall(10);
    uint16_t st = ioreq.status;
    if (st & RS_ERROR) st |= RS_BUSY;
    if (!(st & RS_BUSY)) { errcnt = 0; return 1; }
    return 0;
}

RC static void pout(uint8_t c) { bytebuf = c; docall(8); }

/* LOUT: wait for the device, but not forever */
RC static void lout(uint8_t c)
{
    for (;;) {
        if (pstat()) { pout(c); return; }
        if (errcnt > ERRCNT2) return;
    }
}

RC static void listmes(const char *s) { while (*s) lout((uint8_t)*s++); }

RC static void open_dev(void) { if (r_listdev->attr & DEVA_OPCL) docall(13); }
RC static void close_dev(void) { if (r_listdev->attr & DEVA_OPCL) docall(14); }

/* ---------------------------------------------------- DSKERR (INT 24h) */
RC static void dskerr(struct armregs *f)
{
    if (pabort == 0) {
        listmes(errmes);
        if (!(AH(f) & 0x80)) {
            unsigned di = f->r5 & 0xFF;
            if (di > 12) di = 12;
            const char *t = m_err24;
            while (di--) { while (*t) t++; t++; }
            listmes(t);
            listmes(m_errread);
            listmes(r_filequeue);
            listmes(belmes);
        } else {
            char drv[5];
            drv[0] = 'A' + AL(f); drv[1] = '.'; drv[2] = '\r'; drv[3] = '\n'; drv[4] = 0;
            listmes(m_fat);
            listmes(drv);
        }
        pabort++;
    }
    f->r0 &= ~0xFFu;                    /* ignore */
}

RC static void set24(void) { herrint = r_getvect(0x24); r_setvect(0x24, dskerr); }
RC static void res24(void) { r_setvect(0x24, herrint); }

RC static int open_file(const char *name)
{
    struct armregs r;
    rz(&r); r.r0 = 0x3D00; r.r2 = 0x16; r.r3 = (uint32_t)name;
    if (my21(&r)) return -(int)(r.r0 & 0xFFFF);
    return r.r0 & 0xFFFF;
}

RC static void close_file(int h)
{
    struct armregs r;
    rz(&r); r.r0 = 0x3E00; r.r1 = h; my21(&r);
}

/* CompQ: the current file is done, move the queue up */
RC static void compq(void)
{
    rmove(r_filequeue, r_filequeue + MAXFILELEN, r_endqueue - (r_filequeue + MAXFILELEN));
    r_queuetail -= MAXFILELEN;
    *r_queuetail = 0;
}

RC static void prtoperr(void)
{
    open_dev();
    listmes(errmes);
    listmes(m_notfound);
    listmes(r_filequeue);
    listmes(belmes);
    close_dev();
}

/* open the first file in the queue; the ones that fail are reported on the
 * printer and dropped */
RC static void next_file(void)
{
    for (;;) {
        if (!r_filequeue[0]) return;
        set24();
        pabort = 0;
        int h = open_file(r_filequeue);
        res24();
        if (pabort) { compq(); continue; }
        if (h < 0) { prtoperr(); compq(); continue; }
        currhand = h;
        currfil = 1;
        open_dev();
        return;
    }
}

RC static void drop_current(void)
{
    set24();
    pabort = 1;
    close_file(currhand);
    res24();
    currfil = 0;
    currhand = -1;
    r_nxtchr = r_endptr;
}

RC static void filclose(void)
{
    set24();
    pabort = -1;
    close_file(currhand);
    res24();
    currfil = 0;
    currhand = -1;
    r_nxtchr = r_endptr;
    close_dev();
    compq();
    next_file();
}

RC static void readbuff(void)
{
    set24();
    pabort = 0;
    struct armregs r;
    rz(&r); r.r0 = 0x3F00; r.r1 = currhand; r.r2 = r_blksiz; r.r3 = (uint32_t)r_buffer;
    int cf = my21(&r);
    res24();
    if (pabort) { filclose(); return; }
    unsigned n = r.r0 & 0xFFFF;
    if (cf || n == 0) { pout(0x0C); filclose(); return; }
    r_nxtchr = r_buffer;
    for (unsigned i = n; i < r_blksiz; i++) r_buffer[i] = 0x1A;
}

/* DOINT: one activation of the spooler */
RC static void doint(void)
{
    if (!currfil || queuelock) return;
    if (r_nxtchr >= r_endptr) { readbuff(); goto done; }
    unsigned spins = 0;
    for (;;) {
        if (r_nxtchr >= r_endptr) break;
        if (!sofint && tickcnt >= r_maxtick) break;
        int ready = pstat();
        if (!currfil) break;
        if (!ready) {
            /* busy: give up at once from INT 28h, after BUSYTICK ticks from
             * the timer (ticks cannot arrive while IRQ 0 is in service here,
             * so also after a bounded number of polls) */
            if (sofint || ticksub >= r_busytick || ++spins > 20000) break;
            continue;
        }
        uint8_t c = *r_nxtchr;
        if (c == 0x1A) { pout(0x0C); filclose(); break; }
        if (c == '\r') colpos = 0;
        if (c == '\t') {                /* one blank per pass up to the tab stop */
            unsigned n = (unsigned)(-(int8_t)(colpos | 0xF8)) & 0xFF;
            colpos++;
            pout(' ');
            if (n != 1) continue;
        } else {
            if (c == 8) colpos--;
            if (c >= 0x20) colpos++;
            pout(c);
        }
        r_nxtchr++;
        ticksub = 0;
    }
done:
    context_back();
}

RC static void run_worker(void) { r_callstk(doint, r_wstack + WSTACK); }

/* HDSPINT (INT 1Ch), also INT 2Fh AX=0080h (FakeINT1C) */
RC static void timer_path(void)
{
    tickcnt++;
    ticksub++;
    if (r_slicecnt) { r_slicecnt--; return; }
    if (busy) return;
    if (r_indos[-1] || r_indos[0]) return;      /* critical error flag, InDOS */
    busy++;
    tickcnt = 0;
    ticksub = 0;
    run_worker();
    r_slicecnt = r_timeslice;
    busy--;
}

RC void r_int1c(struct armregs *f)
{
    timer_path();
    if (r_next1c) r_next1c(f);
}

/* SPINT (INT 28h) */
RC void r_int28(struct armregs *f)
{
    if (!busy) {
        busy++;
        sofint++;
        run_worker();
        sofint = 0;
        r_slicecnt = r_timeslice;
        busy--;
    }
    if (r_next28) r_next28(f);
}

/* INT 17h / INT 14h: while PRINT prints on that port, others time out */
RC void r_int17(struct armregs *f)
{
    if (r_flag17_14 == 1 && currfil && (f->r3 & 0xFFFF) == r_int17num && !busy) {
        f->r0 = (f->r0 & ~0xFF00u) | 0xA100;
        return;
    }
    if (r_next17) r_next17(f); else f->cpsr |= ARM_CPSR_C;
}

RC void r_int14(struct armregs *f)
{
    if (r_flag17_14 == 2 && currfil && (f->r3 & 0xFFFF) == r_int14num && !busy) {
        unsigned ah = AH(f);
        if (ah == 0 || ah > 2) f->r0 &= ~0xFFu;
        f->r0 = (f->r0 & ~0xFF00u) | 0x8000;
        return;
    }
    if (r_next14) r_next14(f); else f->cpsr |= ARM_CPSR_C;
}

/* ------------------------------------------------ INT 2Fh functions */
/* ADDFIL: DS:DX -> { level 0, far pointer (flat here) to the name } */
RC static int addfil(struct armregs *f)
{
    if (r_queuetail >= r_endqueue) { f->r0 = E_QFULL; return 1; }
    const uint8_t *pk = (const uint8_t *)f->r3;
    if (pk[0] != 0) { f->r0 = 1; return 1; }
    const char *name = (const char *)(pk[1] | (pk[2] << 8) | (pk[3] << 16) | ((uint32_t)pk[4] << 24));
    char *d = r_queuetail;
    int i;
    for (i = 0; i < MAXFILELEN; i++) if (!(d[i] = name[i])) break;
    if (i == MAXFILELEN) { f->r0 = E_NAMELONG; return 1; }
    char *slot = r_queuetail;
    r_queuetail += MAXFILELEN;
    *r_queuetail = 0;
    set24();
    pabort = 0;
    int h = open_file(slot);
    res24();
    if (h < 0) {
        int err = -h;
        if (slot[1] == ':') {           /* a bad drive? */
            struct armregs r;
            rz(&r); r.r0 = 0x1900; my21(&r);
            int cur = r.r0 & 0xFF, dl = (slot[0] | 0x20) - 'a';
            rz(&r); r.r0 = 0x0E00; r.r3 = dl; my21(&r);
            rz(&r); r.r0 = 0x1900; my21(&r);
            int got = r.r0 & 0xFF;
            rz(&r); r.r0 = 0x0E00; r.r3 = cur; my21(&r);
            if (got != dl) err = 15;
        }
        r_queuetail -= MAXFILELEN;
        *r_queuetail = 0;
        f->r0 = err;
        return 1;
    }
    if (currfil) {
        set24(); pabort = 1; close_file(h); res24();
    } else {
        currhand = h;
        r_nxtchr = r_endptr;
        currfil = 1;
        open_dev();
    }
    return 0;
}

/* AmbChk: "*.TXT" to cancel becomes "????????.TXT" (INT 21h AH=29h), in
 * the caller's buffer */
RC static void ambchk(char *name)
{
    ambcan = 0;
    char *p = name;
    while (*p) p++;
    while (p > name) {
        char c = *--p;
        if (c == '*' || c == '?') ambcan = 1;
        if (c == r_pchar) break;
    }
    if (!ambcan) return;
    char *fn = p + 1;
    uint8_t *fcb = (uint8_t *)acname;
    for (int i = 0; i < 12; i++) fcb[i] = ' ';
    struct armregs r;
    rz(&r); r.r0 = 0x2900; r.r4 = (uint32_t)fn; r.r5 = (uint32_t)fcb; my21(&r);
    char *d = fn;
    for (int i = 0; i < 8 && fcb[1 + i] != ' '; i++) *d++ = fcb[1 + i];
    if (fcb[9] != ' ') {
        *d++ = '.';
        for (int i = 0; i < 3 && fcb[9 + i] != ' '; i++) *d++ = fcb[9 + i];
    }
    *d = 0;
}

/* the comparison of CANFIL (queue names are upper case) */
RC static int name_match(const char *s, const char *q)
{
    for (;;) {
        char al = *s++;
        if (al == *q) {
            if (!*q) return 1;
            q++;
            continue;
        }
        if (!ambcan || al != '?') return 0;
        if (*q == '.') {                /* FindPeriod */
            for (;;) {
                al = *s++;
                if (!al) return 0;
                if (al == '.') break;
            }
            q++;
            continue;
        }
        if (!*q) {                      /* FindNul */
            for (;;) {
                al = *s++;
                if (al == '?' || al == '.') continue;
                return al == 0;
            }
        }
        q++;
    }
}

RC static int canfil(struct armregs *f)
{
    if (!currfil) return 0;
    char *name = (char *)f->r3;
    canflg = 0;
    acanocrd = 0;
    char *bx = r_filequeue;
    ambchk(name);
    for (;;) {
        if (name_match(name, bx)) {
            acanocrd = 1;
            if (bx == r_filequeue && !canflg) {
                canflg = 1;
                drop_current();
                listmes(m_file);
                listmes(bx);
                listmes(m_canceled);
                listmes(belmes);
                close_dev();
            }
            char *si = bx + MAXFILELEN;
            if (si == r_queuetail) *bx = 0;
            else rmove(bx, si, r_endqueue - si);
            r_queuetail -= MAXFILELEN;
            *r_queuetail = 0;
            if (*bx) continue;
            break;
        }
        bx += MAXFILELEN;
        if (*bx) continue;
        if (acanocrd) break;
        f->r0 = 2;                      /* error_file_not_found */
        return 1;
    }
    if (currhand == -1) next_file();    /* StartAnFil */
    return 0;
}

RC static int canall(void)
{
    if (currfil) {
        drop_current();
        r_queuetail = r_filequeue;
        r_filequeue[0] = 0;
        listmes(m_allcan);
        listmes(belmes);
        close_dev();
    }
    return 0;
}

/* SPCOMINT */
RC void r_int2f(struct armregs *f)
{
    unsigned ah = AH(f), al = AL(f);
    if (ah > 1) { if (r_next2f) r_next2f(f); else f->cpsr |= ARM_CPSR_C; return; }
    if (al >= 0xF8) return;
    if ((f->r0 & 0xFFFF) == 0x0080) { timer_path(); return; }
    if (ah == 0) { f->r0 = (f->r0 & ~0xFFu) | 1; return; }     /* PSPRINT: go away */
    if (al == 0) { f->r0 |= 0xFF; return; }                     /* installed */
    if (busy) { f->r0 = E_BUSY; f->cpsr |= ARM_CPSR_C; return; }
    if (al > 6) { f->r0 = 1; f->cpsr |= ARM_CPSR_C; return; }
    busy++;
    queuelock = 0;
    int cf = 0;
    if (al == 1) cf = addfil(f);
    else if (al == 2) cf = canfil(f);
    else if (al == 3) cf = canall();
    else if (al == 4) {                 /* QSTAT: lock the queue, DS:SI -> it */
        queuelock = 1;
        pstat();
        f->r4 = (uint32_t)r_filequeue;
        f->r3 = errcnt;
    } else if (al == 5) {               /* EndStat */
        queuelock = 0;
    } else {                            /* QSTATDEV */
        queuelock = 1;
        pstat();
        f->r0 = 0;
        if (r_filequeue[0]) { f->r0 = E_QFULL; f->r4 = (uint32_t)r_listdev; cf = 1; }
        queuelock = 0;
    }
    if (!cf) pstat();
    context_back();
    busy--;
    if (cf) f->cpsr |= ARM_CPSR_C; else f->cpsr &= ~ARM_CPSR_C;
}

/* MoveTrans, the other way round: the transient part is finished, so the
 * queue, the buffer and the worker stack move down to the end of the
 * resident part, and PRINT stays resident with just that */
RC void r_keep(void)
{
    __asm__ volatile ("mrs r0, cpsr\n orr r0, r0, #0x80\n msr cpsr_c, r0" ::: "r0", "memory");
    uint8_t *dst = (uint8_t *)(((uint32_t)r_end + 3) & ~3u);
    uint8_t *src = (uint8_t *)r_filequeue;
    uint32_t n = (uint32_t)(r_endptr - src);
    int32_t delta = dst - src;
    rmove(dst, src, n);
    r_filequeue += delta; r_endqueue += delta; r_queuetail += delta;
    r_buffer += delta; r_endptr += delta; r_nxtchr += delta;
    r_wstack = (uint8_t *)(((uint32_t)r_endptr + 7) & ~7u);
    uint32_t keep = (uint32_t)(r_wstack + WSTACK) - ((uint32_t)r_my_psp << 4);
    struct armregs r;
    rz(&r);
    r.r0 = 0x3100;
    r.r3 = (keep + 15) >> 4;
    r_i21(&r);
    for (;;) ;
}
