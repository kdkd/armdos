/*
 * fcb.c - the CP/M-style FCB functions (DOS/FCB.ASM, FCBIO.ASM, FCBIO2.ASM,
 * SEARCH.ASM): 0Fh-17h, 21h-24h, 27h, 28h (29h is in name.c).
 *
 * The FCB's reserved bytes (+18h..+1Fh) remember where the file's directory
 * entry is (flags/drive, entry index, directory sector, first cluster), so
 * each call rebuilds a transient SFT from the FCB - FCB I/O needs no kernel
 * state and never runs out of FCBS.
 */
#include "dos.h"

#define FF_OPEN     0x80
#define FF_DEVICE   0x40
#define FF_WRITTEN  0x20

/* ext FCB prefix: FFh, 5 reserved, attr */
static struct fcb *fcb_of(struct armregs *f, int *attr, uint8_t **ext)
{
    uint8_t *p = (uint8_t *)f->r3;
    *attr = 0;
    *ext = 0;
    if (p[0] == 0xFF) { *attr = p[6]; *ext = p; return (struct fcb *)(p + 7); }
    return (struct fcb *)p;
}

static int fcb_drive(struct fcb *fc)
{
    return fc->drive ? fc->drive - 1 : cur_drive;
}

/* the FCB's directory (the current directory of its drive) */
static int fcb_dir(struct fcb *fc, struct dpb **pd, uint16_t *pcl)
{
    int drive = fcb_drive(fc), err;
    if (drive_remote(drive)) return -E_ACCESS;      /* FCB I/O on a redirected drive */
    struct dpb *d = drive_dpb(drive, &err);
    if (!d) return -err;
    struct cds *c = get_cds(drive);
    uint16_t cl = 0;
    if (c->path[3]) {
        int e = dir_walk_to(d, c->path, &cl);
        if (e < 0) return e;
    }
    *pd = d;
    *pcl = cl;
    return drive;
}

static void fcb_fill(struct fcb *fc, int drive, const struct dirent *de, const struct dirloc *loc)
{
    fc->drive = drive + 1;
    fc->cur_block = 0;
    fc->rec_size = 128;
    fc->size = de->size;
    fc->date = de->date;
    fc->time = de->time;
    fc->sfn = FF_OPEN | drive;
    fc->dir_index = loc->index;
    fc->dir_sector = loc->sector;
    fc->first_cluster = de->cluster;
}

static int fcb_open_dev(struct fcb *fc)
{
    if (has_wild(fc->name)) return 0;
    struct devhdr *d = find_chardev(fc->name);
    if (!d) return 0;
    fc->cur_block = 0;
    fc->rec_size = 128;
    fc->size = 0;
    STAMP_NOW(fc);
    fc->sfn = FF_OPEN | FF_DEVICE;
    fc->dir_sector = (uint32_t)d;
    return 1;
}

static int fcb_open(struct armregs *f)
{
    int attr;
    uint8_t *ext;
    struct fcb *fc = fcb_of(f, &attr, &ext);
    if (fcb_open_dev(fc)) return 0;
    struct dpb *d;
    uint16_t cl;
    int drive = fcb_dir(fc, &d, &cl);
    if (drive < 0) return drive;
    struct dirent de;
    struct dirloc loc;
    int r = dir_find(d, cl, fc->name, attr & (ATTR_HIDDEN | ATTR_SYSTEM), 0, &de, &loc, 0);
    if (r < 0) return r;
    fcb_fill(fc, drive, &de, &loc);
    memcpy(fc->name, de.name, 11);
    return 0;
}

