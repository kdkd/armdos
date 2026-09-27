/*
 * engine.c - the disk side of the ARM Disk Optimizer.
 *
 * Everything goes through absolute disk reads and writes (INT 25h/26h, the
 * DOS 4 packet form) on a FAT12 or FAT16 volume:
 *
 *  - the first FAT is read into memory, the directory tree is walked from the
 *    root, and every cluster gets an owner (a file or directory, bad, free);
 *    lost or cross-linked clusters stop us ("run CHKDSK /F first");
 *  - IO.SYS/ARMDOS.SYS, system and hidden files, bad clusters and files that
 *    are open in the kernel's SFTs never move;
 *  - a move copies a run of clusters to free ones, verifies the copy, and
 *    only then relinks: (1) the new clusters are chained in the FAT (a crash
 *    here leaves a lost chain, nothing else), (2) the single pointer that
 *    leads into the run - the predecessor's FAT entry or the directory entry -
 *    is switched, (3) the old clusters are freed.  For a directory the "."
 *    entry is patched in the copy and the ".." entries of its subdirectories
 *    are updated after the switch.  Every FAT change is written to all copies.
 *  - Full Optimization places directories first, then the files directory by
 *    directory, packed from the start of the disk, evicting whatever is in the
 *    way to free space at the end of the disk; Unfragment Files Only moves
 *    each fragmented file to the first free gap that holds it.
 */
#include "defrag.h"
#include <stdlib.h>

#define STOPPED ((const char *)1)

int dr;
unsigned spc, csize, maxc, nclus;
int fat16, floppy;
uint16_t *owner;
struct fil *fils;
unsigned nfils;
struct stats st;
int opt_sort, opt_desc, opt_hidden;
int stop_req;
uint32_t moved_clusters;

static struct kdpb dpb;
static unsigned rootsecs, fatsz, eofval, badval, bufcl, tophint, fcap;
static uint8_t *fat, *fatdirty, *iobuf, *vbuf;
static uint8_t sec[512] __attribute__((aligned(4)));
static uint32_t total_move, done_move;
static const char *meth;

static const char E_READ[]   = "Error reading drive";
static const char E_WRITE[]  = "Error writing drive";
static const char E_VERIFY[] = "Verify error: data written does not match";
static const char E_ERRORS[] = "Disk errors found; run CHKDSK /F first";
static const char E_MEM[]    = "Insufficient memory";
static const char E_SPACE[]  = "Not enough free space to continue";
static const char E_DIRCHG[] = "Directory changed during optimization";

/* ------------------------------------------------------------------ FAT */

static unsigned fget(unsigned c)
{
    if (fat16) return fat[c * 2] | (fat[c * 2 + 1] << 8);
    uint32_t o = c + c / 2;
    unsigned v = fat[o] | (fat[o + 1] << 8);
    return (c & 1) ? v >> 4 : v & 0xFFF;
}

static void fset(unsigned c, unsigned v)
{
    uint32_t o;
    if (fat16) { o = c * 2; fat[o] = v; fat[o + 1] = v >> 8; }
    else {
        o = c + c / 2;
        if (c & 1) { fat[o] = (fat[o] & 0x0F) | ((v & 0x0F) << 4); fat[o + 1] = v >> 4; }
        else { fat[o] = v; fat[o + 1] = (fat[o + 1] & 0xF0) | ((v >> 8) & 0x0F); }
    }
    fatdirty[o / 512] = 1;
    fatdirty[(o + 1) / 512] = 1;
}

static const char *fat_flush(void)
{
    for (unsigned s = 0; s < fatsz; ) {
        if (!fatdirty[s]) { s++; continue; }
        unsigned e = s;
        while (e < fatsz && fatdirty[e]) fatdirty[e++] = 0;
        for (unsigned f = 0; f < dpb.nfats; f++)
            if (abs_rw(dr, 1, dpb.first_fat + f * fatsz + s, e - s, fat + s * 512)) return E_WRITE;
        ui_pace(e - s, 0);              /* the FAT sits next to the heads */
        s = e;
    }
    return 0;
}

static uint32_t cl2sec(unsigned c) { return dpb.first_data + (uint32_t)(c - 2) * spc; }
static int valid(unsigned c) { return c >= 2 && c <= maxc; }

int fixed_cluster(unsigned c)
{
    unsigned o = owner[c];
    if (o == OW_FREE) return 0;
    if (o >= OW_RES) return 1;
    return (fils[o - 1].fl & F_FIXED) != 0;
}

