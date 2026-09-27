/*
 * file.c - handles and files: the SFT and JFT (DOS/HANDLE.ASM, OPEN.ASM,
 * CREATE.ASM, CLOSE.ASM), reading and writing through the FAT
 * (DISK.ASM, DISK2.ASM, DISK3.ASM), and the path calls
 * (MKDIR/RMDIR/CHDIR/UNLINK/RENAME/CHMOD, 39h-47h, 56h, 57h, 5Ah-5Ch, 60h,
 * 67h, 68h, 6Ch).
 */
#include "dos.h"

void dir_cache_reset(void);

/* ---------------------------------------------------------- the SFT */

struct sft *sft_get(unsigned idx)
{
    for (struct sftblock *b = LOL.sft_head; b && b != (struct sftblock *)0xFFFFFFFFu; b = b->next) {
        if (idx < b->count) return &b->e[idx];
        idx -= b->count;
    }
    return 0;
}

int sft_count(void)
{
    int n = 0;
    for (struct sftblock *b = LOL.sft_head; b && b != (struct sftblock *)0xFFFFFFFFu; b = b->next) n += b->count;
    return n;
}

int sft_alloc(unsigned *idx)
{
    unsigned i = 0;
    for (struct sftblock *b = LOL.sft_head; b && b != (struct sftblock *)0xFFFFFFFFu; b = b->next) {
        for (unsigned k = 0; k < b->count; k++, i++) {
            if (b->e[k].ref_count == 0) {
                memset(&b->e[k], 0, sizeof b->e[k]);
                b->e[k].ref_count = 0xFFFF;     /* busy while being opened */
                *idx = i;
                return 0;
            }
        }
    }
    return -E_TOOMANY;
}

uint8_t *jft_ptr(struct psp *p, unsigned *size)
{
    if (size) *size = p->jftsize;
    return (uint8_t *)p->jftptr;
}

struct sft *handle_sft(unsigned h, unsigned *sfn)
{
    unsigned size;
    uint8_t *jft = jft_ptr(PSP(cur_psp), &size);
    if (h >= size || jft[h] == 0xFF) return 0;
    struct sft *s = sft_get(jft[h]);
    if (!s || !s->ref_count) return 0;
    if (sfn) *sfn = jft[h];
    return s;
}

int jft_alloc(void)
{
    unsigned size;
    uint8_t *jft = jft_ptr(PSP(cur_psp), &size);
    for (unsigned i = 0; i < size; i++) if (jft[i] == 0xFF) return i;
    return -E_TOOMANY;
}

/* -------------------------------------------------------- date/time */

void dos_now(uint16_t *date, uint16_t *time)
{
    uint8_t t[6];
    clock_read(t);
    int y, m, d;
    days_to_ymd(t[0] | (t[1] << 8), &y, &m, &d);
    *date = ((y - 1980) << 9) | (m << 5) | d;
    *time = (t[3] << 11) | (t[2] << 5) | (t[5] / 2);
}

/* ----------------------------------------------------- cluster maps */

/* the cluster holding relative cluster rel of the file (0 if none) */
static int file_cluster(struct sft *s, unsigned rel, unsigned *out)
{
    struct dpb *d = (struct dpb *)s->devptr;
    if (!s->first_cluster) { *out = 0; return 0; }
    unsigned from = s->first_cluster, steps = rel;
    if (s->last_cluster >= 2 && rel >= s->rel_cluster) {
        from = s->last_cluster;
        steps = rel - s->rel_cluster;
    }
    unsigned cl;
    int e = fat_walk(d, from, steps, &cl);
    if (e < 0) return e;
    if (cl) { s->rel_cluster = rel; s->last_cluster = cl; }
    *out = cl;
    return 0;
}

/* make the chain at least `need` clusters long; returns how many it has */
static int file_extend(struct sft *s, unsigned need)
{
    struct dpb *d = (struct dpb *)s->devptr;
    unsigned have = 0, last = 0;
    if (s->first_cluster) {
        /* find the end of the chain (from the cached position when possible) */
        unsigned cl = s->first_cluster, n = 1;
        if (s->last_cluster >= 2) { cl = s->last_cluster; n = s->rel_cluster + 1; }
        for (;;) {
            uint32_t v = fat_get(d, cl);
            if (v == 0xFFFFFFFFu) return -E_GENFAIL;
            if (fat_eof(d, v) || v < 2 || n >= need) break;
            cl = v; n++;
        }
        have = n; last = cl;
        if (have >= need) return have;
    }
    while (have < need) {
        int cl = fat_alloc(d, last);
        if (cl < 0) return cl;
        if (cl == 0) break;                     /* disk full */
        if (!s->first_cluster) s->first_cluster = cl;
        last = cl;
        have++;
    }
    return have;
}

/* --------------------------------------------------------- read/write */

