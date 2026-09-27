/*
 * armcd.c - ARMCD.SYS resident part: the device driver for the ARM-PC's ATAPI
 * CD-ROM drive (secondary IDE channel, master), in the style of the 1993 vendor
 * drivers that the CD-ROM extensions (ARMCDEX.EXE) talk to.
 *
 *   DEVICE=C:\DOS\ARMCD.SYS /D:ARMCD001
 *
 * Implements the CD-ROM driver request set: INIT, IOCTL INPUT (device header,
 * head location, audio channel info, drive bytes, device status, sector size,
 * volume size, media changed, audio disk/track info, Q channel, UPC, audio status),
 * INPUT/OUTPUT FLUSH, IOCTL OUTPUT (eject, lock/unlock, reset, audio channel control,
 * close tray), DEVICE OPEN/CLOSE, READ LONG (cooked 2048 and raw 2352 audio),
 * READ LONG PREFETCH, SEEK, PLAY AUDIO, STOP AUDIO (pause, then stop), RESUME AUDIO.
 * The request status has the BUSY bit set while audio plays, as the spec requires.
 *
 * The drive is polled (nIEN set); long waits (spin-up, data still arriving) sleep
 * in WFI between timer ticks.
 *
 * Copyright (C) 1993 Europa Micro Systems (ARM-DOS project).
 */
#include "armcd.h"

__attribute__((section(".devhdr"), used))
struct cddevhdr armcd_header = {
    (struct cddevhdr *)0xFFFFFFFFu, 0xC800, 0, armcd_strategy, armcd_interrupt,
    { 'A', 'R', 'M', 'C', 'D', '0', '0', '1' }, 0, 0, 1
};

static struct cdreq *req RES;
uint32_t armcd_sense RES;
static uint8_t media_changed RES;      /* for IOCTL 9: -1 once after a change */
static uint8_t disc_known RES;         /* 1 = a disc was seen since the last change */
static uint8_t paused RES;             /* STOP AUDIO paused a play (RESUME continues it) */
static uint8_t locked RES;
static uint32_t play_start RES, play_end RES;   /* LBAs of the last PLAY / RESUME */
/* the TOC (valid while toc_ok) */
static uint8_t toc_ok RES, toc_first RES, toc_last RES;
static uint32_t toc_leadout RES;
static uint32_t toc_start[100] RES;
static uint8_t toc_ctl[100] RES;
static uint8_t scratch[64] RES;

/* ------------------------------------------------------------ helpers */

void *memset(void *d, int c, size_t n) { uint8_t *p = d; while (n--) *p++ = c; return d; }
void *memcpy(void *d, const void *s, size_t n) { uint8_t *p = d; const uint8_t *q = s; while (n--) *p++ = *q++; return d; }

static void zero(uint8_t *p, unsigned n) { while (n--) *p++ = 0; }
static void be32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static uint32_t rbe32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }
static uint8_t bcd(unsigned v) { return ((v / 10) << 4) | (v % 10); }

static uint32_t irq_on(void)
{
    uint32_t c;
    __asm__ volatile("mrs %0, cpsr" : "=r"(c));
    __asm__ volatile("msr cpsr_c, %0" :: "r"(c & ~0x80u) : "memory");
    return c;
}
static void irq_restore(uint32_t c) { __asm__ volatile("msr cpsr_c, %0" :: "r"(c) : "memory"); }
static void wfi(void) { __asm__ volatile("mcr p15, 0, %0, c7, c0, 4" :: "r"(0) : "memory"); }

/* wait until (status & mask) == want; the first ~2 ms spin, then sleep between
   timer ticks (IRQs on meanwhile); 0 = ok, -1 = timeout */
int atapi_wait(uint8_t mask, uint8_t want, uint32_t ticks)
{
    for (int i = 0; i < 2000; i++) if ((IOP(P_CMD) & mask) == want) return 0;
    uint32_t c = irq_on();
    uint32_t t0 = TICKS;
    int r = -1;
    for (;;) {
        if ((IOP(P_CMD) & mask) == want) { r = 0; break; }
        if (TICKS - t0 > ticks) break;
        wfi();
    }
    irq_restore(c);
    return r;
}

