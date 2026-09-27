/* ARM/AT BIOS — INT 13h: the floppy (DMA controller at 300h) and the ATA fixed disk */
#include "bios.h"

#define FDC_ADDR   0x300
#define FDC_COUNT  0x304
#define FDC_LBA_LO 0x305
#define FDC_LBA_HI 0x306
#define FDC_CMD    0x307
#define FDC_MEDIA  0x302

#define ATA_DATA   0x1F0
#define ATA_ERR    0x1F1
#define ATA_COUNT  0x1F2
#define ATA_LBA0   0x1F3
#define ATA_LBA1   0x1F4
#define ATA_LBA2   0x1F5
#define ATA_DRIVE  0x1F6
#define ATA_CMD    0x1F7
#define ATA_CTL    0x3F6

#define ST_BSY 0x80
#define ST_DRQ 0x08
#define ST_ERR 0x01

/* the primary IDE channel's master (80h, C:) and slave (81h, D:) */
char hd_model[2][41];
static uint32_t hd_total[2];
static int hd_present[2], hd_cyls[2], hd_heads = 16, hd_spt = 63;

static const struct { uint8_t cyls, heads, spt, type; } fd_geom[5] = {
    { 0, 0, 0, 0 }, { 40, 2, 9, 1 }, { 80, 2, 15, 2 }, { 80, 2, 9, 3 }, { 80, 2, 18, 4 },
};

/* the classic 11-byte diskette parameter table (INT 1Eh) */
static const uint8_t fd_params[11] = { 0xAF, 0x02, 0x25, 0x02, 18, 0x1B, 0xFF, 0x6C, 0xF6, 0x0F, 0x08 };

/* ------------------------------------------------------------ floppy */

/* Reading the controller's status clears its "disk changed" bit, so every
   status read goes through here and the change is latched until INT 13h
   reports it (AH=16h, or error 06h from a transfer). */
static uint8_t fd_change_latch;

static uint8_t fdc_status(void)
{
    uint8_t st = inb(FDC_CMD);
    if (st & 0x20) fd_change_latch = 1;
    return st;
}

int floppy_media(void)
{
    outb(FDC_CMD, 0x04);
    if (!(fdc_status() & 0x40)) return 0;
    int m = inb(FDC_MEDIA);
    return (m >= 1 && m <= 4) ? m : 0;
}

int floppy_present(void) { return floppy_media() != 0; }

static int fdc_xfer(int write, uint32_t lba, int count, void *buf)
{
    uint32_t a = (uint32_t)buf;
    outb(FDC_ADDR + 0, a); outb(FDC_ADDR + 1, a >> 8); outb(FDC_ADDR + 2, a >> 16); outb(FDC_ADDR + 3, a >> 24);
    outb(FDC_COUNT, count);
    outb(FDC_LBA_LO, lba); outb(FDC_LBA_HI, lba >> 8);
    outb(FDC_CMD, write ? 0x02 : 0x01);
    uint8_t st;
    while ((st = fdc_status()) & 0x80) ;
    if (!(st & 0x40)) return 0x80;          /* no disk: timeout */
    if (fd_change_latch) { fd_change_latch = 0; return 0x06; }  /* the door was opened */
    if (write && (st & 0x10)) return 0x03;  /* write protected */
    if (st & 0x01) return 0x04;             /* sector not found */
    return 0;
}

/* --------------------------------------------------------------- ATA */

/*
 * Wait for the drive. A real disk answers within milliseconds; the web page's
 * streamed disk may keep BSY up while a chunk of the image is still arriving over
 * the network, so after a short spin this waits on the timer tick (sleeping between
 * polls) for up to 30 seconds, like an AT BIOS's generous fixed-disk timeout.
 */
static int ata_status_ok(uint8_t st, int want_drq, int *done)
{
    *done = 0;
    if (st & ST_BSY) return 0;
    *done = 1;
    if (st & ST_ERR) return 0x20;       /* controller failure */
    if (!want_drq || (st & ST_DRQ)) return 0;
    *done = 0;
    return 0;
}

static int ata_wait(int want_drq)
{
    int done, e;
    for (int i = 0; i < 20000; i++) {
        e = ata_status_ok(inb(ATA_CMD), want_drq, &done);
        if (done) return e;
    }
    uint32_t s = irq_save();
    irq_restore(s);
    if (s & 0x80) {                     /* interrupts off: no clock to wait on, so count */
        for (uint32_t i = 0; i < 400000000u; i++) {
            e = ata_status_ok(inb(ATA_CMD), want_drq, &done);
            if (done) return e;
        }
        return 0x80;
    }
    uint32_t start = ticks();
    while (ticks() - start < 546) {     /* 30 s */
        e = ata_status_ok(inb(ATA_CMD), want_drq, &done);
        if (done) return e;
        wfi();
    }
    return 0x80;
}