int file_read(struct sft *s, uint8_t *buf, unsigned n)
{
    if (s->flags & SF_REMOTE) return redir_rw(0, s, buf, n);
    if (s->flags & SF_DEVICE) return dev_read(s, buf, n);
    struct dpb *d = (struct dpb *)s->devptr;
    if (s->position >= s->size) return 0;
    if (n > s->size - s->position) n = s->size - s->position;
    uint32_t csize = 512u << d->cluster_shift;
    unsigned done = 0;
    while (done < n) {
        uint32_t pos = s->position;
        unsigned rel = pos / csize, offc = pos % csize, cl;
        int e = file_cluster(s, rel, &cl);
        if (e < 0) return done ? (int)done : e;
        if (!cl) break;
        uint32_t sector = clus2sec(d, cl) + offc / 512;
        unsigned boff = pos % 512, left = n - done;
        if (boff == 0 && left >= 512) {
            /* whole sectors straight into the caller's buffer */
            unsigned nsec = (csize - offc) / 512;
            if (nsec > left / 512) nsec = left / 512;
            /* take in following clusters if they are contiguous on the disk */
            unsigned ccl = cl, crel = rel;
            while (nsec < left / 512 && nsec < 120) {
                uint32_t v = fat_get(d, ccl);
                if (v != ccl + 1 || fat_eof(d, v)) break;
                ccl = v; crel++;
                unsigned more = csize / 512;
                if (nsec + more > left / 512) more = left / 512 - nsec;
                nsec += more;
                s->rel_cluster = crel; s->last_cluster = ccl;
            }
            buf_sync_range(d, sector, nsec, 0);
            e = dsk_io(d, 0, sector, nsec, buf + done, AREA_DATA);
            if (e < 0) return done ? (int)done : e;
            done += nsec * 512;
            s->position += nsec * 512;
        } else {
            struct buf *b = getbuf(d, sector, AREA_DATA, 0);
            if (!b) return done ? (int)done : -E_ACCESS;
            unsigned k = 512 - boff;
            if (k > left) k = left;
            memcpy(buf + done, b->data + boff, k);
            done += k;
            s->position += k;
        }
    }
    return done;
}

static void file_touched(struct sft *s)
{
    s->flags &= ~SF_CLEAN;
}

/* set the size to the position (write of 0 bytes) */
static int file_setsize(struct sft *s)
{
    struct dpb *d = (struct dpb *)s->devptr;
    uint32_t csize = 512u << d->cluster_shift;
    uint32_t pos = s->position;
    unsigned need = (pos + csize - 1) / csize;
    file_touched(s);
    if (pos > s->size) {
        int have = file_extend(s, need);
        if (have < 0) return have;
        if ((unsigned)have < need) pos = have * csize;      /* disk full */
        s->size = pos;
        return 0;
    }
    if (need == 0) {
        if (s->first_cluster) fat_free_chain(d, s->first_cluster);
        s->first_cluster = 0;
    } else if (s->first_cluster) {
        unsigned cl;
        int e = fat_walk(d, s->first_cluster, need - 1, &cl);
        if (e < 0) return e;
        if (cl) {
            uint32_t next = fat_get(d, cl);
            if (next != 0xFFFFFFFFu && !fat_eof(d, next) && next >= 2) {
                fat_set(d, cl, fat_eofval(d));
                fat_free_chain(d, next);
            }
        }
    }
    s->rel_cluster = 0;
    s->last_cluster = 0;
    s->size = pos;
    return 0;
}

int file_write(struct sft *s, const uint8_t *buf, unsigned n)
{
    if (s->flags & SF_REMOTE) return redir_rw(1, s, buf, n);
    if (s->flags & SF_DEVICE) return dev_write(s, buf, n);
    struct dpb *d = (struct dpb *)s->devptr;
    if (n == 0) { int e = file_setsize(s); return e < 0 ? e : 0; }
    uint32_t csize = 512u << d->cluster_shift;
    uint32_t end = s->position + n;
    if (end < s->position) { n = 0xFFFFFFFFu - s->position; end = 0xFFFFFFFFu; }
    unsigned need = (end + csize - 1) / csize;
    int have = file_extend(s, need);
    if (have < 0) return have;
    if ((unsigned)have < need) {
        uint32_t cap = (uint32_t)have * csize;
        if (cap <= s->position) return 0;       /* disk full: nothing written */
        n = cap - s->position;
    }
    file_touched(s);
    unsigned done = 0;
    while (done < n) {
        uint32_t pos = s->position;
        unsigned rel = pos / csize, offc = pos % csize, cl;
        int e = file_cluster(s, rel, &cl);
        if (e < 0) break;
        if (!cl) break;
        uint32_t sector = clus2sec(d, cl) + offc / 512;
        unsigned boff = pos % 512, left = n - done;
        if (boff == 0 && left >= 512) {
            unsigned nsec = (csize - offc) / 512;
            if (nsec > left / 512) nsec = left / 512;
            unsigned ccl = cl, crel = rel;
            while (nsec < left / 512 && nsec < 120) {
                uint32_t v = fat_get(d, ccl);
                if (v != ccl + 1 || fat_eof(d, v)) break;
                ccl = v; crel++;
                unsigned more = csize / 512;
                if (nsec + more > left / 512) more = left / 512 - nsec;
                nsec += more;
                s->rel_cluster = crel; s->last_cluster = ccl;
            }
            buf_sync_range(d, sector, nsec, 1);
            e = dsk_io(d, 1, sector, nsec, (void *)(buf + done), AREA_DATA);
            if (e < 0) { if (!done) return e; break; }
            done += nsec * 512;
            s->position += nsec * 512;
        } else {
            unsigned k = 512 - boff;
            if (k > left) k = left;
            /* no need to read a sector we overwrite beyond the old end */
            int noread = boff == 0 && pos >= s->size && ((pos & ~511u) >= ((s->size + 511) & ~511u));
            struct buf *b = getbuf(d, sector, AREA_DATA, noread);
            if (!b) { if (!done) return -E_ACCESS; break; }
            if (noread) memset(b->data, 0, 512);
            memcpy(b->data + boff, buf + done, k);
            buf_dirty(b);
            done += k;
            s->position += k;
        }
        if (s->position > s->size) s->size = s->position;
    }
    if (s->position > s->size) s->size = s->position;
    return done;
}

