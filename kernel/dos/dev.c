/*
 * dev.c - calling device drivers, the INT 24h critical error machinery
 * (CTRLC.ASM HardErr/FATAL), sector I/O with retries, and the media check
 * that notices a diskette change and rebuilds the drive's DPB.
 */
#include "dos.h"

struct ctx *base_ctx(void);
static uint8_t aborting;

int devcall(struct devhdr *d, void *req)
{
    struct reqhdr *r = req;
    r->status = 0;
    d->strategy(r);
    d->interrupt();
    return r->status;
}

struct devhdr *find_chardev(const char *name8)
{
    for (struct devhdr *d = (struct devhdr *)((uint8_t *)&LOL + offsetof(struct lol, nul)); d && d != DEV_END; d = d->next)
        if ((d->attr & DEVA_CHAR) && !memcmp(d->name, name8, 8)) return d;
    return 0;
}

struct devhdr *con_device(void) { return LOL.con; }

/* INT 24h device error codes -> extended error (ErrMap24) + class/action/locus */
static void set_i24_exterr(int code, int drive)
{
    static const uint8_t cls[16][3] = {
        { 11, 7, 2 },  { 4, 5, 1 },   { 5, 7, 0xFF }, { 4, 5, 1 },
        { 11, 4, 2 },  { 4, 5, 1 },   { 5, 1, 2 },    { 11, 7, 2 },
        { 11, 4, 2 },  { 2, 7, 4 },   { 5, 4, 0xFF }, { 5, 4, 0xFF },
        { 13, 4, 0xFF },{ 13, 4, 0xFF },{ 13, 4, 0xFF },{ 11, 7, 2 },
    };
    int c = code & 15;
    exterr.code = 19 + c;
    if (c == 0x0D || c == 0x0E) exterr.code = 31;
    exterr.class_ = cls[c][0];
    exterr.action = cls[c][1];
    exterr.locus = cls[c][2] != 0xFF ? cls[c][2] : (drive >= 0 ? 2 : 4);
}

/*
 * A critical error.  ah = INT 24h AH (bit 7 char device/FAT, bits 1-2 area,
 * bit 0 write, bits 3-5 allowed fail/retry/ignore).  Returns 0 ignore,
 * 1 retry, 3 fail; an abort terminates the program and does not return.
 */
int crit_error(int ah, int drive, int code, struct devhdr *dev)
{
    set_i24_exterr(code, (ah & 0x80) ? -1 : drive);
    exterr.volptr = 0;
    int di = code & 0xFF;
    if (di > DE_GENERAL) di = DE_GENERAL;
    int allowed = ah & 0x38;
    int al;
    if (DV.errormode || no_i24) {
        al = 3;                         /* no INT 24h inside INT 24h (or when asked) */
    } else {
        struct ctx *b = base_ctx();
        struct armregs h;
        if (b) memcpy(&h, &b->orig, sizeof h); else memset(&h, 0, sizeof h);
        h.r0 = ((ah & 0xFF) << 8) | (drive & 0xFF);
        h.r5 = (h.r5 & ~0xFFFFu) | di;
        h.r4 = (uint32_t)dev;           /* BP:SI -> device header: SI holds it */
        h.r6 = (uint32_t)dev >> 4;
        h.cpsr &= ~CPSR_C;
        h.intno = 0x24;
        DV.errormode = 1;
        DV.indos--;
        int_handler hd = (int_handler)IVT[0x24];
        if (hd) hd(&h); else h.r0 = 3;
        DV.indos++;
        DV.errormode = 0;
        al = h.r0 & 0xFF;
    }
    switch (al) {
    case 0:
        if (allowed & 0x20) return 0;
        goto fail;
    case 1:
        if (allowed & 0x10) return 1;
        goto fail;
    case 3:
        if (allowed & 0x08) goto fail;
        /* FAIL not allowed -> abort */
        /* fall through */
    default:
        if (aborting) goto fail;
        {
            struct ctx *b = base_ctx();
            if (!b) goto fail;
            aborting = 1;
            terminate(b->f, 0, 2);
            aborting = 0;
            abort_to_base();
        }
    }
fail:
    fail_err = 1;
    return 3;
}