/* ------------------------------------------------------------- open/close */

const char *eng_open(int drive)
{
    eng_close();
    dr = drive;
    disk_reset();                       /* write back what DOS has buffered */
    unsigned attr;
    if (ioctl_remote(drive, &attr)) return "Invalid drive specification";
    if (attr & 0x1200) return "Cannot optimize a network drive";
    if ((attr & 0x8000) || truename_letter(drive) != 'A' + drive)
        return "Cannot optimize a SUBSTed, JOINed or ASSIGNed drive";
    struct kdpb *d = get_dpb(drive);
    if (!d) return "Drive not ready";
    dpb = *d;
    if (dpb.sector_size != 512) return "Invalid media type";
    floppy = ioctl_removable(drive) == 1;
    spc = dpb.cluster_mask + 1;
    csize = spc * 512;
    maxc = dpb.max_cluster;
    nclus = maxc - 1;
    rootsecs = (dpb.root_ents * 32 + 511) / 512;
    fat16 = maxc >= 4086;
    eofval = fat16 ? 0xFFF8 : 0xFF8;
    badval = fat16 ? 0xFFF7 : 0xFF7;
    fatsz = dpb.fat_size;
    uint32_t bb = csize > 32768 ? csize : 32768;
    bufcl = bb / csize;
    fat = malloc((uint32_t)fatsz * 512 + 2);
    fatdirty = calloc(fatsz + 1, 1);
    owner = calloc(maxc + 2, 2);
    iobuf = malloc(bb);
    vbuf = malloc(bb);
    if (!fat || !fatdirty || !owner || !iobuf || !vbuf) { eng_close(); return E_MEM; }
    int ok = 0;
    for (unsigned f = 0; f < dpb.nfats && !ok; f++)
        ok = abs_rw(dr, 0, dpb.first_fat + f * fatsz, fatsz, fat) == 0;
    if (!ok) { eng_close(); return E_READ; }
    ui_pace(fatsz, 1);
    return 0;
}

void eng_close(void)
{
    free(fat); free(fatdirty); free(owner); free(iobuf); free(vbuf); free(fils);
    fat = fatdirty = iobuf = vbuf = 0; owner = 0; fils = 0;
    nfils = fcap = 0;
}

/* ------------------------------------------------------------ the tree */

static int add_fil(void)
{
    if (nfils >= 65000) return -1;
    if (nfils == fcap) {
        unsigned n = fcap ? fcap * 2 : 256;
        struct fil *p = realloc(fils, n * sizeof *p);
        if (!p) return -1;
        fils = p; fcap = n;
    }
    memset(&fils[nfils], 0, sizeof *fils);
    return nfils++;
}

static int walk_chain(unsigned fi)
{
    struct fil *f = &fils[fi];
    unsigned c = f->first, n = 0, fr = 1, prev = 0;
    if (!c) { f->nclus = 0; f->frags = 0; return 0; }
    for (;;) {
        if (!valid(c) || owner[c]) return -1;
        owner[c] = fi + 1;
        n++;
        if (prev && c != prev + 1) fr++;
        prev = c;
        unsigned nx = fget(c);
        if (nx >= eofval) break;
        if (!valid(nx) || nx == badval) return -1;
        c = nx;
    }
    f->nclus = n;
    f->frags = fr;
    return 0;
}

static const char *const bootfiles[] = { "IO      SYS", "ARMDOS  SYS", "MSDOS   SYS", "IBMBIO  COM", "IBMDOS  COM" };

/* sector n of a directory (ROOT or a fil), 0 past the end */
static uint32_t dir_sector(unsigned di, unsigned n)
{
    if (di == ROOT) return n < rootsecs ? dpb.dir_sector + n : 0;
    unsigned c = fils[di].first, k = n / spc;
    while (k--) {
        c = fget(c);
        if (c >= eofval || !valid(c)) return 0;
    }
    return cl2sec(c) + n % spc;
}