/* write the SFT's state to its directory entry */
static int file_update_dir(struct sft *s)
{
    struct dpb *d = (struct dpb *)s->devptr;
    struct buf *b = getbuf(d, s->dir_sector, AREA_DIR, 0);
    if (!b) return -E_GENFAIL;
    struct dirent *de = (struct dirent *)(b->data + s->dir_index * 32);
    if (memcmp(de->name, s->name, 11)) return 0;     /* the entry went away */
    if (!(s->flags & SF_NODATE)) STAMP_NOW(s);
    de->size = (de->attr & ATTR_DIR) ? 0 : s->size;
    de->cluster = s->first_cluster;
    de->time = s->time;
    de->date = s->date;
    de->attr |= ATTR_ARCHIVE;
    buf_dirty(b);
    return 0;
}

int file_commit(struct sft *s)
{
    if (s->flags & SF_REMOTE) return redir_sft(0x07, s);
    if (s->flags & SF_DEVICE) return 0;
    if (!(s->flags & SF_CLEAN) || (s->flags & SF_NODATE)) {
        int e = file_update_dir(s);
        if (e < 0) return e;
        s->flags |= SF_CLEAN;
    }
    return flush_bufs(s->flags & SF_DRIVE_MASK);
}

int file_close_sft(struct sft *s)
{
    int e = 0;
    if (s->ref_count && s->ref_count != 0xFFFF) s->ref_count--;
    if (s->flags & SF_REMOTE) return s->ref_count ? 0 : redir_sft(0x06, s);
    if (s->ref_count) {
        /* still open through another handle: DOS 4 (CLOSE.ASM CloseEntry)
           still writes a dirty file's directory entry on every close */
        if (!(s->flags & SF_DEVICE) && (!(s->flags & SF_CLEAN) || (s->flags & SF_NODATE))) {
            e = file_update_dir(s);
            if (e >= 0) { s->flags |= SF_CLEAN; e = flush_bufs(s->flags & SF_DRIVE_MASK); }
        }
        return e < 0 ? e : 0;
    }
    if (s->flags & SF_DEVICE) {
        struct devhdr *dv = (struct devhdr *)s->devptr;
        if (dv->attr & DEVA_OPENCLOSE) {
            struct reqhdr q;
            memset(&q, 0, sizeof q);
            q.len = 13; q.cmd = CMD_CLOSE;
            devcall(dv, &q);
        }
        return 0;
    }
    if (!(s->flags & SF_CLEAN) || (s->flags & SF_NODATE)) {
        e = file_update_dir(s);
        if (e >= 0) e = flush_bufs(s->flags & SF_DRIVE_MASK);
    }
    return e;
}

uint32_t sft_seek(struct sft *s, int whence, int32_t off)
{
    uint32_t base = whence == 0 ? 0 : whence == 1 ? s->position : s->size;
    uint32_t np = base + (uint32_t)off;
    if (s->flags & SF_DEVICE) return 0;
    s->position = np;
    return np;
}

/* ---------------------------------------------------- open / create */

static void sft_fill_file(struct sft *s, const struct pathinfo *pi, const struct dirent *de, const struct dirloc *loc, int mode)
{
    s->mode = mode;
    s->attr = de->attr;
    s->flags = pi->drive | SF_CLEAN;
    s->devptr = (uint32_t)pi->dpb;
    s->first_cluster = de->cluster;
    s->time = de->time;
    s->date = de->date;
    s->size = (de->attr & ATTR_DIR) ? 0 : de->size;
    s->position = 0;
    s->rel_cluster = 0;
    s->last_cluster = 0;
    s->dir_sector = loc->sector;
    s->dir_index = loc->index;
    memcpy(s->name, de->name, 11);
    s->owner_psp = cur_psp;
}

static void sft_fill_dev(struct sft *s, const struct pathinfo *pi, int mode)
{
    struct devhdr *dv = pi->dev;
    s->mode = mode;
    s->attr = 0;
    s->flags = SF_DEVICE | DI_NOEOF | (dv->attr & (DI_STDIN | DI_STDOUT | DI_NUL | DI_CLOCK | DI_SPECIAL));
    s->devptr = (uint32_t)dv;
    memcpy(s->name, pi->name11, 11);
    s->owner_psp = cur_psp;
    STAMP_NOW(s);
    if (dv->attr & DEVA_OPENCLOSE) {
        struct reqhdr q;
        memset(&q, 0, sizeof q);
        q.len = 13; q.cmd = CMD_OPEN;
        devcall(dv, &q);
    }
}

