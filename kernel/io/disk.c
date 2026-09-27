/*
 * disk.c - the resident block device driver (MSDISK.ASM + MSINIT.ASM):
 * unit 0 = A: (the floppy drive), unit 1 = B: (the same drive as a
 * "phantom", with DOS's "Insert diskette for drive B:" prompt), unit 2 = C:
 * (the first DOS partition of the fixed disk, found in its MBR).  All I/O is
 * INT 13h AH=42h/43h (LBA, flat buffer in the packet); 32-bit sector
 * numbers (attribute bit 1, the DOS 4 "big partition" interface).
 */
#include "iosys.h"

static void disk_strategy(struct reqhdr *r);
static void disk_interrupt(void);

struct devhdr disk_dev = {
    &com1_dev, DEVA_OPENCLOSE | DEVA_GENIOCTL | DEVA_32BIT, 0,
    disk_strategy, disk_interrupt, "\3\0\0\0\0\0\0"
};

struct unit {
    uint8_t  bios;              /* 00h / 80h */
    uint8_t  removable;
    uint8_t  changed;           /* sticky: the change line fired */
    uint8_t  devtype;           /* generic IOCTL device type */
    uint8_t  ptype;             /* fixed disk: partition type */
    uint8_t  formatted;         /* fixed disk: the boot sector had a BPB */
    uint32_t hidden;            /* first sector of the unit on the drive */
    uint32_t total;             /* sectors */
    uint16_t cyls, heads, spt;
    struct bpb bpb;
    char     vol[12];
};

static struct unit units[MAXUNITS];
static int nunits;
static struct bpb *bpbptr[MAXUNITS];
static struct reqhdr *disk_req;
static uint8_t fd_owner;                /* which of A:/B: the floppy drive is now */
uint8_t disk_boot_bios;
static int boot_unit;

/* standard floppy BPBs (KERNEL.md 1.5) */
static const struct bpb bpb1440 = { 512, 1, 1, 2, 224, 2880, 0xF0, 9, 18, 2, 0, 0 };
static const struct bpb bpb720  = { 512, 2, 1, 2, 112, 1440, 0xF9, 3, 9, 2, 0, 0 };
static const struct bpb bpb1200 = { 512, 1, 1, 2, 224, 2400, 0xF9, 7, 15, 2, 0, 0 };
static const struct bpb bpb360  = { 512, 2, 1, 2, 112, 720, 0xFD, 2, 9, 2, 0, 0 };

static uint8_t scratch[512] __attribute__((aligned(4)));
static struct { uint8_t size, res, count_lo, count_hi; uint32_t buf, lba, lba_hi; } dap;

static int bios_rw(int bios, int write, uint32_t lba, int count, void *buf)
{
    struct armregs r = { 0 };
    dap.size = 16; dap.res = 0;
    dap.count_lo = count; dap.count_hi = count >> 8;
    dap.buf = (uint32_t)buf;
    dap.lba = lba; dap.lba_hi = 0;
    r.r0 = write ? 0x4300 : 0x4200;
    r.r3 = bios;
    r.r4 = (uint32_t)&dap;
    kint(0x13, &r);
    return (r.cpsr & CPSR_C) ? ((r.r0 >> 8) & 0xFF ? (r.r0 >> 8) & 0xFF : 0xFF) : 0;
}

static void bios_reset(int bios)
{
    struct armregs r = { 0 };
    r.r3 = bios;
    kint(0x13, &r);
}

static int map_error(int e)
{
    switch (e) {
    case 0x80: return DE_NOTREADY;
    case 0x03: return DE_WRPROT;
    case 0x04: return DE_NOTFOUND;
    case 0x10: return DE_CRC;
    case 0x40: return DE_SEEK;
    case 0x02: return DE_NOTFOUND;
    case 0x01: return DE_UNIT;
    case 0x0C: return DE_MEDIA;
    default:   return DE_GENERAL;
    }
}

/* one transfer with DOS's retries; returns 0, or 0x100 | device error code
   (the write-protect code is 0) */
static int unit_rw(struct unit *u, int write, uint32_t lba, int count, void *buf)
{
    int e = 0;
    for (int tries = 0; tries < 5; tries++) {
        e = bios_rw(u->bios, write, u->hidden + lba, count, buf);
        if (e == 0) return 0;
        if (e == 0x06) {                /* media changed under us: note it, go on */
            for (int i = 0; i < nunits; i++) if (units[i].bios == u->bios) units[i].changed = 1;
            continue;
        }
        if (e == 0x80 || e == 0x03) break;  /* no point retrying these */
        bios_reset(u->bios);
    }
    return 0x100 | map_error(e);
}