static const char *scan_dir(unsigned di)
{
    unsigned start = nfils;
    for (unsigned n = 0; ; n++) {
        uint32_t s = dir_sector(di, n);
        if (!s) break;
        if (abs_rw(dr, 0, s, 1, sec)) return E_READ;
        ui_pace(1, 1);
        for (unsigned i = 0; i < 16; i++) {
            const uint8_t *e = sec + i * 32;
            if (!e[0]) goto done;
            if (e[0] == 0xE5 || e[0] == '.') continue;
            if (e[11] & 0x08) continue;     /* volume label */
            int k = add_fil();
            if (k < 0) return E_MEM;
            struct fil *f = &fils[k];
            f->entoff = n * 512 + i * 32;
            f->parent = di;
            memcpy(f->name, e, 11);
            f->attr = e[11];
            f->time = e[22] | (e[23] << 8);
            f->date = e[24] | (e[25] << 8);
            f->first = e[26] | (e[27] << 8);
            f->size = e[28] | (e[29] << 8) | (e[30] << 16) | ((uint32_t)e[31] << 24);
            if (f->attr & 0x10) { f->fl |= F_DIR; st.dirs++; } else st.files++;
            if ((f->attr & 0x04) || ((f->attr & 0x02) && !opt_hidden)) f->fl |= F_FIXED;
            if (di == ROOT)
                for (unsigned b = 0; b < sizeof bootfiles / sizeof *bootfiles; b++)
                    if (!memcmp(f->name, bootfiles[b], 11)) f->fl |= F_FIXED;
            if (f->fl & F_FIXED) st.fixedfiles++;
            if ((f->fl & F_DIR) && !f->first) return E_ERRORS;
            if (walk_chain(k)) return E_ERRORS;
        }
    }
done:;
    unsigned end = nfils;
    for (unsigned k = start; k < end; k++)
        if (fils[k].fl & F_DIR) {
            const char *err = scan_dir(k);
            if (err) return err;
        }
    return 0;
}

/* files the kernel has open on this drive stay where they are (their SFTs
   hold the first cluster and the directory sector of their entry) */
static int root_locked;

static void mark_open(void)
{
    R r = { 0 };
    r.r0 = 0x5200;
    dos(&r);
    const uint8_t *lol = (const uint8_t *)r.r1;
    uint32_t blk;
    memcpy(&blk, lol + 4, 4);
    for (int guard = 0; blk && blk != 0xFFFFFFFF && guard < 64; guard++) {
        const uint8_t *b = (const uint8_t *)blk;
        unsigned cnt = b[4] | (b[5] << 8);
        for (unsigned i = 0; i < cnt; i++) {
            const uint8_t *s = b + 6 + i * 0x3B;
            unsigned refs = s[0] | (s[1] << 8), flags = s[5] | (s[6] << 8);
            if (!refs || (flags & 0x8080) || (flags & 0x3F) != (unsigned)dr) continue;
            unsigned first = s[0x0B] | (s[0x0C] << 8);
            uint32_t dsec = s[0x1B] | (s[0x1C] << 8) | (s[0x1D] << 16) | ((uint32_t)s[0x1E] << 24);
            if (valid(first) && owner[first] && owner[first] < OW_RES)
                fils[owner[first] - 1].fl |= F_FIXED | F_OPEN;
            if (dsec >= dpb.first_data) {
                unsigned c = (dsec - dpb.first_data) / spc + 2;
                if (valid(c) && owner[c] && owner[c] < OW_RES)
                    fils[owner[c] - 1].fl |= F_FIXED | F_NOSORT;
            }
            else if (dsec) root_locked = 1;         /* its entry is in the root */
        }
        memcpy(&blk, b, 4);
    }
}

static void compute_stats(void)
{
    uint32_t files = st.files, dirs = st.dirs, fixedfiles = st.fixedfiles;
    memset(&st, 0, sizeof st);
    st.files = files; st.dirs = dirs; st.fixedfiles = fixedfiles;
    unsigned last = 0;
    for (unsigned c = 2; c <= maxc; c++) {
        unsigned o = owner[c];
        if (o == OW_FREE) st.freec++;
        else if (o == OW_BAD) st.bad++;
        else {
            st.used++;
            if (fixed_cluster(c)) st.fixedclus++;
            else last = c;
        }
    }
    for (unsigned c = 2; c < last; c++)
        if (owner[c] == OW_FREE && (c == 2 || owner[c - 1] != OW_FREE)) st.gaps++;
    for (unsigned k = 0; k < nfils; k++) {
        struct fil *f = &fils[k];
        if (!f->first) continue;
        unsigned c = f->first, fr = 1, n = 0;
        for (;;) {
            n++;
            unsigned nx = fget(c);
            if (nx >= eofval || !valid(nx) || n > nclus) break;
            if (nx != c + 1) fr++;
            c = nx;
        }
        f->frags = fr;
        if (fr > 1 && !(f->fl & F_FIXED)) { st.fragfiles++; st.fragclus += f->nclus; st.frags += fr; }
    }
    st.notfrag_pct = st.used ? (st.used - st.fragclus) * 100 / st.used : 100;
    if (st.fragclus && st.notfrag_pct > 99) st.notfrag_pct = 99;
    if (!st.fragfiles) st.recommend = st.gaps ? M_FULL : M_NONE;
    else st.recommend = st.notfrag_pct >= 90 ? M_UNFRAG : M_FULL;
}