static int check_mode(int mode)
{
    if ((mode & 7) > 2) return -E_BADACCESS;
    if (((mode >> 4) & 7) > 4) return -E_BADACCESS;
    return 0;
}

/* open (create=0) or create (create=1: truncate / newonly: fail if it exists)
   a path into the SFT entry s */
int file_open_sft(const char *path, int mode, struct sft *s, int create, int attr, int newonly)
{
    struct pathinfo pi;
    int e = resolve(path, &pi, 0);
    if (e < 0) return e;
    if (pi.dev) { sft_fill_dev(s, &pi, mode); return 1; }
    if (pi.isroot) return create ? -E_ACCESS : -E_NOFILE;
    if (pi.remote) return redir_open(&pi, s, mode, create, attr, newonly);
    struct dirent de;
    struct dirloc loc;
    int found = dir_lookup_path(&pi, &de, &loc, ATTR_HIDDEN | ATTR_SYSTEM | ATTR_DIR | (create ? ATTR_VOLUME : 0));
    if (found < 0 && found != -E_NOFILE) return found;
    if (!create) {
        if (found < 0) return -E_NOFILE;
        if (de.attr & (ATTR_DIR | ATTR_VOLUME)) return -E_ACCESS;
        if ((mode & 7) != 0 && (de.attr & ATTR_RDONLY)) return -E_ACCESS;
        sft_fill_file(s, &pi, &de, &loc, mode);
        return 1;
    }
    attr &= ATTR_RDONLY | ATTR_HIDDEN | ATTR_SYSTEM | ATTR_ARCHIVE | ATTR_VOLUME;
    if (found >= 0) {
        if (newonly) return -E_EXISTS;
        if (de.attr & (ATTR_DIR | ATTR_VOLUME | ATTR_RDONLY)) return -E_ACCESS;
        if (de.cluster) fat_free_chain(pi.dpb, de.cluster);
        de.cluster = 0;
        de.size = 0;
        de.attr = attr | ((attr & ATTR_VOLUME) ? 0 : ATTR_ARCHIVE);
    } else {
        int idx = dir_new_entry(pi.dpb, pi.dircl, &loc);
        if (idx < 0) return idx;
        memset(&de, 0, sizeof de);
        memcpy(de.name, pi.name11, 11);
        de.attr = attr | ((attr & ATTR_VOLUME) ? 0 : ATTR_ARCHIVE);
    }
    STAMP_NOW(&de);
    e = dir_write(pi.dpb, &loc, &de);
    if (e < 0) return e;
    /* the new entry goes to the disk now (a write-protected diskette fails here, as DOS) */
    e = flush_bufs(pi.drive);
    if (e < 0) return fail_err ? -E_ACCESS : e;
    if (de.attr & ATTR_VOLUME) label_to_boot(pi.dpb, de.name);
    sft_fill_file(s, &pi, &de, &loc, mode);
    s->flags &= ~SF_CLEAN;
    return found >= 0 ? 3 : 2;
}

/* allocate handle + SFT and open/create; returns handle or -err; *action 1/2/3 */
static int do_open(const char *path, int mode, int create, int attr, int newonly, int *action)
{
    int h = jft_alloc();
    if (h < 0) return h;
    unsigned idx;
    int e = sft_alloc(&idx);
    if (e < 0) return e;
    struct sft *s = sft_get(idx);
    int r = file_open_sft(path, mode & 0xFF, s, create, attr, newonly);
    if (r < 0) { s->ref_count = 0; return r; }
    s->mode |= mode & 0x6000;           /* 6Ch: no INT 24h (bit 13), auto-commit (bit 14) */
    s->ref_count = 1;
    uint8_t *jft = jft_ptr(PSP(cur_psp), 0);
    jft[h] = idx;
    if (action) *action = r;
    return h;
}

int open_path(const char *path, int mode, int *ph)
{
    int e = check_mode(mode);
    if (e < 0) return e;
    int h = do_open(path, mode, 0, 0, 0, 0);
    if (h < 0) return h;
    *ph = h;
    return 0;
}

int create_path(const char *path, int attr, int mode, int newonly, int *ph)
{
    int h = do_open(path, mode, 1, attr, newonly, 0);
    if (h < 0) return h;
    *ph = h;
    return 0;
}

static int close_handle(unsigned h)
{
    unsigned size;
    uint8_t *jft = jft_ptr(PSP(cur_psp), &size);
    if (h >= size || jft[h] == 0xFF) return -E_BADHANDLE;
    struct sft *s = sft_get(jft[h]);
    jft[h] = 0xFF;
    if (!s || !s->ref_count) return -E_BADHANDLE;
    return file_close_sft(s);
}

void close_process_files(struct psp *p)
{
    unsigned size;
    uint8_t *jft = jft_ptr(p, &size);
    for (unsigned h = 0; h < size; h++) {
        if (jft[h] == 0xFF) continue;
        struct sft *s = sft_get(jft[h]);
        jft[h] = 0xFF;
        if (s && s->ref_count) file_close_sft(s);
    }
}

/* ---------------------------------------------------- directories */