static int fcb_create(struct armregs *f)
{
    int attr;
    uint8_t *ext;
    struct fcb *fc = fcb_of(f, &attr, &ext);
    if (fcb_open_dev(fc)) return 0;
    if (has_wild(fc->name)) return -E_NOFILE;
    struct dpb *d;
    uint16_t cl;
    int drive = fcb_dir(fc, &d, &cl);
    if (drive < 0) return drive;
    struct dirent de;
    struct dirloc loc;
    int r = dir_find(d, cl, fc->name, ATTR_HIDDEN | ATTR_SYSTEM | ATTR_DIR | ATTR_VOLUME, 0, &de, &loc, 0);
    if (r >= 0) {
        if (de.attr & (ATTR_DIR | ATTR_RDONLY)) return -E_ACCESS;
        if ((de.attr & ATTR_VOLUME) != (attr & ATTR_VOLUME)) return -E_ACCESS;
        if (de.cluster) fat_free_chain(d, de.cluster);
    } else {
        if (r != -E_NOFILE) return r;
        int idx = dir_new_entry(d, cl, &loc);
        if (idx < 0) return idx;
        memset(&de, 0, sizeof de);
        memcpy(de.name, fc->name, 11);
    }
    de.attr = (attr & (ATTR_HIDDEN | ATTR_SYSTEM | ATTR_RDONLY | ATTR_VOLUME)) | ((attr & ATTR_VOLUME) ? 0 : ATTR_ARCHIVE);
    de.cluster = 0;
    de.size = 0;
    STAMP_NOW(&de);
    r = dir_write(d, &loc, &de);
    if (r < 0) return r;
    fcb_fill(fc, drive, &de, &loc);
    if (attr & ATTR_VOLUME) { flush_bufs(drive); label_to_boot(d, de.name); }
    return 0;
}

/* a transient SFT for an open FCB */
static int fcb_sft(struct fcb *fc, struct sft *s)
{
    memset(s, 0, sizeof *s);
    if (!(fc->sfn & FF_OPEN)) return -E_FCBUNAVAIL;
    s->ref_count = 1;
    s->mode = 0x8002;
    if (fc->sfn & FF_DEVICE) {
        struct devhdr *d = (struct devhdr *)fc->dir_sector;
        s->flags = SF_DEVICE | DI_NOEOF | (d->attr & 0x1F) | DI_RAW;
        s->devptr = (uint32_t)d;
        return 0;
    }
    int drive = fc->sfn & 0x1F, err;
    struct dpb *d = drive_dpb(drive, &err);
    if (!d) return -err;
    s->flags = drive;
    s->devptr = (uint32_t)d;
    s->first_cluster = fc->first_cluster;
    s->size = fc->size;
    s->dir_sector = fc->dir_sector;
    s->dir_index = fc->dir_index;
    memcpy(s->name, fc->name, 11);
    return 0;
}

static int fcb_close(struct armregs *f)
{
    int attr;
    uint8_t *ext;
    struct fcb *fc = fcb_of(f, &attr, &ext);
    if (!(fc->sfn & FF_OPEN)) return 0;         /* DOS closes anything quietly */
    if (fc->sfn & FF_DEVICE) return 0;
    if (!(fc->sfn & FF_WRITTEN)) return 0;
    int drive = fc->sfn & 0x1F, err;
    struct dpb *d = drive_dpb(drive, &err);
    if (!d) return -err;
    struct buf *b = getbuf(d, fc->dir_sector, AREA_DIR, 0);
    if (!b) return -E_GENFAIL;
    struct dirent *de = (struct dirent *)(b->data + fc->dir_index * 32);
    if (memcmp(de->name, fc->name, 11)) return -E_NOFILE;
    de->size = fc->size;
    de->cluster = fc->first_cluster;
    STAMP_NOW(de);
    fc->date = de->date;
    fc->time = de->time;
    de->attr |= ATTR_ARCHIVE;
    buf_dirty(b);
    fc->sfn &= ~FF_WRITTEN;
    return flush_bufs(drive);
}

/* ------------------------------------------------------ search */

/* search state in the FCB: +0Ch entry index, +0Eh directory cluster, +18h drive|0x80 */
/* FCB search on a redirected drive: 111Bh/111Ch on a kernel find DTA whose
   bytes 00h-14h (the redirector's search state) live in the FCB's +0Ch-+20h */