static void ata_select(int unit, uint32_t lba, int count)
{
    outb(ATA_DRIVE, 0xE0 | (unit << 4) | ((lba >> 24) & 0x0F));
    outb(ATA_COUNT, count);
    outb(ATA_LBA0, lba);
    outb(ATA_LBA1, lba >> 8);
    outb(ATA_LBA2, lba >> 16);
}

static int ata_xfer(int unit, int write, uint32_t lba, int count, void *buf)
{
    uint8_t *p = buf;
    outb(ATA_DRIVE, 0xE0 | (unit << 4));    /* the status below is the selected drive's */
    while (count > 0) {
        int n = count > 255 ? 255 : count;
        int e = ata_wait(0);
        if (e) return e;
        ata_select(unit, lba, n);
        outb(ATA_CMD, write ? 0x30 : 0x20);
        for (int s = 0; s < n; s++) {
            if ((e = ata_wait(1))) return e;
            if ((uintptr_t)p & 1) {
                for (int i = 0; i < 256; i++) {
                    if (write) outw(ATA_DATA, p[0] | (p[1] << 8));
                    else { uint16_t w = inw(ATA_DATA); p[0] = w; p[1] = w >> 8; }
                    p += 2;
                }
            } else {
                volatile uint16_t *w = (volatile uint16_t *)p;
                for (int i = 0; i < 256; i++) {
                    if (write) outw(ATA_DATA, w[i]);
                    else w[i] = inw(ATA_DATA);
                }
                p += 512;
            }
        }
        lba += n;
        count -= n;
    }
    if (write) { outb(ATA_CMD, 0xE7); ata_wait(0); }
    return 0;
}

static void ata_identify(int unit)
{
    hd_present[unit] = 0;
    outb(ATA_CTL, 0x02);                    /* nIEN: we poll */
    outb(ATA_DRIVE, 0xA0 | (unit << 4));
    uint8_t st = inb(ATA_CMD);
    if (st == 0xFF || st == 0x00) return;   /* nothing there */
    outb(ATA_CMD, 0xEC);
    if (ata_wait(1)) return;
    uint16_t id[256];
    for (int i = 0; i < 256; i++) id[i] = inw(ATA_DATA);
    char *model = hd_model[unit];
    hd_total[unit] = id[60] | ((uint32_t)id[61] << 16);
    for (int i = 0; i < 20; i++) { model[i * 2] = id[27 + i] >> 8; model[i * 2 + 1] = id[27 + i] & 0xFF; }
    model[40] = 0;
    for (int i = 39; i >= 0 && model[i] == ' '; i--) model[i] = 0;
    hd_cyls[unit] = hd_total[unit] / (hd_heads * hd_spt);
    if (hd_cyls[unit] > 1024) hd_cyls[unit] = 1024;
    hd_present[unit] = hd_total[unit] > 0;
}

uint32_t hd_sectors_of(int unit) { return hd_present[unit] ? hd_total[unit] : 0; }
uint32_t hd_sectors(void) { return hd_sectors_of(0); }

/* BIOS drive number -> unit (0 master, 1 slave), or -1: not a hard disk we have */
static int hd_unit(int drive)
{
    int u = drive - 0x80;
    return (u == 0 || u == 1) && hd_present[u] ? u : -1;
}

void disk_init(void)
{
    ata_identify(0);
    if (hd_present[0]) ata_identify(1);     /* (a slave only counts next to a master, as in a real PC) */
    outb(ATA_DRIVE, 0xA0);
    BDA8(BDA_HDCOUNT) = hd_present[0] + hd_present[1];
    BDA8(BDA_FDSTATUS) = 0;
    BDA8(BDA_HDSTATUS) = 0;
    IVT[0x1E] = (uint32_t)fd_params;
}

int disk_read_lba(int drive, uint32_t lba, int count, void *buf)
{
    if (drive == 0) return fdc_xfer(0, lba, count, buf);
    if (hd_unit(drive) >= 0) return ata_xfer(hd_unit(drive), 0, lba, count, buf);
    return 0x80;
}

int disk_write_lba(int drive, uint32_t lba, int count, const void *buf)
{
    if (drive == 0) return fdc_xfer(1, lba, count, (void *)buf);
    if (hd_unit(drive) >= 0) return ata_xfer(hd_unit(drive), 1, lba, count, (void *)buf);
    return 0x80;
}

/* ------------------------------------------------------------ INT 13h */

static void finish(struct armregs *f, int drive, int err)
{
    set_ah(f, err);
    set_cf(f, err != 0);
    if (drive & 0x80) BDA8(BDA_HDSTATUS) = err; else BDA8(BDA_FDSTATUS) = err;
}

