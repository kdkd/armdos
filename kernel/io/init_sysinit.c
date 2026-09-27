/*
 * init_sysinit.c - SYSINIT (BIOS/SYSINIT1.ASM): runs once at 0x90100.
 *
 *   1. start the resident drivers (CON, CLOCK$, the disks)
 *   2. find ARMDOS.SYS in the boot drive's root directory, read it with the
 *      block driver (a small FAT reader of our own, as DOS's is), place it
 *      right after IO.SYS's resident part, apply its AR1 relocations and call
 *      its initialisation entry
 *   3. open CON/AUX/PRN as handles 0-4, process CONFIG.SYS (init_config.c)
 *   4. build the DOS tables and the memory arena, run INSTALL= programs
 *   5. EXEC the shell: "\COMMAND.COM /P" unless CONFIG.SYS says otherwise
 */
#include "iosys.h"
#include "init.h"

struct sysinit S;

/* ------------------------------------------------------------- output */

void sys_puts(const char *s)
{
    if (S.handles_open) {
        struct armregs r = { 0 };
        r.r0 = 0x4000;
        r.r1 = 1;
        r.r2 = strlen(s);
        r.r3 = (uint32_t)s;
        svc21(&r);
        return;
    }
    for (; *s; s++) {
        struct armregs r = { 0 };
        r.r0 = 0x0E00 | (uint8_t)*s;
        r.r1 = 7;
        kint(0x10, &r);
    }
}

void sys_printf(const char *fmt, ...)
{
    char buf[200];
    va_list ap;
    va_start(ap, fmt);
    kvsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    sys_puts(buf);
}

__attribute__((noreturn)) void sys_halt(void)
{
    irq_on();
    for (;;) cpu_wfi();
}

/* -------------------------------------------- reading ARMDOS.SYS early */

static int blk_read(int unit, uint32_t sector, int count, void *buf)
{
    struct req_rw q;
    memset(&q, 0, sizeof q);
    q.h.len = sizeof q;
    q.h.unit = unit;
    q.h.cmd = CMD_READ;
    q.addr = (uint32_t)buf;
    q.count = count;
    q.start = 0xFFFF;
    q.start32 = sector;
    disk_dev.strategy(&q.h);
    disk_dev.interrupt();
    return (q.h.status & RS_ERROR) ? -1 : 0;
}

static uint8_t secbuf[512] __attribute__((aligned(4)));
static uint8_t fatbuf[512] __attribute__((aligned(4)));
static uint32_t fatbuf_sec = 0xFFFFFFFF;

/* find a file in the root directory of `unit` and read it to dest */
static int read_root_file(int unit, const struct bpb *b, const char *name11, uint8_t *dest, uint32_t max, uint32_t *psize)
{
    uint32_t root = b->reserved + (uint32_t)b->nfats * b->fat_secs;
    uint32_t rootsecs = ((uint32_t)b->root_ents * 32 + 511) / 512;
    uint32_t data = root + rootsecs;
    uint32_t total = b->total16 ? b->total16 : b->total32;
    uint32_t clusters = (total - data) / b->sec_per_clus;
    int fat16 = clusters + 1 >= 4086;
    uint32_t cl = 0, size = 0;
    int found = 0;

    for (uint32_t s = 0; s < rootsecs && !found; s++) {
        if (blk_read(unit, root + s, 1, secbuf)) return -1;
        for (int i = 0; i < 16; i++) {
            const struct dirent *d = (const struct dirent *)(secbuf + i * 32);
            if (d->name[0] == 0) { s = rootsecs; break; }
            if (!memcmp(d->name, name11, 11) && !(d->attr & (ATTR_DIR | ATTR_VOLUME))) {
                cl = d->cluster; size = d->size; found = 1; break;
            }
        }
    }
    if (!found || size > max) return -1;
    uint32_t csize = b->sec_per_clus * 512u, got = 0;
    while (got < size) {
        if (cl < 2 || cl > clusters + 1) return -1;
        if (blk_read(unit, data + (cl - 2) * b->sec_per_clus, b->sec_per_clus, dest + got)) return -1;
        got += csize;
        /* next cluster */
        uint32_t off = fat16 ? cl * 2 : cl + cl / 2;
        uint32_t sec = b->reserved + off / 512;
        uint8_t v[2];
        for (int k = 0; k < 2; k++) {
            uint32_t sk = b->reserved + (off + k) / 512;
            if (sk != fatbuf_sec) { if (blk_read(unit, sk, 1, fatbuf)) return -1; fatbuf_sec = sk; }
            v[k] = fatbuf[(off + k) % 512];
        }
        (void)sec;
        uint32_t n = v[0] | (v[1] << 8);
        if (!fat16) n = (cl & 1) ? n >> 4 : n & 0xFFF;
        cl = n;
    }
    *psize = size;
    return 0;
}