static void drive_reset(void)
{
    IOP(P_CTL) = 0x06;                  /* SRST + nIEN */
    for (volatile int i = 0; i < 200; i++) (void)IOP(P_CTL);
    IOP(P_CTL) = 0x02;
    atapi_wait(S_BSY, 0, 91);
}

/*
 * One packet command. out = 0: data in to buf (at most len bytes kept), 1: data out
 * from buf. Returns 0, or -1 (timeout / no drive), or 1 with armcd_sense set.
 */
static int packet_once(const uint8_t *cdb, void *buf, unsigned len, int out)
{
    if (atapi_wait(S_BSY, 0, 91)) { drive_reset(); return -1; }
    IOP(P_DH) = 0xA0;
    IOP(P_ERR) = 0;
    IOP(P_BCL) = 0xFE; IOP(P_BCH) = 0xFF;
    IOP(P_CMD) = 0xA0;
    if (atapi_wait(S_BSY | S_DRQ, S_DRQ, 91)) { drive_reset(); return -1; }
    for (int i = 0; i < 12; i += 2) IOW(P_DATA) = cdb[i] | (cdb[i + 1] << 8);
    uint8_t *p = buf;
    unsigned done = 0;
    for (;;) {
        /* data may take a while (a disc spinning up, a streamed image): 60 s */
        if (atapi_wait(S_BSY, 0, 1092)) { drive_reset(); return -1; }
        uint8_t st = IOP(P_CMD);
        if (!(st & S_DRQ)) {
            if (st & S_ERR) return 1;
            return 0;
        }
        unsigned n = IOP(P_BCL) | (IOP(P_BCH) << 8);
        if (IOP(P_REASON) & 2) {                /* data in */
            if (!((uint32_t)p & 3) && !(n & 3) && done + n <= len) {
                uint32_t *w = (uint32_t *)(p + done);
                for (unsigned i = 0; i < n; i += 4) *w++ = IOL(P_DATA);
                done += n;
            } else {
                for (unsigned i = 0; i < n; i += 2) {
                    uint16_t v = IOW(P_DATA);
                    if (done < len) p[done] = v;
                    if (done + 1 < len) p[done + 1] = v >> 8;
                    done += 2;
                }
            }
        } else {                                 /* data out */
            for (unsigned i = 0; i < n; i += 2) {
                uint16_t v = (done < len ? p[done] : 0) | ((done + 1 < len ? p[done + 1] : 0) << 8);
                IOW(P_DATA) = v;
                done += 2;
            }
        }
    }
}

static void request_sense(void)
{
    uint8_t c[12], s[18];
    zero(c, 12); zero(s, 18);
    c[0] = 0x03; c[4] = 18;
    if (packet_once(c, s, 18, 0) == 0) armcd_sense = (s[2] & 15) << 16 | s[12] << 8 | s[13];
    else armcd_sense = 0x020400;
}

static void media_change_seen(void)
{
    media_changed = 1; toc_ok = 0; paused = 0; play_start = play_end = 0;
}

/* with retries: unit attention (media changed / reset) and "becoming ready" */
int atapi_packet(const uint8_t *cdb, void *buf, unsigned len, int out)
{
    uint32_t t0 = TICKS;
    for (int tries = 0; tries < 400; tries++) {
        int r = packet_once(cdb, buf, len, out);
        if (r <= 0) { if (r == 0) armcd_sense = 0; else armcd_sense = 0x020400; return r; }
        request_sense();
        uint32_t k = armcd_sense >> 16, asc = (armcd_sense >> 8) & 0xFF;
        if (k == 6) { if (asc == 0x28 || asc == 0x29) media_change_seen(); continue; }
        if (k == 2 && asc == 0x3A) {             /* no medium */
            if (disc_known) { disc_known = 0; media_change_seen(); }
            return 1;
        }
        if (k == 2 && asc == 0x04 && TICKS - t0 < 182) {  /* becoming ready: up to 10 s */
            uint32_t c = irq_on(); uint32_t t = TICKS; while (TICKS == t) wfi(); irq_restore(c);
            continue;
        }
        return 1;
    }
    return 1;
}