/* single-floppy system: B: is A:'s drive with the other diskette */
static void check_single(int unit)
{
    if (unit > 1 || !units[unit].removable) return;
    if (fd_owner == unit) return;
    static const char msg1[] = "\r\nInsert diskette for drive ";
    static const char msg2[] = ": and press any key when ready\r\n\r\n";
    struct armregs r;
    for (const char *p = msg1; *p; p++) { memset(&r, 0, sizeof r); r.r0 = 0x0E00 | (uint8_t)*p; r.r1 = 7; kint(0x10, &r); }
    memset(&r, 0, sizeof r); r.r0 = 0x0E00 | ('A' + unit); r.r1 = 7; kint(0x10, &r);
    for (const char *p = msg2; *p; p++) { memset(&r, 0, sizeof r); r.r0 = 0x0E00 | (uint8_t)*p; r.r1 = 7; kint(0x10, &r); }
    memset(&r, 0, sizeof r);
    kint(0x16, &r);                     /* wait for a key */
    fd_owner = unit;
    BDA8(0x104) = unit;                 /* 0:0504, the single-drive logical unit, as DOS */
    units[unit].changed = 1;
}

static int bpb_valid(const struct bpb *b)
{
    if (b->bytes_per_sec != 512) return 0;
    if (!b->sec_per_clus || (b->sec_per_clus & (b->sec_per_clus - 1))) return 0;
    if (!b->reserved || !b->nfats || b->nfats > 2 || !b->root_ents || !b->fat_secs) return 0;
    if (b->media < 0xF0) return 0;
    if (!b->total16 && !b->total32) return 0;
    return 1;
}

/* the BPB DOS 4's IO.SYS assumes for a fixed-disk partition from its type and
   size (MSINIT.ASM DiskTable / DiskTable2), used for an unformatted one */
static void default_bpb(struct unit *u, struct bpb *b)
{
    uint32_t t = u->total;
    unsigned spc, root;
    if (u->ptype == 0x06) {
        if (t <= 32680) { spc = 8; root = 512; }
        else if (t <= 0x40000) { spc = 4; root = 512; }
        else if (t <= 0x80000) { spc = 8; root = 512; }
        else if (t <= 0x100000) { spc = 16; root = 512; }
        else if (t <= 0x200000) { spc = 32; root = 512; }
        else if (t <= 0x400000) { spc = 64; root = 512; }
        else { spc = 128; root = 512; }
    } else {
        if (t <= 512) { spc = 1; root = 64; }
        else if (t <= 2048) { spc = 2; root = 112; }
        else if (t <= 8192) { spc = 4; root = 256; }
        else if (t <= 32680) { spc = 8; root = 512; }
        else { spc = 16; root = 1024; }
    }
    unsigned rootsecs = root * 32 / 512, fat = 1;
    for (int k = 0; k < 8; k++) {
        uint32_t clusters = (t - 1 - rootsecs - 2 * fat) / spc;
        uint32_t bytes = clusters + 1 >= 4086 ? (clusters + 2) * 2 : ((clusters + 2) * 3 + 1) / 2;
        unsigned nf = (bytes + 511) / 512;
        if (nf == fat) break;
        fat = nf;
    }
    b->bytes_per_sec = 512; b->sec_per_clus = spc; b->reserved = 1; b->nfats = 2;
    b->root_ents = root; b->media = 0xF8; b->fat_secs = fat;
    b->sec_per_track = u->spt; b->heads = u->heads; b->hidden = u->hidden;
    if (t < 0x10000) { b->total16 = t; b->total32 = 0; } else { b->total16 = 0; b->total32 = t; }
}

static int bpb_fat16(const struct bpb *b)
{
    uint32_t total = b->total16 ? b->total16 : b->total32;
    uint32_t data = b->reserved + b->nfats * b->fat_secs + (b->root_ents * 32 + 511) / 512;
    return b->sec_per_clus && (total - data) / b->sec_per_clus + 1 >= 4086;
}