static int fcb_search_remote(struct fcb *fc, int attr, uint8_t *ext, int first)
{
    static uint8_t rdta[43];
    uint8_t *keep = (uint8_t *)fc + 0x0C;
    int drive = fcb_drive(fc), e;
    if (first) {
        struct pathinfo pi;
        memset(&pi, 0, sizeof pi);
        pi.drive = drive;
        pi.remote = 1;
        struct cds *c = get_cds(drive);
        strcpy(pi.full, c->path);
        if (attr & ATTR_VOLUME) pi.full[3] = 0;     /* the label lives in the root */
        int n = strlen(pi.full);
        if (pi.full[n - 1] != '\\') pi.full[n++] = '\\';
        fcb_to_name(fc->name, pi.full + n);
        e = redir_find(&pi, attr, rdta);
    } else {
        memcpy(rdta, keep, 21);
        e = redir_find(0, 0, rdta);
    }
    if (e == -E_NOFILE || e == -E_NOPATH) e = -E_NOMORE;
    if (e < 0) return e;
    memcpy(keep, rdta, 21);
    uint8_t *dta = (uint8_t *)cur_dta;
    if (ext) { memset(dta, 0, 7); dta[0] = 0xFF; dta[6] = attr; dta += 7; }
    struct dirent de;
    memset(&de, 0, sizeof de);
    redir_name11((const char *)rdta + 0x1E, de.name);
    de.attr = rdta[0x15];
    de.time = rdta[0x16] | (rdta[0x17] << 8);
    de.date = rdta[0x18] | (rdta[0x19] << 8);
    de.size = rdta[0x1A] | (rdta[0x1B] << 8) | (rdta[0x1C] << 16) | ((uint32_t)rdta[0x1D] << 24);
    dta[0] = drive + 1;
    memcpy(dta + 1, &de, 32);
    return 0;
}

static int fcb_search(struct armregs *f, int first)
{
    int attr;
    uint8_t *ext;
    struct fcb *fc = fcb_of(f, &attr, &ext);
    struct dpb *d;
    uint16_t cl;
    int drive;
    unsigned start;
    {
        int dr = fcb_drive(fc);
        uint8_t k = ((uint8_t *)fc)[0x0C];
        if (drive_remote(dr) && (first ? 1 : (k & 0x80) && (k & 0x1F) == dr + 1)) {
            if (!first || has_wild(fc->name) || !find_chardev(fc->name))
                return fcb_search_remote(fc, attr, ext, first);
        }
    }
    if (first) {
        /* a device name is "found" (COPY CON looks its source up this way) */
        if (!has_wild(fc->name) && find_chardev(fc->name)) {
            uint8_t *dta = (uint8_t *)cur_dta;
            struct dirent de;
            memset(&de, 0, sizeof de);
            memcpy(de.name, fc->name, 11);
            de.attr = 0x40;
            STAMP_NOW(&de);
            if (ext) { memset(dta, 0, 7); dta[0] = 0xFF; dta[6] = attr; dta += 7; }
            dta[0] = fcb_drive(fc) + 1;
            memcpy(dta + 1, &de, 32);
            fc->cur_block = 0xFFFF;
            fc->sfn = FF_OPEN | fcb_drive(fc);
            return 0;
        }
        drive = fcb_dir(fc, &d, &cl);
        if (drive < 0) return drive;
        start = 0;
    } else {
        if (fc->cur_block == 0xFFFF) return -E_NOMORE;
        drive = (fc->sfn & 0x1F);
        int err;
        d = drive_dpb(drive, &err);
        if (!d) return -err;
        cl = fc->rec_size;
        start = fc->cur_block;
    }
    struct dirent de;
    int r = dir_find(d, cl, fc->name, attr, start, &de, 0, 0);
    if (r == -E_NOFILE) return -E_NOMORE;       /* "no more files" for both, as DOS */
    if (r < 0) return r;
    fc->cur_block = r + 1;
    fc->rec_size = cl;
    fc->sfn = FF_OPEN | drive;
    uint8_t *dta = (uint8_t *)cur_dta;
    if (ext) {
        memset(dta, 0, 7);
        dta[0] = 0xFF;
        dta[6] = attr;
        dta += 7;
    }
    dta[0] = drive + 1;
    memcpy(dta + 1, &de, 32);
    return 0;
}

