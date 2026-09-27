/*
 * himem.c - HIMEM.SYS, the extended memory manager (XMS 2.0) for ARM-DOS
 * (ARCH.md 10, 15, 16).  An installable character device driver
 * ("XMSXXXX0"): CONFIG.SYS DEVICE=HIMEM.SYS [/NUMHANDLES=n].
 *
 * Extended memory runs from 1 MB to 16 MB, but the first 64 KB above 1 MB
 * (the "HMA") holds the ARM/AT BIOS's data and stacks, so XMS blocks come from
 * 0x110000-0xFFFFFF (15,296 KB).  The HMA is reported as existing but in use.
 *
 * INT 2Fh AX=4300h -> AL=80h; AX=4310h -> BX = flat pointer to the XMS entry,
 * called with BLX, AH = function, registers per ARCH.md 5 (results in r0-r6).
 */
#include "klib.h"

#define XMS_BASE    0x110000u
#define XMS_END     0x1000000u
#define MAXHANDLES  128
/* KB of XMS: INT 15h AH=88h at init (the BIOS counts the installed SIMMs; everything from
   the end of the HMA up), at most XMS_END - XMS_BASE */
static uint32_t xms_kb = (XMS_END - XMS_BASE) / 1024;
#define XMS_KB      xms_kb

struct xhandle {
    uint16_t used, locks;
    uint32_t base;          /* KB offset from XMS_BASE */
    uint32_t size;          /* KB */
};

/* the handle table lives right after our bss, as long as /NUMHANDLES says */
static struct xhandle *handles;
static int nhandles = 32;
static int_handler old2f, old15;

static void strategy(struct reqhdr *r);
static void interrupt(void);
void xms_entry(void);

__attribute__((section(".devhdr"), used))
struct devhdr himem_header = {
    DEV_END, DEVA_CHAR | DEVA_NONIBM, 0, strategy, interrupt, "XMSXXXX0"
};

/* ------------------------------------------------------ allocation */

/* largest free gap (KB) and total free (KB) */
static void free_info(uint32_t *largest, uint32_t *total)
{
    uint32_t pos = 0, big = 0, sum = 0;
    for (;;) {
        /* the used block with the lowest base >= pos */
        struct xhandle *next = 0;
        for (int i = 0; i < nhandles; i++)
            if (handles[i].used && handles[i].size && handles[i].base >= pos && (!next || handles[i].base < next->base))
                next = &handles[i];
        uint32_t end = next ? next->base : XMS_KB;
        uint32_t gap = end - pos;
        if (gap > big) big = gap;
        sum += gap;
        if (!next) break;
        pos = next->base + next->size;
    }
    *largest = big;
    *total = sum;
}

/* first fit; returns the base or 0xFFFFFFFF */
static uint32_t find_gap(uint32_t kb, struct xhandle *ignore)
{
    uint32_t pos = 0;
    for (;;) {
        struct xhandle *next = 0;
        for (int i = 0; i < nhandles; i++)
            if (handles[i].used && handles[i].size && &handles[i] != ignore && handles[i].base >= pos &&
                (!next || handles[i].base < next->base))
                next = &handles[i];
        uint32_t end = next ? next->base : XMS_KB;
        if (end - pos >= kb) return pos;
        if (!next) return 0xFFFFFFFFu;
        pos = next->base + next->size;
    }
}

static struct xhandle *handle_of(uint32_t dx)
{
    unsigned i = dx & 0xFFFF;
    if (i < 1 || i > (unsigned)nhandles || !handles[i - 1].used) return 0;
    return &handles[i - 1];
}

static uint8_t *addr_of(struct xhandle *h, uint32_t off) { return (uint8_t *)(XMS_BASE + h->base * 1024 + off); }

/* ------------------------------------------------------ the entry */

/* r[0]=AX r[1]=BX r[2]=CX r[3]=DX r[4]=SI r[5]=DI r[6]=BP */
#define FAIL(code) do { r[0] = 0; r[1] = (r[1] & ~0xFFu) | (code); return; } while (0)
#define OK()       do { r[0] = 1; r[1] &= ~0xFFu; } while (0)