static int build_bpb(int unit, uint8_t *buf)
{
    struct unit *u = &units[unit];
    int e = unit_rw(u, 0, 0, 1, buf);
    if (e) return e;
    struct bpb b;
    memcpy(&b, buf + 0x0B, sizeof b);
    if (bpb_valid(&b)) {
        if (buf[0x26] == 0x29) { memcpy(u->vol, buf + 0x2B, 11); u->vol[11] = 0; }
        else strcpy(u->vol, "NO NAME    ");
    } else {
        if (!u->removable) {
            /* an unformatted partition: DOS's default BPB, but the media is not usable */
            default_bpb(u, &u->bpb);
            u->formatted = 0;
            strcpy(u->vol, "NO NAME    ");
            return 0x100 | DE_MEDIA;
        }
        /* no BPB: go by the FAT id byte, as DOS 2 disks require */
        e = unit_rw(u, 0, 1, 1, buf);
        if (e) return e;
        switch (buf[0]) {
        case 0xF0: b = bpb1440; break;
        case 0xF9: b = u->spt == 15 ? bpb1200 : bpb720; break;
        case 0xFD: b = bpb360; break;
        default: return 0x100 | DE_MEDIA;
        }
        strcpy(u->vol, "NO NAME    ");
    }
    if (u->removable) { b.hidden = 0; }
    u->bpb = b;
    u->formatted = 1;
    return 0;
}

/* floppy geometry from INT 13h AH=08h (depends on the diskette in the drive) */
static void floppy_geometry(struct unit *u)
{
    struct armregs r = { 0 };
    r.r0 = 0x0800;
    r.r3 = 0;
    kint(0x13, &r);
    u->spt = r.r2 & 0x3F;
    u->cyls = ((r.r2 >> 8) & 0xFF) + 1;
    u->heads = ((r.r3 >> 8) & 0xFF) + 1;
    if (!u->spt) { u->spt = 18; u->cyls = 80; u->heads = 2; }
    u->total = (uint32_t)u->spt * u->heads * u->cyls;
}

static uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

static void do_rw(int unit, struct req_rw *q)
{
    struct unit *u = &units[unit];
    int write = q->h.cmd == CMD_WRITE || q->h.cmd == CMD_WRITEV;
    uint32_t start = q->start == 0xFFFF ? q->start32 : q->start;
    unsigned count = q->count, done = 0;
    uint8_t *buf = (uint8_t *)q->addr;
    check_single(unit);
    uint32_t limit = u->bpb.total16 ? u->bpb.total16 : u->bpb.total32;
    if (!u->removable && u->total && u->total < limit) limit = u->total;
    while (done < count) {
        unsigned n = count - done;
        if (n > 64) n = 64;
        if (limit && start + done + n > limit) {
            q->count = done;
            q->h.status |= RS_ERROR | DE_NOTFOUND;
            return;
        }
        int e = unit_rw(u, write, start + done, n, buf + done * 512);
        if (e) {
            q->count = done;
            q->h.status |= RS_ERROR | (e & 0xFF);
            return;
        }
        done += n;
    }
}

/* ------------------------------------------------------ generic IOCTL */

