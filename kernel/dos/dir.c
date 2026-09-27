/*
 * dir.c - directories (DOS/DIR.ASM, DIR2.ASM): reading and writing entries,
 * searching with wildcards and attributes, growing subdirectories, and
 * walking a canonical path down from the root.
 */
#include "dos.h"

int attr_match(int fattr, int sattr)
{
    return ((fattr & ~sattr) & (ATTR_HIDDEN | ATTR_SYSTEM | ATTR_DIR)) == 0;
}

/* remember the last cluster walk of a subdirectory (sequential scans) */
static struct { uint8_t drive; uint16_t start; unsigned n; unsigned cl; } walkc = { 0xFF, 0, 0, 0 };

/* entry `index` of a directory: 1 found (de/buf/loc set), 0 past the end, <0 error */
static int dir_entry(struct dpb *d, uint16_t dircl, unsigned index, struct dirent **pde, struct buf **pb, struct dirloc *loc)
{
    uint32_t sector;
    if (dircl == 0) {
        if (index >= d->root_ents) return 0;
        sector = d->dir_sector + index / 16;
    } else {
        unsigned epc = 16u << d->cluster_shift;
        unsigned n = index / epc, cl;
        if (walkc.drive == d->drive && walkc.start == dircl && walkc.n <= n) {
            if (fat_walk(d, walkc.cl, n - walkc.n, &cl) < 0) return -E_GENFAIL;
        } else {
            if (fat_walk(d, dircl, n, &cl) < 0) return -E_GENFAIL;
        }
        if (!cl) return 0;
        walkc.drive = d->drive; walkc.start = dircl; walkc.n = n; walkc.cl = cl;
        sector = clus2sec(d, cl) + (index % epc) / 16;
    }
    struct buf *b = getbuf(d, sector, AREA_DIR, 0);
    if (!b) return -E_GENFAIL;
    *pde = (struct dirent *)(b->data + (index % 16) * 32);
    if (pb) *pb = b;
    if (loc) { loc->sector = sector; loc->index = index % 16; loc->entry = index; loc->dircl = dircl; }
    return 1;
}

void dir_cache_reset(void) { walkc.drive = 0xFF; }

struct dirent *dir_get(struct dpb *d, uint16_t dircl, unsigned index, struct buf **pb, struct dirloc *loc, int area)
{
    (void)area;
    struct dirent *de;
    if (dir_entry(d, dircl, index, &de, pb, loc) != 1) return 0;
    return de;
}

/*
 * Search a directory from entry `start` for name11 (may have '?') with the
 * attribute rules of DOS's Search.  Returns the entry index (de/loc filled),
 * -E_NOFILE, or another -err.
 */
int dir_find(struct dpb *d, uint16_t dircl, const char *name11, int sattr, unsigned start, struct dirent *out, struct dirloc *loc, int create)
{
    (void)create;
    sattr &= ~(ATTR_RDONLY | ATTR_ARCHIVE);
    if (sattr == ATTR_VOLUME) dircl = 0;
    for (unsigned i = start;; i++) {
        struct dirent *de;
        struct buf *b;
        int r = dir_entry(d, dircl, i, &de, &b, loc);
        if (r < 0) return r;
        if (r == 0) return -E_NOFILE;
        uint8_t c0 = de->name[0];
        if (c0 == 0) return -E_NOFILE;
        if (c0 == 0xE5) continue;
        if (!name_match(name11, de->name)) continue;
        if (de->attr & ATTR_VOLUME) {
            if (!(sattr & ATTR_VOLUME)) continue;
        } else {
            if (sattr == ATTR_VOLUME) continue;
            if (!attr_match(de->attr, sattr)) continue;
        }
        if (out) memcpy(out, de, sizeof *out);
        return i;
    }
}

/* a free slot in the directory, extending a subdirectory by a cluster if
   needed; returns the index or -err */