void int13_handler(struct armregs *f)
{
    int drive = DL(f);
    int unit = hd_unit(drive);
    int fd = drive == 0, hd = unit >= 0;
    int cyls, heads, spt;

    if (fd) {
        int m = floppy_media();
        cyls = fd_geom[m].cyls; heads = fd_geom[m].heads; spt = fd_geom[m].spt;
    } else { cyls = hd ? hd_cyls[unit] : 0; heads = hd_heads; spt = hd_spt; }

    switch (AH(f)) {
    case 0x00:
        if (fd) outb(FDC_CMD, 0x03);
        finish(f, drive, (fd || hd) ? 0 : 0x01);
        break;
    case 0x01: {
        int st = (drive & 0x80) ? BDA8(BDA_HDSTATUS) : BDA8(BDA_FDSTATUS);
        set_ah(f, st);
        set_cf(f, st != 0);
        break;
    }
    case 0x02: case 0x03: case 0x04: {
        if (!fd && !hd) { finish(f, drive, drive < 0x80 ? 0x80 : 0x01); break; }
        int count = AL(f);
        int cyl = CH(f) | ((CL(f) & 0xC0) << 2);
        int sec = CL(f) & 0x3F;
        int head = DH(f);
        if (spt == 0) { finish(f, drive, 0x80); set_al(f, 0); break; }
        if (sec == 0 || sec > spt || head >= heads || cyl >= cyls) { finish(f, drive, 0x04); set_al(f, 0); break; }
        uint32_t lba = ((uint32_t)cyl * heads + head) * spt + (sec - 1);
        int err = 0;
        if (AH(f) == 0x02) err = disk_read_lba(drive, lba, count, (void *)f->r1);
        else if (AH(f) == 0x03) err = disk_write_lba(drive, lba, count, (void *)f->r1);
        finish(f, drive, err);
        set_al(f, err ? 0 : count);
        break;
    }
    case 0x08:
        if (drive < 0x80) {
            int m = floppy_media();
            set_bl(f, m ? fd_geom[m].type : 4);
            if (!m) m = 4;
            f->r2 = ((fd_geom[m].cyls - 1) << 8) | fd_geom[m].spt;
            f->r3 = ((fd_geom[m].heads - 1) << 8) | 1;
            f->r5 = (uint32_t)fd_params;
            finish(f, drive, drive == 0 ? 0 : 0x01);
            if (drive != 0) f->r3 = 1;
        } else if (hd) {
            int c = cyls - 1;
            f->r2 = ((c & 0xFF) << 8) | ((c >> 2) & 0xC0) | spt;
            f->r3 = ((heads - 1) << 8) | BDA8(BDA_HDCOUNT);     /* DL: how many hard disks */
            finish(f, drive, 0);
        } else {
            finish(f, drive, 0x01);
        }
        break;
    case 0x15:
        set_cf(f, 0);
        if (drive == 0) set_ah(f, floppy_present() ? 0x02 : 0x00);
        else if (hd) { set_ah(f, 0x03); f->r2 = hd_total[unit] >> 16; f->r3 = hd_total[unit] & 0xFFFF; }
        else set_ah(f, 0);
        break;
    case 0x16:
        if (drive == 0) {
            outb(FDC_CMD, 0x04);
            uint8_t st = fdc_status();
            int e = fd_change_latch ? 0x06 : (st & 0x40) ? 0 : 0x80;
            if (st & 0x40) fd_change_latch = 0;     /* reported (the line stays active with no disk) */
            finish(f, drive, e);
        } else finish(f, drive, 0x01);
        break;
    case 0x41:
        if (hd && (f->r1 & 0xFFFF) == 0x55AA) {
            f->r1 = 0xAA55;
            set_ah(f, 0x21);
            f->r2 = 0x0001;
            set_cf(f, 0);
        } else finish(f, drive, 0x01);
        break;
    case 0x42: case 0x43: {
        const uint8_t *pk = (const uint8_t *)f->r4;
        int count = pk[2] | (pk[3] << 8);
        uint32_t buf = pk[4] | (pk[5] << 8) | (pk[6] << 16) | ((uint32_t)pk[7] << 24);
        uint32_t lba = pk[8] | (pk[9] << 8) | (pk[10] << 16) | ((uint32_t)pk[11] << 24);
        if (!fd && !hd) { finish(f, drive, 0x01); break; }
        int err = AH(f) == 0x42 ? disk_read_lba(drive, lba, count, (void *)buf)
                                : disk_write_lba(drive, lba, count, (void *)buf);
        finish(f, drive, err);
        break;
    }
    case 0x48: {
        if (!hd) { finish(f, drive, 0x01); break; }
        uint8_t *p = (uint8_t *)f->r4;
        memset(p + 2, 0, 24);
        p[0] = 26; p[1] = 0;
        *(uint32_t *)(p + 4) = hd_cyls[unit];
        *(uint32_t *)(p + 8) = hd_heads;
        *(uint32_t *)(p + 12) = hd_spt;
        *(uint32_t *)(p + 16) = hd_total[unit];
        p[24] = 0; p[25] = 2;   /* 512 bytes per sector */
        finish(f, drive, 0);
        break;
    }
    default:
        finish(f, drive, 0x01);
        break;
    }
}