static void gioctl(int unit, struct req_gioctl *q)
{
    struct unit *u = &units[unit];
    uint8_t *d = (uint8_t *)q->data;
    if (q->category != 8) { q->h.status |= RS_ERROR | DE_BADCMD; return; }
    switch (q->minor) {
    case 0x60: {                        /* get device parameters */
        const struct bpb *b = &u->bpb;
        if (u->removable && !(d[0] & 1)) b = u->devtype == 7 ? &bpb1440 : u->devtype == 1 ? &bpb1200 : u->devtype == 2 ? &bpb720 : &bpb360;
        d[1] = u->devtype;
        d[2] = u->removable ? 2 : 1;    /* bit0 non-removable, bit1 change line */
        d[3] = 0;
        d[4] = u->cyls; d[5] = u->cyls >> 8;
        d[6] = 0;                       /* media type */
        memset(d + 7, 0, 31);
        memcpy(d + 7, b, sizeof *b);
        break;
    }
    case 0x40:                          /* set device parameters: accepted */
        break;
    case 0x61: case 0x41: case 0x62: case 0x42: {
        /* read / write / verify / format track:
           +0 special, +1 head, +3 cylinder, (+5 first sector, +7 count, +9 buffer) */
        unsigned head = d[1] | (d[2] << 8), cyl = d[3] | (d[4] << 8);
        uint32_t lba = ((uint32_t)cyl * u->heads + head) * u->spt;
        int e = 0;
        check_single(unit);
        if (u->removable) floppy_geometry(u);
        lba = ((uint32_t)cyl * u->heads + head) * u->spt;
        if (q->minor == 0x61 || q->minor == 0x41) {
            unsigned first = d[5] | (d[6] << 8), n = d[7] | (d[8] << 8);
            e = unit_rw(u, q->minor == 0x41, lba + first, n, (void *)rd32(d + 9));
        } else if (q->minor == 0x62) {
            for (unsigned s = 0; s < u->spt && !e; s++) e = unit_rw(u, 0, lba + s, 1, scratch);
        } else if (!u->removable) {
            if (d[0] & 1) break;
            /* a fixed disk is only verified (4.00's IO.SYS) */
            for (unsigned s = 0; s < u->spt && !e; s++) e = unit_rw(u, 0, lba + s, 1, scratch);
        } else {
            if (d[0] & 1) break;        /* "is this format supported" query: yes */
            memset(scratch, 0xF6, 512);
            for (unsigned s = 0; s < u->spt && !e; s++) e = unit_rw(u, 1, lba + s, 1, scratch);
        }
        if (e) q->h.status |= RS_ERROR | (e & 0xFF);
        break;
    }
    case 0x66: case 0x46: {             /* get / set media id */
        uint8_t *sec = scratch;
        int e = unit_rw(u, 0, 0, 1, sec);
        if (e) { q->h.status |= RS_ERROR | (e & 0xFF); break; }
        if (sec[0x26] != 0x29 && !u->removable && q->minor == 0x66) {
            /* unformatted partition: what DOS 4 reports from its default BPB */
            struct bpb b;
            memcpy(&b, sec + 0x0B, sizeof b);
            if (!bpb_valid(&b)) default_bpb(u, &b);
            d[0] = d[1] = 0;
            memset(d + 2, 0, 4);
            memcpy(d + 6, "NO NAME    ", 11);
            memcpy(d + 17, bpb_fat16(&b) ? "FAT16   " : "FAT12   ", 8);
            break;
        }
        if (sec[0x26] != 0x29) { q->h.status |= RS_ERROR | DE_MEDIA; break; }
        if (q->minor == 0x66) {
            d[0] = d[1] = 0;
            memcpy(d + 2, sec + 0x27, 4 + 11 + 8);
        } else {
            memcpy(sec + 0x27, d + 2, 4 + 11 + 8);
            e = unit_rw(u, 1, 0, 1, sec);
            if (e) q->h.status |= RS_ERROR | (e & 0xFF);
        }
        break;
    }
    case 0x67:                          /* get access flag */
        d[1] = 1;
        break;
    case 0x47:
        break;
    default:
        q->h.status |= RS_ERROR | DE_BADCMD;
    }
}

/* ------------------------------------------------------- the driver */

static void disk_strategy(struct reqhdr *r) { disk_req = r; }

static void disk_interrupt(void)
{
    struct reqhdr *r = disk_req;
    int unit = r->unit;
    r->status = RS_DONE;
    if (r->cmd != CMD_INIT && unit >= nunits) { r->status |= RS_ERROR | DE_UNIT; return; }
    struct unit *u = &units[unit];
    switch (r->cmd) {
    case CMD_INIT: {
        struct req_init *q = (struct req_init *)r;
        q->units = nunits;
        q->arg = (uint32_t)bpbptr;
        break;
    }
    case CMD_MEDIA: {
        struct req_media *q = (struct req_media *)r;
        q->volid = (uint32_t)u->vol;
        /* an unformatted partition is "changed" every time, so DOS keeps
           asking for its BPB (and gets "unknown media") until it is formatted */
        if (!u->removable) { q->changed = u->formatted ? 1 : -1; break; }
        if (fd_owner != unit) { check_single(unit); }
        struct armregs a = { 0 };
        a.r0 = 0x1600;
        a.r3 = u->bios;
        kint(0x13, &a);
        int e = (a.r0 >> 8) & 0xFF;
        if (!(a.cpsr & CPSR_C)) e = 0;
        if (e == 0x06 || u->changed) { q->changed = -1; u->changed = 0; }
        else if (e == 0x80) q->changed = 0;
        else q->changed = 1;
        if (q->changed == -1) for (int i = 0; i < nunits; i++) if (units[i].bios == u->bios && i != unit) units[i].changed = 1;
        break;
    }
    case CMD_BPB: {
        struct req_bpb *q = (struct req_bpb *)r;
        check_single(unit);
        if (u->removable) floppy_geometry(u);
        int e = build_bpb(unit, (uint8_t *)q->buf);
        if (e) r->status |= RS_ERROR | (e & 0xFF);
        else { q->bpb = (uint32_t)&u->bpb; u->changed = 0; }
        break;
    }
    case CMD_READ:
    case CMD_WRITE:
    case CMD_WRITEV:
        do_rw(unit, (struct req_rw *)r);
        break;
    case CMD_REMOVABLE:
        if (!u->removable) r->status |= RS_BUSY;
        break;
    case CMD_OPEN:
    case CMD_CLOSE:
        break;
    case CMD_GENIOCTL:
        gioctl(unit, (struct req_gioctl *)r);
        break;
    case CMD_GETLOG:
        r->unit = (unit <= 1 && u->removable) ? fd_owner + 1 : 0;
        break;
    case CMD_SETLOG:
        if (unit <= 1 && u->removable) { fd_owner = unit; BDA8(0x104) = unit; }
        r->unit = (unit <= 1 && u->removable) ? fd_owner + 1 : 0;
        break;
    default:
        r->status |= RS_ERROR | DE_BADCMD;
    }
}