int dir_new_entry(struct dpb *d, uint16_t dircl, struct dirloc *loc)
{
    unsigned i;
    for (i = 0;; i++) {
        struct dirent *de;
        struct buf *b;
        int r = dir_entry(d, dircl, i, &de, &b, loc);
        if (r < 0) return r;
        if (r == 0) break;
        if ((uint8_t)de->name[0] == 0 || (uint8_t)de->name[0] == 0xE5) return i;
    }
    if (dircl == 0) return -E_ACCESS;       /* the root directory is full */
    /* grow: find the last cluster, add one, zero it */
    unsigned last = dircl, cl;
    for (;;) {
        uint32_t v = fat_get(d, last);
        if (v == 0xFFFFFFFFu) return -E_GENFAIL;
        if (fat_eof(d, v) || v < 2) break;
        last = v;
    }
    int n = fat_alloc(d, last);
    if (n < 0) return n;
    if (n == 0) return -E_ACCESS;           /* disk full */
    cl = n;
    uint32_t s = clus2sec(d, cl);
    for (unsigned k = 0; k <= d->cluster_mask; k++) {
        struct buf *b = getbuf(d, s + k, AREA_DIR, 1);
        if (!b) return -E_GENFAIL;
        memset(b->data, 0, 512);
        buf_dirty(b);
    }
    dir_cache_reset();
    struct dirent *de;
    struct buf *b;
    if (dir_entry(d, dircl, i, &de, &b, loc) != 1) return -E_GENFAIL;
    return i;
}

int dir_write(struct dpb *d, const struct dirloc *loc, const struct dirent *de)
{
    struct buf *b = getbuf(d, loc->sector, AREA_DIR, 0);
    if (!b) return -E_GENFAIL;
    memcpy(b->data + loc->index * 32, de, 32);
    buf_dirty(b);
    return 0;
}

/* walk the directories of a canonical path "C:\A\B" to B's cluster */
int dir_walk_to(struct dpb *d, const char *full, uint16_t *pcl)
{
    const char *p = full + 3;
    uint16_t cl = 0;
    while (*p) {
        char comp[16];
        int n = 0;
        while (*p && *p != '\\') { if (n < 15) comp[n++] = *p; p++; }
        comp[n] = 0;
        if (*p == '\\') p++;
        char n11[11];
        if (name_to_fcb(comp, n11) != 0) return -E_NOPATH;
        struct dirent de;
        int r = dir_find(d, cl, n11, ATTR_DIR | ATTR_HIDDEN | ATTR_SYSTEM, 0, &de, 0, 0);
        if (r == -E_NOFILE || (r >= 0 && !(de.attr & ATTR_DIR))) return -E_NOPATH;
        if (r < 0) return r;
        cl = de.cluster;
    }
    *pcl = cl;
    return 0;
}

/*
 * Resolve a path: canonical form, device names, and the directory that
 * holds the last component (which need not exist).
 */
int resolve(const char *path, struct pathinfo *pi, int allow_wild)
{
    memset(pi, 0, sizeof *pi);
    int e = canon_path(path, pi->full, &pi->drive, allow_wild);
    if (e == -E_BADDRIVE) return -E_NOPATH;     /* TransPath: a bad drive is a bad path */
    if (e < 0) return e;
    char *last = pi->full + 3;
    for (char *p = last; *p; p++) if (*p == '\\') last = p + 1;
    if (!*last) {
        pi->isroot = 1;
        memset(pi->name11, ' ', 11);
    } else {
        int w = name_to_fcb(last, pi->name11);
        if (w < 0) return -E_NOFILE;
        pi->wild = w;
        if (!w) {
            struct devhdr *dv = find_chardev(pi->name11);
            if (dv) { pi->dev = dv; return 0; }
        }
    }
    if (drive_remote(pi->drive)) { pi->remote = 1; return 0; }    /* the redirector's */
    int err = 0;
    pi->dpb = drive_dpb(pi->drive, &err);
    if (!pi->dpb) return -E_NOPATH;             /* TransPath: FATREAD failed */
    if (pi->isroot) { pi->dircl = 0; return 0; }
    /* the parent directory */
    char parent[80];
    int plen = last - pi->full;
    memcpy(parent, pi->full, plen);
    if (plen > 3) plen--;
    parent[plen] = 0;
    uint16_t cl;
    e = dir_walk_to(pi->dpb, parent, &cl);
    if (e < 0) return e;
    pi->dircl = cl;
    return 0;
}

int dir_lookup_path(struct pathinfo *pi, struct dirent *de, struct dirloc *loc, int sattr)
{
    int r = dir_find(pi->dpb, pi->dircl, pi->name11, sattr, 0, de, loc, 0);
    return r < 0 ? r : 0;
}

int get_volume_label(struct dpb *d, char *out11)
{
    struct dirent de;
    static const char any[11] = { '?','?','?','?','?','?','?','?','?','?','?' };
    int r = dir_find(d, 0, any, ATTR_VOLUME, 0, &de, 0, 0);
    if (r < 0) return r;
    memcpy(out11, de.name, 11);
    return 0;
}