/* driver error code for the last failure */
static unsigned sense_err(void)
{
    uint32_t k = armcd_sense >> 16, asc = (armcd_sense >> 8) & 0xFF;
    if (armcd_sense == 0) return E_GENERAL;
    if (k == 2) return E_NOTREADY;
    if (k == 3) return asc == 0x02 ? E_SEEK : E_READ;
    if (k == 5) return (asc == 0x21 || asc == 0x64) ? E_NOTFOUND : asc == 0x20 ? E_BADCMD : E_GENERAL;
    if (k == 6) return E_DISKCHG;
    return E_GENERAL;
}

static uint8_t cdb[12] RES;
static uint8_t *mk(uint8_t op) { zero(cdb, 12); cdb[0] = op; return cdb; }

/* ------------------------------------------------------------ disc info */

static int read_toc(void)
{
    if (toc_ok) return 0;
    static uint8_t t[4 + 101 * 8] RES;
    uint8_t *c = mk(0x43);
    c[7] = sizeof t >> 8; c[8] = sizeof t & 0xFF;          /* LBA form, format 0 */
    if (atapi_packet(c, t, sizeof t, 0)) return -1;
    unsigned n = ((t[0] << 8) | t[1]) - 2;
    toc_first = t[2]; toc_last = t[3];
    for (unsigned o = 4; o + 8 <= 4 + n && o + 8 <= sizeof t; o += 8) {
        uint8_t no = t[o + 2];
        uint32_t lba = rbe32(t + o + 4);
        if (no == 0xAA) toc_leadout = lba;
        else if (no < 100) { toc_start[no] = lba; toc_ctl[no] = t[o + 1]; }
    }
    disc_known = 1;
    toc_ok = 1;
    return 0;
}

/* current audio status and position: READ SUB-CHANNEL (MSF=0, format 1) into scratch */
static int subq(void)
{
    uint8_t *c = mk(0x42);
    c[2] = 0x40; c[3] = 1; c[8] = 16;
    zero(scratch, 16);
    return atapi_packet(c, scratch, 16, 0);
}
static int playing(void) { return subq() == 0 && scratch[1] == 0x11; }

static int mode_sense_0e(uint8_t *pg)          /* 16 bytes of page 0Eh */
{
    uint8_t *c = mk(0x5A);
    c[2] = 0x0E; c[8] = 24;
    zero(scratch, 24);
    if (atapi_packet(c, scratch, 24, 0)) return -1;
    memcpy(pg, scratch + 8, 16);
    return 0;
}

static uint32_t to_lba(uint8_t mode, uint32_t a) { return mode ? rb_to_lba(a) : a; }

/* ------------------------------------------------------------ IOCTL */