static int is_current_dir(const char *full)
{
    for (int i = 0; i < n_cds; i++)
        if ((cds_tab[i].flags & CDS_VALID) && !(cds_tab[i].flags & CDS_JOIN) &&
            (!strcmp(cds_tab[i].path, full) || !strcmp(cds_tab[i].path, canon_prejoin))) return 1;
    return 0;
}

static int do_mkdir(const char *path)
{
    struct pathinfo pi;
    int e = resolve(path, &pi, 0);
    if (e < 0) return e;
    if (pi.dev || pi.isroot) return -E_ACCESS;
    if (strlen(pi.full) > 66) return -E_NOPATH;
    struct armregs rr;
    if (pi.remote) return redir_path(0x03, &pi, 0, &rr);
    struct dirent de;
    struct dirloc loc;
    int r = dir_lookup_path(&pi, &de, &loc, ATTR_HIDDEN | ATTR_SYSTEM | ATTR_DIR | ATTR_VOLUME);
    if (r >= 0) return -E_ACCESS;
    if (r != -E_NOFILE) return r;
    int idx = dir_new_entry(pi.dpb, pi.dircl, &loc);
    if (idx < 0) return idx;
    int cl = fat_alloc(pi.dpb, 0);
    if (cl < 0) return cl;
    if (cl == 0) return -E_ACCESS;
    uint16_t date, time;
    dos_now(&date, &time);
    uint32_t s = clus2sec(pi.dpb, cl);
    for (unsigned k = 0; k <= pi.dpb->cluster_mask; k++) {
        struct buf *b = getbuf(pi.dpb, s + k, AREA_DIR, 1);
        if (!b) return -E_GENFAIL;
        memset(b->data, 0, 512);
        if (k == 0) {
            struct dirent *dot = (struct dirent *)b->data;
            memset(dot, ' ', 11); dot[0].name[0] = '.';
            dot[0].attr = ATTR_DIR; dot[0].cluster = cl; dot[0].date = date; dot[0].time = time;
            memset(dot[1].name, ' ', 11); dot[1].name[0] = '.'; dot[1].name[1] = '.';
            dot[1].attr = ATTR_DIR; dot[1].cluster = pi.dircl; dot[1].date = date; dot[1].time = time;
        }
        buf_dirty(b);
    }
    memset(&de, 0, sizeof de);
    memcpy(de.name, pi.name11, 11);
    de.attr = ATTR_DIR;
    de.cluster = cl;
    de.date = date;
    de.time = time;
    e = dir_write(pi.dpb, &loc, &de);
    if (e < 0) return e;
    return flush_bufs(pi.drive);
}

static int do_rmdir(const char *path)
{
    struct pathinfo pi;
    int e = resolve(path, &pi, 0);
    if (e < 0) return e;
    if (pi.dev) return -E_ACCESS;
    if (pi.isroot) return -E_ACCESS;
    if (is_current_dir(pi.full)) return -E_CURDIR;
    struct armregs rr;
    if (pi.remote) return redir_path(0x01, &pi, 0, &rr);
    struct dirent de;
    struct dirloc loc;
    int r = dir_lookup_path(&pi, &de, &loc, ATTR_HIDDEN | ATTR_SYSTEM | ATTR_DIR);
    if (r == -E_NOFILE) return -E_NOPATH;
    if (r < 0) return r;
    if (!(de.attr & ATTR_DIR)) return -E_NOPATH;
    /* must be empty */
    for (unsigned i = 0;; i++) {
        struct buf *b;
        struct dirent *x = dir_get(pi.dpb, de.cluster, i, &b, 0, AREA_DIR);
        if (!x || x->name[0] == 0) break;
        if ((uint8_t)x->name[0] == 0xE5) continue;
        if (x->name[0] == '.') continue;
        return -E_ACCESS;
    }
    struct buf *b = getbuf(pi.dpb, loc.sector, AREA_DIR, 0);
    if (!b) return -E_GENFAIL;
    b->data[loc.index * 32] = 0xE5;
    buf_dirty(b);
    if (de.cluster) fat_free_chain(pi.dpb, de.cluster);
    dir_cache_reset();
    return flush_bufs(pi.drive);
}

static int do_chdir(const char *path)
{
    struct pathinfo pi;
    /* a bare "d:" is not a directory name (4.00 fails it) */
    if (path[0] && path[1] == ':' && !path[2]) return -E_NOPATH;
    if (!path[0]) return -E_NOPATH;
    {
        /* only "\" and "d:\" may end in a separator */
        size_t l = strlen(path);
        const char *q = path[1] == ':' ? path + 2 : path;
        if ((path[l - 1] == '\\' || path[l - 1] == '/') && strlen(q) > 1) return -E_NOPATH;
    }
    int e = resolve(path, &pi, 0);
    if (e < 0) return e;
    if (pi.dev) return -E_NOPATH;
    /* the CDS of the drive named; its text is the path after SUBST, before JOIN */
    struct cds *c = get_cds(canon_ldrive);
    char text[84];
    strcpy(text, canon_prejoin);
    uint16_t cl = 0;
    if (pi.remote) {
        struct armregs rr;
        if (!pi.isroot && (e = redir_path(0x05, &pi, 0, &rr)) < 0) return e == -E_NOFILE ? -E_NOPATH : e;
    } else if (!pi.isroot) {
        struct dirent de;
        int r = dir_lookup_path(&pi, &de, 0, ATTR_HIDDEN | ATTR_SYSTEM | ATTR_DIR);
        if (r == -E_NOFILE || (r >= 0 && !(de.attr & ATTR_DIR))) return -E_NOPATH;
        if (r < 0) return r;
        cl = de.cluster;
    }
    if (strlen(text) > 66) return -E_NOPATH;
    strcpy(c->path, text);
    c->cluster = cl;
    return 0;
}