/* sector I/O through the drive's driver with INT 24h on errors */
int dsk_io(struct dpb *dpb, int write, uint32_t sector, unsigned count, void *buf, int area)
{
    uint8_t *p = buf;
    while (count) {
        struct req_rw q;
        memset(&q, 0, sizeof q);
        q.h.len = sizeof q;
        q.h.unit = dpb->unit;
        q.h.cmd = write ? (verify_on ? CMD_WRITEV : CMD_WRITE) : CMD_READ;
        q.media = dpb->media;
        q.addr = (uint32_t)p;
        q.count = count;
        if ((dpb->driver->attr & DEVA_32BIT) || sector >= 0xFFFF) {
            q.start = 0xFFFF;
            q.start32 = sector;
        } else q.start = sector;
        int st = devcall(dpb->driver, &q);
        if (!(st & RS_ERROR)) return 0;
        unsigned done = q.count < count ? q.count : 0;
        sector += done; p += done * 512; count -= done;
        /* Retry and Fail always; Ignore only for data (DOS 4: BUF.ASM, DISK*.ASM) */
        int ah = (area << 1) | (write ? 1 : 0) | 0x18 | (area == AREA_DATA ? 0x20 : 0);
        int r = crit_error(ah, dpb->drive, st & 0xFF, dpb->driver);
        if (r == 1) continue;
        if (r == 0) return 0;
        if ((st & 0xFF) == DE_WRPROT) invalidate_bufs(dpb->drive);
        return -(19 + (st & 0x0F));
    }
    return 0;
}

static uint8_t bpb_scratch[512] __attribute__((aligned(4)));

/* make sure the DPB describes the diskette in the drive (DOS FATREAD) */
int media_check(int drive)
{
    struct cds *c = get_cds(drive);
    struct dpb *d = c->dpb;
    int rebuild = d->first_access == 0xFF;
    if (!rebuild) {
        struct req_media q;
        memset(&q, 0, sizeof q);
        q.h.len = sizeof q;
        q.h.unit = d->unit;
        q.h.cmd = CMD_MEDIA;
        q.media = d->media;
        for (;;) {
            int st = devcall(d->driver, &q);
            if (!(st & RS_ERROR)) break;
            /* as FAT.ASM FATERR: "while trying to read the FAT", Retry/Fail */
            int r = crit_error(0x18 | (AREA_FAT << 1), drive, st & 0xFF, d->driver);
            if (r == 1) continue;
            if (r == 0) { q.changed = 1; break; }
            return -(19 + (st & 0x0F));
        }
        if (q.changed < 0) rebuild = 1;
        else if (q.changed == 0 && !bufs_dirty(drive)) rebuild = 1;
    }
    if (!rebuild) return 0;
    invalidate_bufs(drive);
    for (;;) {
        struct req_bpb q;
        memset(&q, 0, sizeof q);
        q.h.len = sizeof q;
        q.h.unit = d->unit;
        q.h.cmd = CMD_BPB;
        q.media = d->media;
        q.buf = (uint32_t)bpb_scratch;
        int st = devcall(d->driver, &q);
        if (!(st & RS_ERROR) && q.bpb) {
            build_dpb(d, (const struct bpb *)q.bpb);
            d->first_access = 0;
            break;
        }
        int r = crit_error(0x18 | (AREA_FAT << 1), drive, st & 0xFF, d->driver);
        if (r == 1) continue;
        d->first_access = 0xFF;
        return -(19 + (st & 0x0F));
    }
    /* current directories that do not exist on the new diskette go to the root */
    for (int i = 0; i < n_cds; i++) {
        struct cds *k = &cds_tab[i];
        if (k->dpb != d || !(k->flags & CDS_VALID) || (k->flags & CDS_JOIN)) continue;
        int rl = cds_rootlen(k);
        if (!k->path[rl]) continue;
        uint16_t cl;
        if (dir_walk_to(d, k->path, &cl) < 0) { k->path[rl] = 0; k->cluster = 0; }
        else k->cluster = cl;
    }
    return 0;
}

/* the DPB of a drive, media-checked once per INT 21h call */
struct dpb *drive_dpb(int drive, int *err)
{
    struct cds *c = get_cds(drive);
    if (!c || !(c->flags & CDS_VALID) || !c->dpb) { *err = E_BADDRIVE; return 0; }
    if (cur_ctx && cur_ctx->checked[drive] && c->dpb->first_access != 0xFF) return c->dpb;
    int e = media_check(drive);
    if (e < 0) { *err = -e; return 0; }
    if (cur_ctx) cur_ctx->checked[drive] = 1;
    return c->dpb;
}

/* a new volume label also goes into the boot record (CREATE.ASM ~212:
   Set_Media_ID), so VOL, DIR and AX=6900h agree */
void label_to_boot(struct dpb *d, const char *name11)
{
    if (!(d->driver->attr & DEVA_GENIOCTL)) return;
    uint8_t info[2 + 4 + 11 + 8];
    struct req_gioctl q;
    memset(&q, 0, sizeof q);
    q.h.len = sizeof q;
    q.h.unit = d->unit;
    q.h.cmd = CMD_GENIOCTL;
    q.category = 8;
    q.minor = 0x66;
    q.data = (uint32_t)info;
    if (devcall(d->driver, &q) & RS_ERROR) return;
    memcpy(info + 6, name11, 11);
    q.minor = 0x46;
    devcall(d->driver, &q);
}