static unsigned ioctl_in(struct cdreq_ioctl *q)
{
    uint8_t *b = (uint8_t *)q->buf;
    switch (b[0]) {
    case IOI_DEVHDR: wr32le(b + 1, (uint32_t)&armcd_header); return 0;
    case IOI_HEAD:
        if (subq()) return sense_err();
        { uint32_t lba = rbe32(scratch + 8); wr32le(b + 2, b[1] ? lba_to_rb(lba) : lba); }
        return 0;
    case IOI_AUDIOCHAN: {
        uint8_t pg[16];
        if (mode_sense_0e(pg)) return sense_err();
        for (int i = 0; i < 8; i += 2) {         /* page 0Eh port selection is a channel mask */
            uint8_t m = pg[8 + i], n = i / 2;
            if (m) for (n = 0; n < 3 && !(m & (1 << n)); n++) ;
            b[1 + i] = n; b[2 + i] = pg[9 + i];
        }
        return 0;
    }
    case IOI_DRVBYTES: b[1] = 0; return 0;
    case IOI_DEVSTAT: {
        uint32_t s = DS_RAW | DS_AUDIO | DS_CHANMANIP | DS_REDBOOK | (locked ? 0 : DS_UNLOCKED);
        uint8_t *c = mk(0x4A);                   /* GET EVENT STATUS: tray and medium */
        c[1] = 1; c[4] = 0x10; c[8] = 8;
        zero(scratch, 8);
        if (atapi_packet(c, scratch, 8, 0) == 0) {
            if (scratch[5] & 1) s |= DS_DOOROPEN;
            if (!(scratch[5] & 2)) s |= DS_NODISC;
            if (scratch[4] == 2 || scratch[4] == 3) media_change_seen();
        } else s |= DS_NODISC;
        wr32le(b + 1, s);
        return 0;
    }
    case IOI_SECTSIZE: wr16le(b + 2, b[1] ? 2352 : 2048); return 0;
    case IOI_VOLSIZE: {
        uint8_t *c = mk(0x25);
        if (atapi_packet(c, scratch, 8, 0)) return sense_err();
        wr32le(b + 1, rbe32(scratch) + 1);
        return 0;
    }
    case IOI_MEDIACHG: {
        uint8_t *c = mk(0x00);                   /* TEST UNIT READY notices a change */
        int r = atapi_packet(c, 0, 0, 0);
        if (media_changed) { b[1] = 0xFF; media_changed = 0; }
        else b[1] = r ? 0 : 1;
        return 0;
    }
    case IOI_DISKINFO:
        if (read_toc()) return sense_err();
        b[1] = toc_first; b[2] = toc_last; wr32le(b + 3, lba_to_rb(toc_leadout));
        return 0;
    case IOI_TRACKINFO: {
        if (read_toc()) return sense_err();
        unsigned t = b[1];
        if (t < toc_first || t > toc_last) return E_NOTFOUND;
        wr32le(b + 2, lba_to_rb(toc_start[t]));
        b[6] = ((toc_ctl[t] & 0x0F) << 4) | (toc_ctl[t] >> 4);     /* CONTROL << 4 | ADR */
        return 0;
    }
    case IOI_QCHAN: {
        uint8_t *c = mk(0x42);
        c[1] = 2; c[2] = 0x40; c[3] = 1; c[8] = 16;
        zero(scratch, 16);
        if (atapi_packet(c, scratch, 16, 0)) return sense_err();
        b[1] = ((scratch[5] & 0x0F) << 4) | (scratch[5] >> 4);
        b[2] = bcd(scratch[6]); b[3] = scratch[7];
        b[4] = scratch[13]; b[5] = scratch[14]; b[6] = scratch[15]; b[7] = 0;
        b[8] = scratch[9]; b[9] = scratch[10]; b[10] = scratch[11];
        return 0;
    }
    case IOI_UPC: {
        uint8_t *c = mk(0x42);
        c[2] = 0x40; c[3] = 2; c[8] = 24;
        zero(scratch, 24);
        if (atapi_packet(c, scratch, 24, 0)) return sense_err();
        if (!(scratch[8] & 0x80)) return E_NOTFOUND;
        b[1] = 0x02;
        for (int i = 0; i < 7; i++) {
            uint8_t hi = scratch[9 + 2 * i], lo = 2 * i + 1 < 13 ? scratch[10 + 2 * i] : '0';
            b[2 + i] = ((hi - '0') << 4) | (lo - '0');
        }
        b[9] = 0; b[10] = 0;
        return 0;
    }
    case IOI_AUDIOSTAT:
        if (subq()) return sense_err();
        wr16le(b + 1, paused ? 1 : 0);
        wr32le(b + 3, lba_to_rb(play_start)); wr32le(b + 7, lba_to_rb(play_end));
        return 0;
    }
    return E_BADCMD;
}

static unsigned ioctl_out(struct cdreq_ioctl *q)
{
    uint8_t *b = (uint8_t *)q->buf, *c;
    switch (b[0]) {
    case IOO_EJECT:
        if (locked) return E_GENERAL;
        c = mk(0x1B); c[4] = 0x02;
        paused = 0;
        return atapi_packet(c, 0, 0, 0) ? sense_err() : 0;
    case IOO_CLOSETRAY:
        c = mk(0x1B); c[4] = 0x03;
        return atapi_packet(c, 0, 0, 0) ? sense_err() : 0;
    case IOO_LOCK:
        c = mk(0x1E); c[4] = b[1] ? 1 : 0;
        if (atapi_packet(c, 0, 0, 0)) return sense_err();
        locked = b[1] ? 1 : 0;
        return 0;
    case IOO_RESET:
        drive_reset(); paused = 0; toc_ok = 0;
        return 0;
    case IOO_AUDIOCHAN: {
        uint8_t pg[24];
        zero(pg, 24);
        if (mode_sense_0e(pg + 8)) return sense_err();
        pg[8] = 0x0E; pg[9] = 0x0E;
        for (int i = 0; i < 8; i += 2) { pg[16 + i] = 1 << (b[1 + i] & 3); pg[17 + i] = b[2 + i]; }
        c = mk(0x55); c[1] = 0x10; c[8] = 24;
        return atapi_packet(c, pg, 24, 1) ? sense_err() : 0;
    }
    }
    return E_BADCMD;
}