static int do_unlink(const char *path)
{
    struct pathinfo pi;
    int e = resolve(path, &pi, 0);
    if (e < 0) return e;
    if (pi.dev || pi.isroot) return -E_ACCESS;
    struct armregs rr;
    if (pi.remote) return redir_path(0x13, &pi, 0, &rr);
    struct dirent de;
    struct dirloc loc;
    int r = dir_lookup_path(&pi, &de, &loc, ATTR_HIDDEN | ATTR_SYSTEM);
    if (r < 0) return r;
    if (de.attr & ATTR_RDONLY) return -E_ACCESS;
    struct buf *b = getbuf(pi.dpb, loc.sector, AREA_DIR, 0);
    if (!b) return -E_GENFAIL;
    b->data[loc.index * 32] = 0xE5;
    buf_dirty(b);
    if (de.cluster) fat_free_chain(pi.dpb, de.cluster);
    return flush_bufs(pi.drive);
}

static int do_rename(const char *from, const char *to)
{
    struct pathinfo po, pn;
    char c1[80], c2[80];
    int d1, d2;
    int e = canon_path(from, c1, &d1, 0);
    if (e == -E_BADDRIVE) return -E_NOPATH;
    if (e < 0) return e;
    e = canon_path(to, c2, &d2, 0);
    if (e == -E_BADDRIVE) return -E_NOPATH;
    if (e < 0) return e;
    if (d1 != d2) return -E_NOTSAME;
    e = resolve(from, &po, 0);
    if (e < 0) return e;
    e = resolve(to, &pn, 0);
    if (e < 0) return e == -E_NOFILE ? -E_ACCESS : e;
    if (po.dev || pn.dev || po.isroot || pn.isroot) return -E_ACCESS;
    if (po.drive != pn.drive) return -E_NOTSAME;
    if (po.remote) {
        struct armregs rr;
        memset(&rr, 0, sizeof rr);
        rr.r4 = (uint32_t)po.full;
        rr.r3 = (uint32_t)pn.full;
        rr.r6 = (uint32_t)get_cds(po.drive);
        return redir_call(0x11, &rr);
    }
    struct dirent de, x;
    struct dirloc lo, ln;
    int r = dir_lookup_path(&po, &de, &lo, ATTR_HIDDEN | ATTR_SYSTEM | ATTR_DIR);
    if (r < 0) return r;
    if (is_current_dir(po.full)) return -E_CURDIR;
    r = dir_lookup_path(&pn, &x, 0, ATTR_HIDDEN | ATTR_SYSTEM | ATTR_DIR | ATTR_VOLUME);
    if (r >= 0) return -E_ACCESS;
    if (r != -E_NOFILE) return r;
    if (pn.dircl == po.dircl) {
        memcpy(de.name, pn.name11, 11);
        e = dir_write(po.dpb, &lo, &de);
        if (e < 0) return e;
        return flush_bufs(po.drive);
    }
    if (de.attr & ATTR_DIR) return -E_ACCESS;       /* directories are not moved */
    int idx = dir_new_entry(pn.dpb, pn.dircl, &ln);
    if (idx < 0) return idx;
    memcpy(de.name, pn.name11, 11);
    e = dir_write(pn.dpb, &ln, &de);
    if (e < 0) return e;
    struct buf *b = getbuf(po.dpb, lo.sector, AREA_DIR, 0);
    if (!b) return -E_GENFAIL;
    b->data[lo.index * 32] = 0xE5;
    buf_dirty(b);
    return flush_bufs(po.drive);
}

static int do_chmod(const char *path, int set, unsigned *attr)
{
    struct pathinfo pi;
    int e = resolve(path, &pi, 0);
    if (e < 0) return e;
    if (pi.dev) { if (!set) *attr = 0; return 0; }
    if (pi.isroot) return -E_NOFILE;
    if (pi.remote) {
        struct armregs rr;
        e = redir_path(set ? 0x0E : 0x0F, &pi, set ? *attr : 0, &rr);
        if (e == 0 && !set) *attr = rr.r0 & 0xFF;
        return e;
    }
    struct dirent de;
    struct dirloc loc;
    int r = dir_lookup_path(&pi, &de, &loc, ATTR_HIDDEN | ATTR_SYSTEM | ATTR_DIR);
    if (r < 0) return r;
    if (!set) { *attr = de.attr; return 0; }
    unsigned a = *attr;
    if (a & (ATTR_VOLUME | ATTR_DIR | 0xC0)) {
        if ((a & ATTR_DIR) != (de.attr & ATTR_DIR) || (a & (ATTR_VOLUME | 0xC0))) return -E_ACCESS;
    }
    de.attr = (de.attr & ATTR_DIR) | (a & (ATTR_RDONLY | ATTR_HIDDEN | ATTR_SYSTEM | ATTR_ARCHIVE));
    e = dir_write(pi.dpb, &loc, &de);
    if (e < 0) return e;
    return flush_bufs(pi.drive);
}