const char *eng_analyze(void)
{
    memset(&st, 0, sizeof st);
    memset(owner, 0, (maxc + 2) * 2);
    nfils = 0;
    root_locked = 0;
    const char *err = scan_dir(ROOT);
    if (err) return err;
    for (unsigned c = 2; c <= maxc; c++) {
        if (owner[c]) continue;
        unsigned v = fget(c);
        if (v == badval) owner[c] = OW_BAD;
        else if (v) return E_ERRORS;    /* lost clusters */
    }
    mark_open();
    compute_stats();
    return 0;
}

/* ------------------------------------------------------------ one move */

static const char *ent_sector(unsigned fi, uint32_t *s, unsigned *off)
{
    struct fil *f = &fils[fi];
    *s = dir_sector(f->parent, f->entoff / 512);
    *off = f->entoff % 512;
    if (!*s) return E_DIRCHG;
    return 0;
}

static const char *set_first(unsigned fi, unsigned d)
{
    uint32_t s;
    unsigned off;
    const char *err = ent_sector(fi, &s, &off);
    if (err) return err;
    if (abs_rw(dr, 0, s, 1, sec)) return E_READ;
    if (memcmp(sec + off, fils[fi].name, 11) || (sec[off + 26] | (sec[off + 27] << 8)) != fils[fi].first)
        return E_DIRCHG;
    sec[off + 26] = d; sec[off + 27] = d >> 8;
    if (abs_rw(dr, 1, s, 1, sec)) return E_WRITE;
    ui_pace(2, 2);
    fils[fi].first = d;
    return 0;
}

/* the ".." of every subdirectory of directory di now says d */
static const char *fix_dotdot(unsigned di, unsigned d)
{
    for (unsigned k = 0; k < nfils; k++) {
        struct fil *f = &fils[k];
        if (f->parent != di || !(f->fl & F_DIR) || !f->first) continue;
        uint32_t s = cl2sec(f->first);
        if (abs_rw(dr, 0, s, 1, sec)) return E_READ;
        if (memcmp(sec + 32, "..         ", 11)) continue;
        sec[32 + 26] = d; sec[32 + 27] = d >> 8;
        if (abs_rw(dr, 1, s, 1, sec)) return E_WRITE;
        ui_pace(2, 2);
    }
    return 0;
}

/* move clusters s..s+k-1 (consecutive, in chain order) of file fi to the free
   clusters d..d+k-1; pred = the cluster before s in the chain, 0 = s is first */
static const char *move_run(unsigned fi, unsigned pred, unsigned s, unsigned k, unsigned d)
{
    const char *err;
    unsigned secs = k * spc;
    struct fil *f = &fils[fi];
    ui_cells(s, k, CS_READ);
    ui_flush();
    if (abs_rw(dr, 0, cl2sec(s), secs, iobuf)) return E_READ;
    ui_pace(secs, 1);
    if ((f->fl & F_DIR) && !pred && !memcmp(iobuf, ".          ", 11)) {
        iobuf[26] = d; iobuf[27] = d >> 8;
    }
    ui_refresh(s, k);
    ui_cells(d, k, CS_WRITE);
    ui_flush();
    if (abs_rw(dr, 1, cl2sec(d), secs, iobuf)) return E_WRITE;
    if (abs_rw(dr, 0, cl2sec(d), secs, vbuf)) return E_READ;
    if (memcmp(iobuf, vbuf, secs * 512)) return E_VERIFY;
    ui_pace(secs * 2, 2);
    /* 1: chain the copy */
    unsigned nx = fget(s + k - 1);
    for (unsigned j = 0; j < k; j++) {
        fset(d + j, j + 1 < k ? d + j + 1 : nx);
        owner[d + j] = fi + 1;
    }
    if ((err = fat_flush())) return err;
    /* 2: switch the one pointer into it */
    if (!pred) {
        if ((err = set_first(fi, d))) return err;
        if ((f->fl & F_DIR) && (err = fix_dotdot(fi, d))) return err;
    } else {
        fset(pred, d);
        if ((err = fat_flush())) return err;
    }
    /* 3: free the original */
    for (unsigned j = 0; j < k; j++) { fset(s + j, 0); owner[s + j] = OW_FREE; }
    if ((err = fat_flush())) return err;
    if (s + k - 1 > tophint) tophint = s + k - 1;
    moved_clusters += k;
    ui_refresh(s, k);
    ui_refresh(d, k);
    return 0;
}