/* ------------------------------------------------------------- init */

static void add_fixed(uint8_t type, uint32_t start, uint32_t size, unsigned spt, unsigned heads)
{
    if (nunits >= MAXUNITS || !size) return;
    int n = nunits;
    struct unit *u = &units[n];
    memset(u, 0, sizeof *u);
    u->bios = 0x80;
    u->removable = 0;
    u->devtype = 5;
    u->ptype = type;
    u->hidden = start;
    u->total = size;
    u->spt = spt;
    u->heads = heads;
    u->cyls = (size + spt * heads - 1) / (spt * heads);
    bpbptr[n] = &u->bpb;
    nunits = n + 1;
    if (build_bpb(n, scratch) != 0 && !u->bpb.bytes_per_sec) default_bpb(u, &u->bpb);
}

int disk_boot_unit(void) { return boot_unit; }

int disk_init(int bootdrive, struct bpb **bpbs_out)
{
    (void)bpbs_out;
    disk_boot_bios = bootdrive;
    /* A: and B: - one physical floppy drive */
    for (int i = 0; i < 2; i++) {
        struct unit *u = &units[i];
        u->bios = 0;
        u->removable = 1;
        u->devtype = 7;                 /* 3.5" 1.44 MB ("other" in 4.0) */
        u->cyls = 80; u->heads = 2; u->spt = 18; u->total = 2880;
        u->bpb = bpb1440;
        strcpy(u->vol, "NO NAME    ");
        bpbptr[i] = &u->bpb;
    }
    nunits = 2;
    fd_owner = 0;
    BDA8(0x104) = 0;

    /* C: - the first DOS partition in the MBR; then D:, E:, ... - the
       logical drives of the extended partition, in chain order (DOS 4) */
    uint8_t *sec = scratch;
    if (BDA8(0x75) && bios_rw(0x80, 0, 0, 1, sec) == 0 && sec[510] == 0x55 && sec[511] == 0xAA) {
        uint8_t table[64];
        memcpy(table, sec + 0x1BE, 64);
        struct armregs r = { 0 };
        r.r0 = 0x0800;
        r.r3 = 0x80;
        kint(0x13, &r);
        unsigned spt = r.r2 & 0x3F, heads = ((r.r3 >> 8) & 0xFF) + 1;
        if (!spt) { spt = 63; heads = 16; }
        for (int p = 0; p < 4; p++) {
            const uint8_t *e = table + p * 16;
            if (e[4] == 0x01 || e[4] == 0x04 || e[4] == 0x06) {
                add_fixed(e[4], rd32(e + 8), rd32(e + 12), spt, heads);
                break;
            }
        }
        for (int p = 0; p < 4; p++) {
            const uint8_t *e = table + p * 16;
            if (e[4] != 0x05) continue;
            uint32_t ext = rd32(e + 8), ebr = ext;
            for (int guard = 0; guard < 24 && nunits < MAXUNITS; guard++) {
                if (bios_rw(0x80, 0, ebr, 1, sec) || sec[510] != 0x55 || sec[511] != 0xAA) break;
                uint8_t l[32];
                memcpy(l, sec + 0x1BE, 32);
                if (l[4] == 0x01 || l[4] == 0x04 || l[4] == 0x06)
                    add_fixed(l[4], ebr + rd32(l + 8), rd32(l + 12), spt, heads);
                if (l[16 + 4] != 0x05) break;
                ebr = ext + rd32(l + 16 + 8);
            }
            break;
        }
    }
    disk_dev.name[0] = nunits;
    boot_unit = bootdrive == 0x80 ? 2 : 0;
    if (bootdrive != 0x80) floppy_geometry(&units[0]);
    return nunits;
}