/* ------------------------------------------------------ INT 21h */

/* DOS 4 extended attributes (43h/57h AL=02h-04h): a FAT disk has none, so
   "get" returns an empty list (count word 0, CX = 2) and "set" succeeds */
static void ea_empty(struct armregs *f)
{
    if (AL(f) != 4) {
        uint8_t *d = (uint8_t *)f->r5;
        if (CX(f) >= 2) { d[0] = 0; d[1] = 0; f->r2 = 2; }
        else f->r2 = 2;
    }
}

static void ret(struct armregs *f, int r)
{
    if (r < 0) sys_err(f, -r);
}

static void set_handle_count(struct armregs *f)
{
    struct psp *p = PSP(cur_psp);
    unsigned want = BX(f), size;
    uint8_t *old = jft_ptr(p, &size);
    if (want <= size) { sys_ok(f); return; }
    if (want <= 20 && (uint32_t)old == (uint32_t)p + 0x18) { sys_ok(f); return; }
    unsigned seg, largest;
    int e = mem_alloc((want + 15) >> 4, &seg, &largest);
    if (e < 0) { sys_err(f, -e); return; }
    uint8_t *n = (uint8_t *)((uint32_t)seg << 4);
    memset(n, 0xFF, want);
    memcpy(n, old, size);
    if ((uint32_t)old != (uint32_t)p + 0x18) mem_free(SEG(old));
    p->jftptr = (uint32_t)n;
    p->jftsize = want;
}

static void create_temp(struct armregs *f)
{
    char *path = (char *)f->r3;
    int len = strlen(path);
    if (len && path[len - 1] != '\\' && path[len - 1] != '/' && path[len - 1] != ':') path[len++] = '\\';
    uint16_t date, time;
    dos_now(&date, &time);
    uint32_t seed = ((uint32_t)date << 16 | time) ^ (BDA32(0x6C) << 3);
    for (int tries = 0; tries < 100; tries++) {
        ksnprintf(path + len, 9, "%08X", seed + tries * 7919);
        int h;
        int e = create_path(path, CX(f), 2, 1, &h);
        if (e == 0) { f->r0 = h; return; }
        if (e != -E_EXISTS) { sys_err(f, -e); return; }
    }
    sys_err(f, E_ACCESS);
}

static void ext_open(struct armregs *f)
{
    unsigned mode = BX(f), action = DX(f) & 0xFF;   /* DH: 4.0 flags (01h = code page not checked) */
    const char *path = (const char *)f->r4;
    if (AL(f) != 0 || (action & 0x0F) > 2 || ((action >> 4) & 0x0F) > 1 || !action || check_mode(mode & 0xFF) < 0) {
        sys_err(f, E_BADACCESS); return;
    }
    if (mode & 0x2000) no_i24 = 1;
    /* exists? */
    int h = jft_alloc();
    if (h < 0) { sys_err(f, -h); return; }
    int act = 0;
    h = do_open(path, mode, 0, 0, 0, &act);
    if (h >= 0) {
        if ((action & 0x0F) == 0) { close_handle(h); sys_err(f, E_EXISTS); return; }
        if ((action & 0x0F) == 2) {
            close_handle(h);
            h = do_open(path, mode, 1, CX(f), 0, &act);
            if (h < 0) { sys_err(f, -h); return; }
            f->r0 = h; f->r2 = 3;
            return;
        }
        f->r0 = h; f->r2 = 1;
        return;
    }
    if (h != -E_NOFILE) { sys_err(f, -h); return; }
    if (!(action & 0xF0)) { sys_err(f, E_NOFILE); return; }
    h = do_open(path, mode, 1, CX(f), 1, &act);
    if (h < 0) { sys_err(f, -h); return; }
    f->r0 = h; f->r2 = 2;
}