static int fcb_delete(struct armregs *f)
{
    int attr;
    uint8_t *ext;
    struct fcb *fc = fcb_of(f, &attr, &ext);
    struct dpb *d;
    uint16_t cl;
    int drive = fcb_dir(fc, &d, &cl);
    if (drive < 0) return drive;
    int n = 0;
    for (unsigned i = 0;;) {
        struct dirent de;
        struct dirloc loc;
        int r = dir_find(d, cl, fc->name, attr & (ATTR_HIDDEN | ATTR_SYSTEM | ATTR_VOLUME), i, &de, &loc, 0);
        if (r < 0) break;
        i = r + 1;
        if (de.attr & (ATTR_RDONLY | ATTR_DIR)) continue;     /* (LABEL deletes labels this way) */
        struct buf *b = getbuf(d, loc.sector, AREA_DIR, 0);
        if (!b) return -E_GENFAIL;
        b->data[loc.index * 32] = 0xE5;
        buf_dirty(b);
        if (de.cluster) fat_free_chain(d, de.cluster);
        n++;
    }
    flush_bufs(drive);
    return n ? 0 : -E_NOFILE;
}

static int fcb_rename(struct armregs *f)
{
    int attr;
    uint8_t *ext;
    struct fcb *fc = fcb_of(f, &attr, &ext);
    const char *newn = (const char *)fc + 0x11;
    struct dpb *d;
    uint16_t cl;
    int drive = fcb_dir(fc, &d, &cl);
    if (drive < 0) return drive;
    int n = 0;
    for (unsigned i = 0;;) {
        struct dirent de;
        struct dirloc loc;
        int r = dir_find(d, cl, fc->name, attr & (ATTR_HIDDEN | ATTR_SYSTEM | ATTR_DIR), i, &de, &loc, 0);
        if (r < 0) break;
        i = r + 1;
        char nn[11];
        for (int k = 0; k < 11; k++) nn[k] = newn[k] == '?' ? de.name[k] : dos_upcase(newn[k]);
        if (!memcmp(nn, de.name, 11)) { n++; continue; }
        struct dirent x;
        if (dir_find(d, cl, nn, ATTR_HIDDEN | ATTR_SYSTEM | ATTR_DIR | ATTR_VOLUME, 0, &x, 0, 0) >= 0) return -E_ACCESS;
        memcpy(de.name, nn, 11);
        if (dir_write(d, &loc, &de) < 0) return -E_GENFAIL;
        n++;
    }
    flush_bufs(drive);
    return n ? 0 : -E_NOFILE;
}

/* ------------------------------------------------------- records */

static uint32_t rand_rec(struct fcb *fc)
{
    uint32_t r = fc->rand_rec;
    if (fc->rec_size >= 64) r &= 0xFFFFFF;
    return r;
}

/* read/write `count` records at record number rec; returns AL (0 ok, 1 EOF/full,
   3 partial) and the number of records done */
static int fcb_io(struct fcb *fc, int write, uint32_t rec, unsigned count, unsigned *done)
{
    struct sft s;
    *done = 0;
    int e = fcb_sft(fc, &s);
    if (e < 0) return 1;
    unsigned rs = fc->rec_size ? fc->rec_size : 128;
    uint8_t *dta = (uint8_t *)cur_dta;
    s.position = rec * rs;
    uint32_t bytes = (uint32_t)count * rs;
    if (bytes > 0xFFFF) { bytes = 0xFFFF / rs * rs; count = bytes / rs; }
    if (write) {
        if (count == 0) {
            /* write of 0 records: set the file size */
            file_write(&s, dta, 0);
            fc->size = s.size;
            fc->first_cluster = s.first_cluster;
            fc->sfn |= FF_WRITTEN;
            return 0;
        }
        int n = file_write(&s, dta, bytes);
        if (n < 0) return 1;
        fc->size = s.size;
        fc->first_cluster = s.first_cluster;
        fc->sfn |= FF_WRITTEN;
        *done = n / rs;
        return (unsigned)n < bytes ? 1 : 0;
    }
    int n = file_read(&s, dta, bytes);
    if (n < 0) return 1;
    if ((s.flags & SF_DEVICE) && n > 0 && (unsigned)n < bytes) { *done = 1; return 0; }
    *done = n / rs;
    if ((unsigned)n == bytes) return 0;
    if (n % rs) {
        memset(dta + n, 0, rs - n % rs);
        (*done)++;
        return 3;
    }
    return 1;
}