/* place an AR1 image (file in memory at f) at base: returns entry or 0 */
uint32_t ar1_place(const uint8_t *f, uint32_t fsize, uint32_t base, uint32_t limit, uint32_t *end)
{
    const struct { char sig[4]; uint16_t hdrsize, flags; uint32_t image_off, image_size, bss_size, stack_size,
                   entry, reloc_off, reloc_count, min_extra, max_extra; } *h = (const void *)f;
    if (fsize < 64 || memcmp(h->sig, "AR1", 4)) return 0;
    if (h->image_off + h->image_size > fsize || h->reloc_off + h->reloc_count * 4 > fsize) return 0;
    uint32_t top = base + h->image_size + h->bss_size;
    if (top > limit) return 0;
    memmove((void *)base, f + h->image_off, h->image_size);
    const uint8_t *rel = f + h->reloc_off;
    for (uint32_t i = 0; i < h->reloc_count; i++) {
        uint32_t off = rel[i * 4] | (rel[i * 4 + 1] << 8) | (rel[i * 4 + 2] << 16) | ((uint32_t)rel[i * 4 + 3] << 24);
        if (off + 4 > h->image_size) continue;
        uint8_t *p = (uint8_t *)base + off;
        uint32_t v = (p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24)) + base;
        p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24;
    }
    memset((void *)(base + h->image_size), 0, h->bss_size);
    *end = top;
    return base + h->entry;
}

/* ---------------------------------------------------------------- main */

static int dos21(struct armregs *r) { return svc21(r); }

static int open_std(void)
{
    struct armregs r = { 0 };
    static const char con[] = "CON", aux[] = "AUX", prn[] = "PRN";
    r.r0 = 0x3D02; r.r3 = (uint32_t)con;
    if (dos21(&r)) return -1;                   /* handle 0 */
    int h = r.r0 & 0xFFFF;
    memset(&r, 0, sizeof r); r.r0 = 0x4500; r.r1 = h; dos21(&r);   /* 1 */
    memset(&r, 0, sizeof r); r.r0 = 0x4500; r.r1 = h; dos21(&r);   /* 2 */
    memset(&r, 0, sizeof r); r.r0 = 0x3D02; r.r3 = (uint32_t)aux; dos21(&r);    /* 3 */
    memset(&r, 0, sizeof r); r.r0 = 0x3D02; r.r3 = (uint32_t)prn; dos21(&r);    /* 4 */
    return 0;
}

void sys_reopen_std(void)
{
    for (int h = 0; h < 5; h++) {
        struct armregs r = { 0 };
        r.r0 = 0x3E00; r.r1 = h;
        dos21(&r);
    }
    S.handles_open = 0;
    if (open_std() == 0) S.handles_open = 1;
}

void sysinit_main(int drive, struct bpb *bootbpb)
{
    (void)bootbpb;
    S.bootdrive_bios = drive;

    /* SWITCHES=/K is read later; start with the enhanced keyboard functions */
    con_init(1);
    clock_init();
    S.nunits = disk_init(drive, 0);
    S.bootunit = disk_boot_unit();

    /* the boot unit's BPB */
    struct req_bpb qb;
    memset(&qb, 0, sizeof qb);
    qb.h.len = sizeof qb; qb.h.unit = S.bootunit; qb.h.cmd = CMD_BPB;
    qb.buf = (uint32_t)secbuf;
    disk_dev.strategy(&qb.h); disk_dev.interrupt();
    if ((qb.h.status & RS_ERROR) || !qb.bpb) goto nosys;
    struct bpb b;
    memcpy(&b, (void *)qb.bpb, sizeof b);

    /* the block driver's INIT gives DOS the BPB array */
    struct req_init qi;
    memset(&qi, 0, sizeof qi);
    qi.h.len = sizeof qi; qi.h.cmd = CMD_INIT;
    disk_dev.strategy(&qi.h); disk_dev.interrupt();

    uint32_t size;
    uint8_t *tmp = (uint8_t *)SYSINIT_TEMP;
    if (read_root_file(S.bootunit, &b, "ARMDOS  SYS", tmp, SYSINIT_BASE - SYSINIT_TEMP, &size)) goto nosys;

    uint32_t dosbase = ((uint32_t)__res_end + 15) & ~15u;
    uint32_t dosend;
    uint32_t entry = ar1_place(tmp, size, dosbase, SYSINIT_TEMP, &dosend);
    if (!entry) goto nosys;

    static struct dosinit di;
    di.magic = DOSINIT_MAGIC;
    di.devchain = &con_dev;
    di.con = &con_dev;
    di.clock = &clock_dev;
    di.block = &disk_dev;
    di.nunits = qi.units;
    di.bpbs = (struct bpb **)qi.arg;
    di.bootdrive = S.bootunit;
    di.io_start = 0x700;
    di.io_end = (uint32_t)__res_end;
    di.dos_start = dosbase;
    di.dos_end = dosend;
    di.sysinit_base = SYSINIT_BASE;
    ((void (*)(struct dosinit *))entry)(&di);
    S.api = di.api;
    if (!S.api || S.api->magic != DOSINIT_MAGIC) goto nosys;
    S.dos_end = dosend;

    if (open_std() == 0) S.handles_open = 1;

    sysinit_config();           /* CONFIG.SYS, drivers, tables, INSTALL=, the shell */
    /* not reached */

nosys:
    sys_puts("\r\nNon-System disk or disk error\r\nReplace and press any key when ready\r\n");
    {
        struct armregs r = { 0 };
        kint(0x16, &r);
        memset(&r, 0, sizeof r);
        kint(0x19, &r);
    }
    sys_halt();
}