void file_functions(struct armregs *f)
{
    int r, h;
    unsigned sfn;
    struct sft *s;
    switch (AH(f)) {
    case 0x39: ret(f, do_mkdir((const char *)f->r3)); break;
    case 0x3A: ret(f, do_rmdir((const char *)f->r3)); break;
    case 0x3B: ret(f, do_chdir((const char *)f->r3)); break;
    case 0x3C:
        r = create_path((const char *)f->r3, CX(f), 2, 0, &h);
        if (r < 0) sys_err(f, -r); else f->r0 = h;
        break;
    case 0x5B:
        r = create_path((const char *)f->r3, CX(f), 2, 1, &h);
        if (r < 0) sys_err(f, -r); else f->r0 = h;
        break;
    case 0x3D:
        r = open_path((const char *)f->r3, AL(f), &h);
        if (r < 0) sys_err(f, -r); else f->r0 = h;
        break;
    case 0x3E:
        ret(f, close_handle(BX(f)));
        break;
    case 0x3F:
        s = handle_sft(BX(f), 0);
        if (!s) { sys_err(f, E_BADHANDLE); break; }
        if ((s->mode & 7) == 1) { sys_err(f, E_ACCESS); break; }
        if (s->mode & 0x2000) no_i24 = 1;
        r = file_read(s, (uint8_t *)f->r3, CX(f));
        if (r < 0) sys_err(f, -r); else f->r0 = r;
        break;
    case 0x40:
        s = handle_sft(BX(f), 0);
        if (!s) { sys_err(f, E_BADHANDLE); break; }
        if ((s->mode & 7) == 0) { sys_err(f, E_ACCESS); break; }
        if (s->mode & 0x2000) no_i24 = 1;
        r = file_write(s, (const uint8_t *)f->r3, CX(f));
        if (r < 0) sys_err(f, -r); else f->r0 = r;
        if (r >= 0 && (s->mode & 0x4000)) file_commit(s);
        break;
    case 0x41: ret(f, do_unlink((const char *)f->r3)); break;
    case 0x42: {
        s = handle_sft(BX(f), 0);
        if (!s) { sys_err(f, E_BADHANDLE); break; }
        if (AL(f) > 2) { sys_err(f, E_INVFN); break; }
        int32_t off = (int32_t)((CX(f) << 16) | DX(f));
        uint32_t np = sft_seek(s, AL(f), off);
        f->r0 = np & 0xFFFF;
        f->r3 = np >> 16;
        break;
    }
    case 0x43: {
        unsigned a = CX(f);
        if (AL(f) >= 2 && AL(f) <= 4) {
            /* extended attributes of a path: the file must exist */
            r = do_chmod((const char *)f->r3, 0, &a);
            if (r < 0) sys_err(f, -r); else ea_empty(f);
            break;
        }
        if (AL(f) > 1) { sys_err(f, E_INVFN); break; }
        r = do_chmod((const char *)f->r3, AL(f), &a);
        if (r < 0) sys_err(f, -r); else if (AL(f) == 0) f->r2 = a;
        break;
    }
    case 0x45:
        s = handle_sft(BX(f), &sfn);
        if (!s) { sys_err(f, E_BADHANDLE); break; }
        h = jft_alloc();
        if (h < 0) { sys_err(f, -h); break; }
        jft_ptr(PSP(cur_psp), 0)[h] = sfn;
        s->ref_count++;
        f->r0 = h;
        break;
    case 0x46: {
        s = handle_sft(BX(f), &sfn);
        unsigned size;
        uint8_t *jft = jft_ptr(PSP(cur_psp), &size);
        if (!s || CX(f) >= size) { sys_err(f, E_BADHANDLE); break; }
        if (CX(f) == BX(f)) break;
        if (jft[CX(f)] != 0xFF) close_handle(CX(f));
        jft[CX(f)] = sfn;
        s->ref_count++;
        break;
    }
    case 0x47: {
        int drive = DL(f) ? DL(f) - 1 : cur_drive;
        struct cds *c = get_cds(drive);
        if (!drive_usable(drive)) { sys_err(f, E_BADDRIVE); break; }
        int err;
        if (!drive_remote(drive) && !drive_dpb(drive, &err)) { sys_err(f, err); break; }
        const char *rest = c->path + cds_rootlen(c);
        while (*rest == '\\') rest++;
        strcpy((char *)f->r4, rest);
        f->r0 = 0x0100;
        break;
    }
    case 0x56: ret(f, do_rename((const char *)f->r3, (const char *)f->r5)); break;
    case 0x57:
        s = handle_sft(BX(f), 0);
        if (!s) { sys_err(f, E_BADHANDLE); break; }
        if (AL(f) == 0) { f->r2 = s->time; f->r3 = s->date; }
        else if (AL(f) == 1) {
            s->time = CX(f); s->date = DX(f);
            s->flags |= SF_NODATE;
            s->flags &= ~SF_CLEAN;
        } else if (AL(f) >= 2 && AL(f) <= 4) {
            ea_empty(f);
        } else sys_err(f, E_INVFN);
        break;
    case 0x5A: create_temp(f); break;
    case 0x5C:
        s = handle_sft(BX(f), 0);
        if (!s) { sys_err(f, E_BADHANDLE); break; }
        if (AL(f) > 1) sys_err(f, E_INVFN);
        break;
    case 0x60: {
        char out[128];
        int drive;
        r = canon_path((const char *)f->r4, out, &drive, 1);
        if (r < 0) { sys_err(f, -r); break; }
        strcpy(out, canon_prejoin);             /* SUBST resolved, JOIN kept (as 4.00) */
        drive = out[0] - 'A';
        /* devices: "C:/CON" */
        char *last = out + 3;
        for (char *p = out + 3; *p; p++) if (*p == '\\') last = p + 1;
        char n11[11];
        if (*last && name_to_fcb(last, n11) == 0 && find_chardev(n11)) {
            char dn[13];
            fcb_to_name(n11, dn);
            char *dot = strchr(dn, '.');
            if (dot) *dot = 0;
            ksnprintf(out, sizeof out, "%c:/%s", 'A' + drive, dn);
        }
        strcpy((char *)f->r5, out);
        break;
    }
    case 0x67: set_handle_count(f); break;
    case 0x68: case 0x6A:
        s = handle_sft(BX(f), 0);
        if (!s) { sys_err(f, E_BADHANDLE); break; }
        ret(f, file_commit(s));
        break;
    case 0x6C: ext_open(f); break;
    }
}