static unsigned pred_of(unsigned fi, unsigned c)
{
    unsigned p = fils[fi].first;
    if (p == c) return 0;
    for (unsigned guard = nclus; guard--; ) {
        unsigned nx = fget(p);
        if (nx == c) return p;
        if (nx >= eofval || !valid(nx)) break;
        p = nx;
    }
    return 0xFFFFFFFF;
}

/* the highest run (up to k) of free clusters outside [lo, hi) */
static unsigned alloc_top(unsigned k, unsigned lo, unsigned hi, unsigned *len)
{
    unsigned c = tophint > maxc || tophint < 2 ? maxc : tophint;
    for (int pass = 0; ; pass++) {
        while (c >= 2 && (owner[c] || (c >= lo && c < hi))) c--;
        if (c >= 2) break;
        if (pass) { *len = 0; return 0; }
        c = maxc;
    }
    tophint = c;
    unsigned b = c;
    while (b - 1 >= 2 && !owner[b - 1] && !(b - 1 >= lo && b - 1 < hi) && c - b + 1 < k) b--;
    *len = c - b + 1;
    return b;
}

static void progress(unsigned cluster)
{
    unsigned pct = total_move ? (uint32_t)((uint64_t)done_move * 100 / total_move) : 100;
    if (pct > 100) pct = 100;
    ui_status(cluster, pct, meth);
}

/* make file fi occupy P..P+L-1 */
static const char *place(unsigned fi, unsigned P)
{
    unsigned L = fils[fi].nclus, i = 0, pred = 0, cur = fils[fi].first;
    const char *err;
    while (i < L) {
        unsigned T = P + i;
        if (cur == T) { pred = cur; cur = fget(cur); i++; done_move++; continue; }
        if (ui_poll_stop()) return STOPPED;
        progress(T);
        if (owner[T] == OW_FREE) {
            unsigned k = 1;
            while (k < bufcl && i + k < L && owner[T + k] == OW_FREE && fget(cur + k - 1) == cur + k) k++;
            if ((err = move_run(fi, pred, cur, k, T))) return err;
            pred = T + k - 1;
            cur = fget(pred);
            i += k;
            done_move += k;
            continue;
        }
        unsigned o = owner[T];
        if (o >= OW_RES || (fils[o - 1].fl & F_FIXED)) return "Internal error: unmovable cluster in the way";
        unsigned g = o - 1, k = 1;
        while (k < bufcl && T + k < P + L && owner[T + k] == o && fget(T + k - 1) == T + k) k++;
        unsigned gp = pred_of(g, T);
        if (gp == 0xFFFFFFFF) return E_ERRORS;
        unsigned len, d = alloc_top(k, T, P + L, &len);
        if (!len) {
            /* a nearly full disk: park it further up the window (it moves again later) */
            for (d = P + L - 1; d > T && owner[d] != OW_FREE; d--) ;
            if (d == T) return E_SPACE;
            len = 1;
        }
        if ((err = move_run(g, gp, T, len, d))) return err;
        cur = i ? fget(pred) : fils[fi].first;
    }
    return 0;
}

/* ------------------------------------------------------------ ordering */

static uint16_t *order;
static unsigned norder;

static void children_by_offset(unsigned parent, int dirs)
{
    unsigned from = norder;
    for (unsigned k = 0; k < nfils; k++) {
        struct fil *f = &fils[k];
        if (f->parent != parent || !!(f->fl & F_DIR) != dirs) continue;
        unsigned j = norder++;
        while (j > from && fils[order[j - 1]].entoff > f->entoff) { order[j] = order[j - 1]; j--; }
        order[j] = k;
    }
}