static void set_cur(struct fcb *fc, uint32_t rec)
{
    fc->cur_block = rec / 128;
    fc->cur_rec = rec % 128;
}

void fcb_functions(struct armregs *f)
{
    int attr, r;
    uint8_t *ext;
    struct fcb *fc;
    unsigned done;
    switch (AH(f)) {
    case 0x0F: case 0x10: case 0x11: case 0x12: case 0x13: case 0x16: case 0x17:
        switch (AH(f)) {
        case 0x0F: r = fcb_open(f); break;
        case 0x10: r = fcb_close(f); break;
        case 0x11: r = fcb_search(f, 1); break;
        case 0x12: r = fcb_search(f, 0); break;
        case 0x13: r = fcb_delete(f); break;
        case 0x16: r = fcb_create(f); break;
        default:   r = fcb_rename(f); break;
        }
        if (r < 0) { fcb_err(-r); set_al(f, 0xFF); }
        else set_al(f, 0);
        break;
    case 0x14: case 0x15: {
        fc = fcb_of(f, &attr, &ext);
        uint32_t rec = (uint32_t)fc->cur_block * 128 + fc->cur_rec;
        r = fcb_io(fc, AH(f) == 0x15, rec, 1, &done);
        if (done) set_cur(fc, rec + 1);
        set_al(f, r);
        break;
    }
    case 0x21: case 0x22: {
        fc = fcb_of(f, &attr, &ext);
        uint32_t rec = rand_rec(fc);
        set_cur(fc, rec);
        r = fcb_io(fc, AH(f) == 0x22, rec, 1, &done);
        set_al(f, r);
        break;
    }
    case 0x23: {
        fc = fcb_of(f, &attr, &ext);
        struct dpb *d;
        uint16_t cl;
        int drive = fcb_dir(fc, &d, &cl);
        struct dirent de;
        if (drive < 0 || has_wild(fc->name) || dir_find(d, cl, fc->name, attr & (ATTR_HIDDEN | ATTR_SYSTEM), 0, &de, 0, 0) < 0) {
            set_al(f, 0xFF);
            break;
        }
        unsigned rs = fc->rec_size ? fc->rec_size : 128;
        uint32_t n = (de.size + rs - 1) / rs;
        fc->rand_rec = rs >= 64 ? (fc->rand_rec & 0xFF000000u) | (n & 0xFFFFFF) : n;
        set_al(f, 0);
        break;
    }
    case 0x24:
        fc = fcb_of(f, &attr, &ext);
        {
            uint32_t rec = (uint32_t)fc->cur_block * 128 + fc->cur_rec;
            if (fc->rec_size >= 64) fc->rand_rec = (fc->rand_rec & 0xFF000000u) | (rec & 0xFFFFFF);
            else fc->rand_rec = rec;
        }
        break;
    case 0x27: case 0x28: {
        fc = fcb_of(f, &attr, &ext);
        uint32_t rec = rand_rec(fc);
        unsigned count = CX(f);
        if (AH(f) == 0x27 && count == 0) { set_al(f, 0); break; }
        r = fcb_io(fc, AH(f) == 0x28, rec, count, &done);
        if (AH(f) == 0x28 && count == 0) done = 0;
        rec += done;
        if (fc->rec_size >= 64) fc->rand_rec = (fc->rand_rec & 0xFF000000u) | (rec & 0xFFFFFF);
        else fc->rand_rec = rec;
        set_cur(fc, rec);
        f->r2 = done;
        set_al(f, r);
        break;
    }
    case 0x29:
        set_al(f, parse_fcb_name(f));
        break;
    }
}