void xms_dispatch(uint32_t *r)
{
    int fn = (r[0] >> 8) & 0xFF;
    struct xhandle *h;
    uint32_t big, total;
    switch (fn) {
    case 0x00:                              /* version */
        r[0] = 0x0200;
        r[1] = 0x0204;
        r[3] = 1;                           /* the HMA exists ... */
        return;
    case 0x01: FAIL(0x91);                  /* ... but the BIOS uses it */
    case 0x02: FAIL(0x93);
    case 0x03: case 0x05: OK(); return;     /* A20 is always on: nothing to do */
    case 0x04: case 0x06: FAIL(0x94);       /* and it stays on */
    case 0x07: r[0] = 1; r[1] &= ~0xFFu; return;
    case 0x08:
        free_info(&big, &total);
        r[0] = big;
        r[3] = total;
        r[1] = (r[1] & ~0xFFu) | (total ? 0 : 0xA0);
        return;
    case 0x09: {
        uint32_t kb = r[3] & 0xFFFF;
        int slot = -1;
        for (int i = 0; i < nhandles; i++) if (!handles[i].used) { slot = i; break; }
        if (slot < 0) FAIL(0xA1);
        uint32_t base = kb ? find_gap(kb, 0) : 0;
        if (base == 0xFFFFFFFFu) FAIL(0xA0);
        handles[slot].used = 1;
        handles[slot].locks = 0;
        handles[slot].base = base;
        handles[slot].size = kb;
        r[3] = slot + 1;
        OK();
        return;
    }
    case 0x0A:
        if (!(h = handle_of(r[3]))) FAIL(0xA2);
        if (h->locks) FAIL(0xAB);
        h->used = 0;
        OK();
        return;
    case 0x0B: {
        const uint8_t *m = (const uint8_t *)r[4];
        uint32_t len = m[0] | (m[1] << 8) | (m[2] << 16) | ((uint32_t)m[3] << 24);
        unsigned sh = m[4] | (m[5] << 8);
        uint32_t so = m[6] | (m[7] << 8) | (m[8] << 16) | ((uint32_t)m[9] << 24);
        unsigned dh = m[10] | (m[11] << 8);
        uint32_t dof = m[12] | (m[13] << 8) | (m[14] << 16) | ((uint32_t)m[15] << 24);
        if (len & 1) FAIL(0xA7);
        uint8_t *src, *dst;
        if (sh == 0) src = (uint8_t *)so;
        else {
            if (!(h = handle_of(sh))) FAIL(0xA3);
            if (so > h->size * 1024 || len > h->size * 1024 - so) FAIL(0xA4);
            src = addr_of(h, so);
        }
        if (dh == 0) dst = (uint8_t *)dof;
        else {
            if (!(h = handle_of(dh))) FAIL(0xA5);
            if (dof > h->size * 1024 || len > h->size * 1024 - dof) FAIL(0xA6);
            dst = addr_of(h, dof);
        }
        memmove(dst, src, len);
        OK();
        return;
    }
    case 0x0C:
        if (!(h = handle_of(r[3]))) FAIL(0xA2);
        if (h->locks == 255) FAIL(0xAC);
        h->locks++;
        {
            uint32_t a = (uint32_t)addr_of(h, 0);
            r[3] = a >> 16;
            r[1] = a & 0xFFFF;
            r[0] = 1;
        }
        return;
    case 0x0D:
        if (!(h = handle_of(r[3]))) FAIL(0xA2);
        if (!h->locks) FAIL(0xAA);
        h->locks--;
        OK();
        return;
    case 0x0E: {
        if (!(h = handle_of(r[3]))) FAIL(0xA2);
        int freeh = 0;
        for (int i = 0; i < nhandles; i++) if (!handles[i].used) freeh++;
        r[1] = (h->locks << 8) | (freeh > 255 ? 255 : freeh);
        r[3] = h->size;
        r[0] = 1;
        return;
    }
    case 0x0F: {
        if (!(h = handle_of(r[3]))) FAIL(0xA2);
        if (h->locks) FAIL(0xAB);
        uint32_t kb = r[1] & 0xFFFF;
        if (kb <= h->size) { h->size = kb; OK(); return; }
        /* grow in place, else move */
        uint32_t oldsize = h->size;
        h->size = 0;
        uint32_t base = find_gap(kb, h);
        if (base == 0xFFFFFFFFu) { h->size = oldsize; FAIL(0xA0); }
        if (base != h->base) memmove((void *)(XMS_BASE + base * 1024), addr_of(h, 0), oldsize * 1024);
        h->base = base;
        h->size = kb;
        OK();
        return;
    }
    case 0x10:
        r[3] = 0;
        FAIL(0xB1);                         /* no upper memory blocks */
    case 0x11: FAIL(0xB2);
    default:
        FAIL(0x80);
    }
}