/* directories breadth first, then the files of each directory in that order */
static int build_order(void)
{
    free(order);
    order = malloc((nfils + 1) * 2);
    uint16_t *dirs = malloc((nfils + 1) * 2);
    if (!order || !dirs) { free(dirs); return -1; }
    norder = 0;
    children_by_offset(ROOT, 1);
    for (unsigned q = 0; q < norder; q++) children_by_offset(order[q], 1);
    unsigned nd = norder;
    memcpy(dirs, order, nd * 2);
    children_by_offset(ROOT, 0);
    for (unsigned q = 0; q < nd; q++) children_by_offset(dirs[q], 0);
    free(dirs);
    /* drop what does not move */
    unsigned j = 0;
    for (unsigned q = 0; q < norder; q++) {
        struct fil *f = &fils[order[q]];
        if ((f->fl & F_FIXED) || !f->nclus) continue;
        order[j++] = order[q];
    }
    norder = j;
    return 0;
}

/* -------------------------------------------------------- directory sort */

static int cmp_ent(const uint8_t *a, const uint8_t *b)
{
    int da = (a[11] & 0x10) != 0, db = (b[11] & 0x10) != 0;
    if (da != db) return db - da;           /* directories first */
    int r = 0;
    switch (opt_sort) {
    case S_NAME: r = memcmp(a, b, 11); break;
    case S_EXT:  r = memcmp(a + 8, b + 8, 3); if (!r) r = memcmp(a, b, 8); break;
    case S_DATE: {
        uint32_t ta = ((uint32_t)(a[24] | (a[25] << 8)) << 16) | a[22] | (a[23] << 8);
        uint32_t tb = ((uint32_t)(b[24] | (b[25] << 8)) << 16) | b[22] | (b[23] << 8);
        r = ta < tb ? -1 : ta > tb;
        break;
    }
    case S_SIZE: {
        uint32_t sa = a[28] | (a[29] << 8) | (a[30] << 16) | ((uint32_t)a[31] << 24);
        uint32_t sb = b[28] | (b[29] << 8) | (b[30] << 16) | ((uint32_t)b[31] << 24);
        r = sa < sb ? -1 : sa > sb;
        break;
    }
    }
    return opt_desc ? -r : r;
}

static int kept_entry(unsigned di, const uint8_t *e)
{
    if (e[0] == '.' || (e[11] & 0x08)) return 1;
    if (e[11] & 0x06) return 1;             /* hidden/system entries keep their order, first */
    if (di == ROOT)
        for (unsigned b = 0; b < sizeof bootfiles / sizeof *bootfiles; b++)
            if (!memcmp(e, bootfiles[b], 11)) return 1;
    return 0;
}

static const char *sort_dir(unsigned di)
{
    if (di != ROOT && (fils[di].fl & F_NOSORT)) return 0;
    if (di == ROOT && root_locked) return 0;
    unsigned nsec = di == ROOT ? rootsecs : fils[di].nclus * spc;
    if (nsec * 512 > bufcl * csize) return 0;           /* too big to sort in one piece */
    for (unsigned n = 0; n < nsec; n++) {
        uint32_t s = dir_sector(di, n);
        if (!s || abs_rw(dr, 0, s, 1, iobuf + n * 512)) return E_READ;
    }
    unsigned ne = nsec * 16, nlive = 0, nkept = 0, i;
    uint16_t *idx = malloc(ne * 2 + 2), *newpos = malloc(ne * 2 + 2);
    if (!idx || !newpos) { free(idx); free(newpos); return E_MEM; }
    for (i = 0; i < ne; i++) newpos[i] = 0xFFFF;
    /* kept entries first (in their order), then the sorted rest */
    for (i = 0; i < ne && iobuf[i * 32]; i++) {
        const uint8_t *e = iobuf + i * 32;
        if (e[0] != 0xE5 && kept_entry(di, e)) idx[nlive++] = i;
    }
    nkept = nlive;
    for (i = 0; i < ne && iobuf[i * 32]; i++) {
        const uint8_t *e = iobuf + i * 32;
        if (e[0] == 0xE5 || kept_entry(di, e)) continue;
        unsigned j = nlive++;
        while (j > nkept && cmp_ent(iobuf + idx[j - 1] * 32, e) > 0) { idx[j] = idx[j - 1]; j--; }
        idx[j] = i;
    }
    memset(vbuf, 0, nsec * 512);
    for (i = 0; i < nlive; i++) { memcpy(vbuf + i * 32, iobuf + idx[i] * 32, 32); newpos[idx[i]] = i; }
    const char *err = 0;
    for (unsigned n = 0; n < nsec && !err; n++) {
        if (!memcmp(iobuf + n * 512, vbuf + n * 512, 512)) continue;
        uint32_t s = dir_sector(di, n);
        if (di != ROOT) { unsigned c = (s - dpb.first_data) / spc + 2; ui_cells(c, 1, CS_WRITE); ui_flush(); }
        if (abs_rw(dr, 1, s, 1, vbuf + n * 512)) err = E_WRITE;
        ui_pace(1, 1);
        if (di != ROOT) ui_refresh((s - dpb.first_data) / spc + 2, 1);
    }
    for (unsigned k = 0; k < nfils && !err; k++)
        if (fils[k].parent == di && newpos[fils[k].entoff / 32] != 0xFFFF)
            fils[k].entoff = newpos[fils[k].entoff / 32] * 32;
    free(idx); free(newpos);
    return err;
}