/* ------------------------------------------------------------ reads, audio */

static unsigned read_long(struct cdreq_read *q)
{
    uint32_t lba = to_lba(q->addrmode, q->start);
    uint8_t *p = (uint8_t *)q->buf;
    unsigned left = q->count, size = q->mode ? 2352 : 2048;
    if (q->addrmode > 1 || q->mode > 1) return E_BADCMD;
    paused = 0;
    while (left) {
        unsigned n = left > 32 ? 32 : left;
        uint8_t *c;
        if (q->mode) {                           /* raw: READ CD, CD-DA (audio extraction) */
            c = mk(0xBE); c[1] = 0x04; be32(c + 2, lba); c[8] = n; c[9] = 0x10;
        } else {
            c = mk(0x28); be32(c + 2, lba); c[8] = n;
        }
        if (atapi_packet(c, p, n * size, 0)) { q->count -= left; return sense_err(); }
        p += n * size; lba += n; left -= n;
    }
    return 0;
}

static unsigned play(struct cdreq_play *q)
{
    uint32_t s = to_lba(q->addrmode, q->start), n = q->count;
    if (q->addrmode > 1) return E_BADCMD;
    uint8_t *c = mk(0xA5);
    be32(c + 2, s); be32(c + 6, n);
    if (atapi_packet(c, 0, 0, 0)) return sense_err();
    play_start = s; play_end = s + n; paused = 0;
    return 0;
}

static unsigned stop(void)
{
    uint8_t *c;
    if (playing()) {                             /* first STOP pauses (RESUME continues) */
        c = mk(0x4B);
        if (atapi_packet(c, 0, 0, 0)) return sense_err();
        paused = 1;
        return 0;
    }
    c = mk(0x4E);                                /* a second one stops for good */
    atapi_packet(c, 0, 0, 0);
    paused = 0; play_start = play_end = 0;
    return 0;
}

static unsigned resume(void)
{
    if (!paused) return E_GENERAL;
    uint8_t *c = mk(0x4B); c[8] = 1;
    if (atapi_packet(c, 0, 0, 0)) return sense_err();
    paused = 0;
    return 0;
}

/* ------------------------------------------------------------ entry points */

void armcd_strategy(struct cdreq *r) { req = r; }

void armcd_interrupt(void)
{
    struct cdreq *r = req;
    unsigned e = 0;
    if (r->cmd != C_INIT && r->unit != 0) { r->status = ST_DONE | ST_ERROR | E_UNIT; return; }
    switch (r->cmd) {
    case C_INIT: armcd_init((struct cdreq_init *)r); return;
    case C_IOCTL_IN: e = ioctl_in((struct cdreq_ioctl *)r); break;
    case C_IOCTL_OUT: e = ioctl_out((struct cdreq_ioctl *)r); break;
    case C_INFLUSH: case C_OUTFLUSH: case C_OPEN: case C_CLOSE: break;
    case C_READ_LONG: e = read_long((struct cdreq_read *)r); break;
    case C_READ_PREFETCH: case C_SEEK: {
        struct cdreq_read *q = (struct cdreq_read *)r;
        uint8_t *c = mk(0x2B); be32(c + 2, to_lba(q->addrmode, q->start));
        if (atapi_packet(c, 0, 0, 0)) e = sense_err();
        paused = 0;
        break;
    }
    case C_PLAY: e = play((struct cdreq_play *)r); break;
    case C_STOP: e = stop(); break;
    case C_RESUME: e = resume(); break;
    case C_WRITE_LONG: case C_WRITE_VERIFY: e = E_WRPROT; break;
    default: e = E_BADCMD; break;
    }
    uint16_t st = ST_DONE;
    if (e) st |= ST_ERROR | e;
    /* BUSY while audio plays (a status query of its own after the command) */
    if (playing()) st |= ST_BUSY;
    r->status = st;
}