/* --------------------------------------------------------- INT 2Fh */

static void int2f(struct armregs *f)
{
    if (AX(f) == 0x4300) { set_al(f, 0x80); return; }
    if (AX(f) == 0x4310) { f->r1 = (uint32_t)xms_entry; f->r8 = 0; return; }
    if (old2f) old2f(f);
}

/* INT 15h AH=88h: extended memory now belongs to the XMS driver (0 KB
   left for programs that take it "raw"), as Microsoft's HIMEM does */
static void int15(struct armregs *f)
{
    if (AH(f) == 0x88) { set_ax(f, 0); set_cf(f, 0); return; }
    if (old15) old15(f); else set_cf(f, 1);
}

/* ------------------------------------------------------ the driver */

static struct reqhdr *req;
static void strategy(struct reqhdr *r) { req = r; }

static int dos21(struct armregs *r) { return svc21(r); }

static void print(const char *s)
{
    struct armregs r = { 0 };
    r.r0 = 0x4000;
    r.r1 = 1;
    r.r2 = strlen(s);
    r.r3 = (uint32_t)s;
    dos21(&r);
}

extern char __bss_end__[];

static void init(struct req_init *q)
{
    const char *cmd = (const char *)q->arg;
    char buf[160];
    print("\r\nHIMEM: ARM-DOS XMS Driver, Version 2.04 - 09/24/88\r\n"
          "XMS Specification Version 2.0\r\n"
          "Copyright 1988 Europa Micro Systems\r\n\r\n");
    /* already one? */
    struct armregs r = { 0 };
    r.r0 = 0x4300;
    kint(0x2F, &r);
    if ((r.r0 & 0xFF) == 0x80) {
        print("ERROR: An Extended Memory Manager is already installed.\r\n"
              "XMS Driver not installed.\r\n\r\n");
        q->brk = (uint32_t)&himem_header;
        q->units = 0;
        return;
    }
    /* /NUMHANDLES=n */
    for (const char *p = cmd; p && *p && *p != '\r'; p++) {
        if (!strncmp(p, "/NUMHANDLES=", 12)) {
            int n = 0;
            for (p += 12; *p >= '0' && *p <= '9'; p++) n = n * 10 + (*p - '0');
            if (n < 1) n = 1;
            if (n > MAXHANDLES) n = MAXHANDLES;
            nhandles = n;
            break;
        }
    }
    /* how much extended memory is there? (before we hook INT 15h and hide it) */
    memset(&r, 0, sizeof r);
    r.r0 = 0x8800;
    kint(0x15, &r);
    if (!(r.cpsr & 0x20000000u) && (r.r0 & 0xFFFF) < xms_kb) xms_kb = r.r0 & 0xFFFF;
    handles = (struct xhandle *)(((uint32_t)__bss_end__ + 3) & ~3u);
    memset(handles, 0, nhandles * sizeof *handles);
    /* hook INT 2Fh */
    memset(&r, 0, sizeof r);
    r.r0 = 0x352F;
    dos21(&r);
    old2f = (int_handler)r.r1;
    memset(&r, 0, sizeof r);
    r.r0 = 0x252F;
    r.r3 = (uint32_t)int2f;
    dos21(&r);
    memset(&r, 0, sizeof r);
    r.r0 = 0x3515;
    dos21(&r);
    old15 = (int_handler)r.r1;
    memset(&r, 0, sizeof r);
    r.r0 = 0x2515;
    r.r3 = (uint32_t)int15;
    dos21(&r);
    print("The High Memory Area is in use by the ARM/AT BIOS.\r\n");
    ksnprintf(buf, sizeof buf, "%uK of extended memory available, %d handles.\r\n\r\n", XMS_KB, nhandles);
    print(buf);
    q->brk = (uint32_t)(handles + nhandles);
}

static void interrupt(void)
{
    struct reqhdr *r = req;
    r->status = RS_DONE;
    switch (r->cmd) {
    case CMD_INIT: init((struct req_init *)r); break;
    case CMD_READ: ((struct req_rw *)r)->count = 0; break;
    case CMD_WRITE: case CMD_WRITEV: break;
    case CMD_NDREAD: r->status |= RS_BUSY; break;
    case CMD_INSTAT: case CMD_OUTSTAT: case CMD_INFLUSH: case CMD_OUTFLUSH:
    case CMD_OPEN: case CMD_CLOSE:
        break;
    default:
        r->status |= RS_ERROR | DE_BADCMD;
    }
}