static const char *sort_all(void)
{
    const char *err = sort_dir(ROOT);
    for (unsigned k = 0; k < nfils && !err; k++)
        if (fils[k].fl & F_DIR) {
            if (ui_poll_stop()) return STOPPED;
            err = sort_dir(k);
        }
    return err;
}

/* ------------------------------------------------------------ the methods */

static const char *full(void)
{
    unsigned P = 2;
    const char *err;
    for (unsigned q = 0; q < norder; q++) {
        unsigned fi = order[q];
        if (fils[fi].fl & F_DONE) continue;
        unsigned L = fils[fi].nclus;
        for (;;) {
            while (P <= maxc && fixed_cluster(P)) P++;
            if (P + L - 1 > maxc) return E_SPACE;
            unsigned u = 0;
            for (unsigned c = P; c < P + L; c++) if (fixed_cluster(c)) { u = c; break; }
            if (!u) break;
            /* a gap before an unmovable cluster: fill it with later files that fit */
            for (unsigned r = q + 1; r < norder && P < u; r++) {
                unsigned g = order[r];
                if ((fils[g].fl & F_DONE) || fils[g].nclus > u - P) continue;
                if ((err = place(g, P))) return err;
                fils[g].fl |= F_DONE;
                P += fils[g].nclus;
            }
            P = u + 1;
        }
        if ((err = place(fi, P))) return err;
        fils[fi].fl |= F_DONE;
        P += L;
        ui_tick();
    }
    return 0;
}

static unsigned first_fit(unsigned L)
{
    unsigned run = 0;
    for (unsigned c = 2; c <= maxc; c++) {
        if (owner[c] == OW_FREE) { if (++run == L) return c - L + 1; }
        else run = 0;
    }
    return 0;
}

static const char *unfrag(void)
{
    const char *err;
    for (unsigned q = 0; q < norder; q++) {
        unsigned fi = order[q];
        struct fil *f = &fils[fi];
        uint32_t base = done_move;
        if (f->frags > 1) {
            unsigned P = first_fit(f->nclus);
            if (P) {
                if ((err = place(fi, P))) return err;
                fils[fi].frags = 1;
            }
        }
        done_move = base + fils[fi].nclus;
        progress(fils[fi].first);
    }
    return 0;
}

const char *eng_optimize(int method)
{
    const char *err = 0;
    stop_req = 0;
    moved_clusters = 0;
    tophint = maxc;
    meth = method == M_UNFRAG ? "Unfragment Files Only" : "Full Optimization";
    for (unsigned k = 0; k < nfils; k++) fils[k].fl &= ~F_DONE;
    ui_timer_start();
    ui_status(2, 0, meth);
    if (opt_sort != S_NONE) {
        ui_bar("Sorting directories...");
        err = sort_all();
    }
    if (!err && build_order()) err = E_MEM;
    if (!err) {
        total_move = done_move = 0;
        for (unsigned q = 0; q < norder; q++) total_move += fils[order[q]].nclus;
        ui_bar(method == M_UNFRAG ? "Unfragmenting files...   ESC=Stop Defrag" : "Optimizing...   ESC=Stop Defrag");
        err = method == M_UNFRAG ? unfrag() : full();
    }
    const char *e2 = fat_flush();
    if (!err) err = e2;
    free(order);
    order = 0;
    disk_reset();
    forget_free(dr, -1);
    compute_stats();
    if (!err) { done_move = total_move; progress(maxc); }
    ui_map_all(0);
    if (err == STOPPED) stop_req = 1;
    return err;
}
