/*
 * SYS - ARM-DOS 4.00 system transfer.
 *
 * A re-creation in C of MS-DOS 4.00 SYS.COM (CMD/SYS in the MIT-licensed
 * MS-DOS 4.0 source): copies IO.SYS and ARMDOS.SYS (4.00: IO.SYS and
 * MSDOS.SYS; COMMAND.COM is not copied) from the current drive or a given
 * [d:][path] to the target and writes the ARM boot record, keeping the
 * target's BPB, serial number and label.  Like 4.00 it makes room when it
 * has to: foreign root entries in slots 0/1 are moved further down the root
 * directory, and files occupying the clusters IO.SYS needs are moved out of
 * the way (the ARM-DOS boot sector loads ALL of IO.SYS, contiguous from the
 * first data cluster, where 4.00's needed only its first 3 sectors there).
 * All messages go to STDERR and the exit code is always 0, as in 4.00.
 *
 * SYS [d:][path] d:
 */
#include "dosutil.h"
#include "bootrec.h"
#include <stdlib.h>

static const char M_NOROOM[]  = "No room for system on destination disk\r\n";
static const char M_BADPATH[] = "Invalid path or System files not found\r\n";
static const char M_XFER[]    = "System transferred\r\n";
static const char M_NOSYS[]   = "No system on default drive\r\n";
static const char M_DEFAULT[] = "Cannot specify default drive\r\n";
static const char M_WRFAIL[]  = "Write failure, diskette unusable\r\n";
static const char M_INSERT[]  = "Insert system disk in drive %1\r\n";

struct dent {
    char name[11];
    uint8_t attr;
    uint8_t res[10];
    uint16_t time, date, cluster;
    uint32_t size;
} __attribute__((packed));

static int target = -1, srcdrive;
static char source[80];                 /* "C:" or "C:\PATH" */
static int have_source;

struct sysfile { const char *name; const char *dname; uint8_t *data; uint32_t size; uint16_t date, time; };
static struct sysfile sf[2] = { { "IO.SYS", "IO      SYS", 0, 0, 0, 0 }, { "ARMDOS.SYS", "ARMDOS  SYS", 0, 0, 0, 0 } };

/* the target */
static struct kdpb dpb;
static unsigned spc, csize, mclus, rootsecs;
static int fat16;
static uint8_t *fat;
static uint8_t sec[512] __attribute__((aligned(4)));
static uint8_t sec2[512] __attribute__((aligned(4)));

static void msg(const char *m) { errs(m); }
static void done(const char *m) { msg(m); dos_exit(0); }

/* ------------------------------------------------------------ parsing */

static void parse(void)
{
    static struct arg a[6];
    int n = parse_tail(a, 6), np = 0;
    char *pos[2] = { 0, 0 };
    for (int i = 0; i < n; i++) {
        if (a[i].sw) { parse_err(M_INVSW, a[i].shown); dos_exit(0); }
        if (np == 2) { parse_err(M_TOOMANY, a[i].shown); dos_exit(0); }
        pos[np++] = a[i].text;
    }
    if (!np) { parse_err(M_REQMISS, 0); dos_exit(0); }
    char *t = np == 2 ? pos[1] : pos[0];
    if (!is_drive_spec(t)) {
        if (np == 1) { parse_err(M_REQMISS, 0); dos_exit(0); }
        parse_err(M_BADFMT, t); dos_exit(0);
    }
    upcase(t);
    target = t[0] - 'A';
    if (np == 2) {
        strncpy(source, pos[0], 60);
        upcase(source);
        if (source[1] != ':') {
            /* a path without a drive is on the current drive */
            char s2[80] = { 'A' + cur_drive(), ':' };
            strcpy(s2 + 2, source);
            strcpy(source, s2);
        }
        size_t l = strlen(source);
        if (l > 2 && source[l - 1] == '\\') source[l - 1] = 0;
        srcdrive = source[0] - 'A';
        have_source = 1;
    } else {
        srcdrive = cur_drive();
        source[0] = 'A' + srcdrive; source[1] = ':'; source[2] = 0;
        if (target == srcdrive) done(M_DEFAULT);
    }
}

/* ---------------------------------------------------- the source files */

static int load_file(struct sysfile *f)
{
    char path[100];
    strcpy(path, source);
    strcat(path, "\\");
    strcat(path, f->name);
    R r = { 0 };
    r.r0 = 0x3D00; r.r3 = (uint32_t)path;
    if (dos(&r)) return -1;
    int h = r.r0 & 0xFFFF, err = 0;
    memset(&r, 0, sizeof r); r.r0 = 0x4202; r.r1 = h; dos(&r);
    f->size = ((r.r3 & 0xFFFF) << 16) | (r.r0 & 0xFFFF);
    memset(&r, 0, sizeof r); r.r0 = 0x4200; r.r1 = h; dos(&r);
    memset(&r, 0, sizeof r); r.r0 = 0x5700; r.r1 = h; dos(&r);
    f->time = r.r2; f->date = r.r3;
    free(f->data);
    f->data = malloc(f->size + 1);
    if (!f->data) err = -1;
    for (uint32_t d = 0; !err && d < f->size; ) {
        uint32_t n = f->size - d > 0x8000 ? 0x8000 : f->size - d;
        memset(&r, 0, sizeof r);
        r.r0 = 0x3F00; r.r1 = h; r.r2 = n; r.r3 = (uint32_t)(f->data + d);
        if (dos(&r) || (r.r0 & 0xFFFF) != n) err = -1;
        d += n;
    }
    memset(&r, 0, sizeof r); r.r0 = 0x3E00; r.r1 = h; dos(&r);
    return err;
}

static void get_system_files(void)
{
    for (;;) {
        if (!load_file(&sf[0]) && !load_file(&sf[1])) return;
        if (ioctl_removable(srcdrive) == 1) {
            char s[4] = { 'A' + srcdrive, ':', 0 };
            outfmt(STDERR, M_INSERT, s, 0, 0);
            errs(M_PRESSKEY);
            getkey();
            continue;
        }
        done(have_source ? M_BADPATH : M_NOSYS);
    }
}

/* ----------------------------------------------------------- the FAT */

static unsigned fget(unsigned c)
{
    if (fat16) return fat[c * 2] | (fat[c * 2 + 1] << 8);
    uint32_t o = c + c / 2;
    unsigned v = fat[o] | (fat[o + 1] << 8);
    return (c & 1) ? v >> 4 : v & 0xFFF;
}

static void fset(unsigned c, unsigned v)
{
    if (fat16) { fat[c * 2] = v; fat[c * 2 + 1] = v >> 8; return; }
    uint32_t o = c + c / 2;
    if (c & 1) { fat[o] = (fat[o] & 0x0F) | ((v & 0x0F) << 4); fat[o + 1] = v >> 4; }
    else { fat[o] = v; fat[o + 1] = (fat[o + 1] & 0xF0) | ((v >> 8) & 0x0F); }
}

static int is_eof(unsigned v) { return v >= (fat16 ? 0xFFF8u : 0xFF8u); }
static int is_bad(unsigned v) { return v == (fat16 ? 0xFFF7u : 0xFF7u); }
static uint32_t clus_sec(unsigned c) { return dpb.first_data + (uint32_t)(c - 2) * spc; }

static void free_chain(unsigned c)
{
    unsigned guard = mclus;
    while (c >= 2 && c <= mclus && guard--) {
        unsigned nx = fget(c);
        fset(c, 0);
        if (is_eof(nx)) break;
        c = nx;
    }
}

static unsigned count_free(void)
{
    unsigned n = 0;
    for (unsigned c = 2; c <= mclus; c++) if (!fget(c)) n++;
    return n;
}

static int write_fat(void)
{
    for (unsigned f = 0; f < dpb.nfats; f++)
        if (abs_rw(target, 1, dpb.first_fat + f * dpb.fat_size, dpb.fat_size, fat)) return -1;
    return 0;
}

/* --------------------------------------------- the directory search */

/* visit every directory entry (root, then subdirectories); fn returns 1 to
   say it changed the entry (the sector is written back) */
typedef int (*visit_fn)(struct dent *e, unsigned dircluster);

static int walk_dir(unsigned cl, visit_fn fn, int depth)
{
    uint8_t *b = malloc(512);
    if (!b || depth > 32) { free(b); return -1; }
    unsigned c = cl, left = cl ? 0 : rootsecs, guard = mclus;
    uint32_t s = cl ? clus_sec(cl) : dpb.dir_sector;
    unsigned in = 0;
    for (;;) {
        if (!cl) { if (!left--) break; }
        else if (in == spc) {
            unsigned nx = fget(c);
            if (is_eof(nx) || nx < 2 || nx > mclus || !guard--) break;
            c = nx; s = clus_sec(c); in = 0;
        }
        if (abs_rw(target, 0, s, 1, b)) { free(b); return -1; }
        int changed = 0, end = 0;
        for (unsigned i = 0; i < 16; i++) {
            struct dent *e = (struct dent *)(b + i * 32);
            if (!e->name[0]) { end = 1; break; }
            if ((uint8_t)e->name[0] == 0xE5 || e->name[0] == '.' || (e->attr & 0x08)) continue;
            if (fn(e, cl)) changed = 1;
        }
        if (changed && abs_rw(target, 1, s, 1, b)) { free(b); return -1; }
        /* then the subdirectories */
        for (unsigned i = 0; i < 16 && !end; i++) {
            struct dent *e = (struct dent *)(b + i * 32);
            if (!e->name[0]) break;
            if ((uint8_t)e->name[0] == 0xE5 || e->name[0] == '.' || !(e->attr & 0x10)) continue;
            if (e->cluster >= 2 && e->cluster <= mclus) walk_dir(e->cluster, fn, depth + 1);
        }
        if (end) break;
        s++; in++;
    }
    free(b);
    return 0;
}

static unsigned from_cl, to_cl;

/* a directory's "." and its subdirectories' ".." name its first cluster */
static void fix_dots(unsigned dircl, unsigned newcl)
{
    if (abs_rw(target, 0, clus_sec(newcl), 1, sec2)) return;
    struct dent *d = (struct dent *)sec2;
    if (!memcmp(d->name, ".          ", 11) && d->cluster == dircl) {
        d->cluster = newcl;
        abs_rw(target, 1, clus_sec(newcl), 1, sec2);
    }
}

static int retarget(struct dent *e, unsigned dircl)
{
    (void)dircl;
    int ch = 0;
    if (e->cluster == from_cl) {
        e->cluster = to_cl;
        if (e->attr & 0x10) fix_dots(from_cl, to_cl);
        ch = 1;
    }
    return ch;
}

static int fix_dotdot_in(struct dent *e, unsigned dircl)
{
    (void)dircl;
    /* e is a subdirectory of the moved directory: its ".." */
    if (!(e->attr & 0x10) || e->cluster < 2) return 0;
    if (abs_rw(target, 0, clus_sec(e->cluster), 1, sec2)) return 0;
    struct dent *d = (struct dent *)(sec2 + 32);
    if (!memcmp(d->name, "..         ", 11) && d->cluster == from_cl) {
        d->cluster = to_cl;
        abs_rw(target, 1, clus_sec(e->cluster), 1, sec2);
    }
    return 0;
}

/* move cluster c (in use by some file) to a free cluster at or above `low` */
static int move_cluster(unsigned c, unsigned low)
{
    unsigned n = 0;
    for (unsigned k = low; k <= mclus; k++) if (!fget(k)) { n = k; break; }
    if (!n) return -1;
    uint8_t *buf = malloc(csize);
    if (!buf) return -1;
    int e = abs_rw(target, 0, clus_sec(c), spc, buf) || abs_rw(target, 1, clus_sec(n), spc, buf);
    free(buf);
    if (e) return -1;
    fset(n, fget(c));
    fset(c, 0);
    for (unsigned p = 2; p <= mclus; p++) if (fget(p) == c) { fset(p, n); return 0; }
    /* no predecessor: a directory entry starts there */
    if (write_fat()) return -1;
    from_cl = c; to_cl = n;
    walk_dir(0, retarget, 0);
    /* if it was a directory, its subdirectories' ".." point at it */
    walk_dir(n, fix_dotdot_in, 30);
    return 0;
}

/* ------------------------------------------------ the root entries 0/1 */

/* move root entry idx (in sector 0 = sec) to the first free slot from #2 */
static int move_entry(unsigned idx)
{
    struct dent save;
    memcpy(&save, sec + idx * 32, 32);
    for (unsigned s = 0; s < rootsecs; s++) {
        uint8_t *b = s ? sec2 : sec;
        if (s && abs_rw(target, 0, dpb.dir_sector + s, 1, sec2)) return -1;
        for (unsigned i = s ? 0 : 2; i < 16; i++) {
            uint8_t *e = b + i * 32;
            if (e[0] != 0 && e[0] != 0xE5) continue;
            int was_end = e[0] == 0;
            memcpy(e, &save, 32);
            if (was_end && i < 15) { /* keep the end mark after it */ }
            if (s && abs_rw(target, 1, dpb.dir_sector + s, 1, sec2)) return -1;
            memset(sec + idx * 32, 0, 32);
            sec[idx * 32] = 0xE5;
            if (idx == 0 && sec[32] == 0) sec[32] = 0xE5;
            return abs_rw(target, 1, dpb.dir_sector, 1, sec);
        }
    }
    return -1;
}

/* ------------------------------------------------------------ writing */

static int create_file(struct sysfile *f)
{
    char path[16] = { 'A' + target, ':', '\\' };
    strcpy(path + 3, f->name);
    R r = { 0 };
    r.r0 = 0x3C00; r.r2 = 0; r.r3 = (uint32_t)path;
    if (dos(&r)) return -1;
    int h = r.r0 & 0xFFFF, err = 0;
    for (uint32_t d = 0; !err && d < f->size; ) {
        uint32_t n = f->size - d > 0x8000 ? 0x8000 : f->size - d;
        memset(&r, 0, sizeof r);
        r.r0 = 0x4000; r.r1 = h; r.r2 = n; r.r3 = (uint32_t)(f->data + d);
        if (dos(&r) || (r.r0 & 0xFFFF) != n) err = -1;
        d += n;
    }
    memset(&r, 0, sizeof r); r.r0 = 0x5701; r.r1 = h; r.r2 = f->time; r.r3 = f->date; dos(&r);
    memset(&r, 0, sizeof r); r.r0 = 0x3E00; r.r1 = h;
    if (dos(&r)) err = -1;
    memset(&r, 0, sizeof r); r.r0 = 0x4301; r.r2 = 7; r.r3 = (uint32_t)path; dos(&r);
    return err;
}

static void write_boot_record(void)
{
    if (abs_rw(target, 0, 0, 1, sec2)) return;
    struct bpb b;
    memcpy(&b, sec2 + 0x0B, sizeof b);
    if (b.bps != 512 || !b.spc || !b.nfats) {
        struct devparams dp;
        memset(&dp, 0, sizeof dp);
        dp.special = 1;
        if (gen_ioctl(target, 0x60, &dp)) return;
        b = dp.bpb;
    }
    uint32_t serial;
    char label[11];
    const char *fs = 0;
    if (sec2[0x26] == 0x29 || sec2[0x26] == 0x28) {
        serial = sec2[0x27] | (sec2[0x28] << 8) | (sec2[0x29] << 16) | ((uint32_t)sec2[0x2A] << 24);
        memcpy(label, sec2 + 0x2B, 11);
        fs = (const char *)sec2 + 0x36;
    } else {
        /* no extended boot record yet: 4.00 SYS's Create_Serial_ID */
        unsigned y, mo, d, h, mi, s, hs;
        get_date(&y, &mo, &d);
        get_time(&h, &mi, &s, &hs);
        serial = ((uint32_t)((((h << 8) | mi) * 2) & 0xFFFF) << 16) | ((((mo << 8) | d) * 2) & 0xFFFF);
        memcpy(label, "NO NAME    ", 11);
        for (unsigned s2 = 0; s2 < rootsecs; s2++) {
            if (abs_rw(target, 0, dpb.dir_sector + s2, 1, sec)) break;
            int stop = 0;
            for (unsigned i = 0; i < 16; i++) {
                struct dent *e = (struct dent *)(sec + i * 32);
                if (!e->name[0]) { stop = 1; break; }
                if ((uint8_t)e->name[0] != 0xE5 && (e->attr & 0x1F) == 0x08) { memcpy(label, e->name, 11); stop = 1; break; }
            }
            if (stop) break;
        }
    }
    uint8_t *out = sec;
    make_bootrec(out, &b, b.media == 0xF8, serial, label, fat16);
    if (fs) memcpy(out + 0x36, fs, 8);
    abs_rw(target, 1, 0, 1, out);
}

/* --------------------------------------------------------------- main */

int main(void)
{
    check_version();
    parse();
    unsigned attr;
    if (ioctl_remote(target, &attr)) done("Invalid drive specification\r\n");
    if (attr & 0x1200) done("Cannot SYS to a network drive\r\n");
    if ((attr & 0x8000) || truename_letter(target) != 'A' + target) done("Cannot SYS a SUBSTed or ASSIGNed drive\r\n");

    get_system_files();

    /* the target's file system */
    struct { uint16_t level; uint32_t serial; char label[11]; char fs[8]; } __attribute__((packed)) mid;
    R r = { 0 };
    r.r0 = 0x6900; r.r1 = target + 1; r.r3 = (uint32_t)&mid;
    if (!dos(&r)) {
        if (memcmp(mid.fs, "FAT12   ", 8) && memcmp(mid.fs, "FAT16   ", 8)) {
            char t[9];
            memcpy(t, mid.fs, 8); t[8] = 0;
            for (int i = 7; i >= 0 && t[i] == ' '; i--) t[i] = 0;
            outfmt(STDERR, "Not able to SYS to %1 file system\r\n", t, 0, 0);
            dos_exit(0);
        }
    } else if ((r.r0 & 0xFFFF) == 21) {
        errs("Not ready\r\n");
        dos_exit(0);
    }
    struct kdpb *d = get_dpb(target);
    if (!d) done(M_WRFAIL);
    dpb = *d;
    disk_reset();
    spc = dpb.cluster_mask + 1;
    csize = spc * dpb.sector_size;
    mclus = dpb.max_cluster;
    rootsecs = (dpb.root_ents * 32 + 511) / 512;
    fat16 = mclus >= 4086;
    fat = malloc((uint32_t)dpb.fat_size * 512);
    if (!fat || abs_rw(target, 0, dpb.first_fat, dpb.fat_size, fat)) done(M_WRFAIL);
    if (abs_rw(target, 0, dpb.dir_sector, 1, sec)) done(M_WRFAIL);

    /* Verify_File_Location: slots 0 and 1 */
    unsigned old = 0;
    for (unsigned i = 0; i < 2; i++) {
        struct dent *e = (struct dent *)(sec + i * 32);
        if (e->name[0] == 0 || (uint8_t)e->name[0] == 0xE5) continue;
        if (!memcmp(e->name, sf[i].dname, 11) && !(e->attr & 0x18)) {
            old += (e->size + csize - 1) / csize;
            continue;
        }
        if (move_entry(i)) done(M_NOROOM);
    }
    /* Determine_Free_Space */
    unsigned need_io = (sf[0].size + csize - 1) / csize, need_dos = (sf[1].size + csize - 1) / csize;
    if (count_free() + old < need_io + need_dos) done(M_NOROOM);

    /* the old system files go: their entries stay deleted for the new ones */
    for (unsigned i = 0; i < 2; i++) {
        struct dent *e = (struct dent *)(sec + i * 32);
        if (e->name[0] == 0 || (uint8_t)e->name[0] == 0xE5) continue;
        free_chain(e->cluster);
        e->name[0] = 0xE5;
        e->cluster = 0;
        e->size = 0;
    }
    if (((struct dent *)sec)->name[0] == 0) sec[0] = 0xE5;  /* never end the directory at slot 0 */
    if (write_fat() || abs_rw(target, 1, dpb.dir_sector, 1, sec)) done(M_WRFAIL);
    /* Free_Cluster: IO.SYS gets clusters 2 .. 2+need_io-1 */
    for (unsigned c = 2; c < 2 + need_io && c <= mclus; c++) {
        unsigned v = fget(c);
        if (!v) continue;
        if (is_bad(v)) done(M_NOROOM);
        if (move_cluster(c, 2 + need_io)) done(M_NOROOM);
    }
    if (write_fat()) done(M_WRFAIL);
    disk_reset();
    forget_free(target, 2);

    /* Do_SYS */
    if (create_file(&sf[0]) || create_file(&sf[1])) done(M_NOROOM);
    disk_reset();
    /* check: slots 0/1, IO.SYS contiguous from cluster 2 */
    if (abs_rw(target, 0, dpb.dir_sector, 1, sec) || abs_rw(target, 0, dpb.first_fat, dpb.fat_size, fat)) done(M_WRFAIL);
    struct dent *e0 = (struct dent *)sec, *e1 = (struct dent *)(sec + 32);
    int ok = !memcmp(e0->name, sf[0].dname, 11) && !memcmp(e1->name, sf[1].dname, 11) && (e0->cluster == 2 || !sf[0].size);
    for (unsigned c = 2; ok && c + 1 < 2 + need_io; c++) if (fget(c) != c + 1) ok = 0;
    if (!ok) done(M_NOROOM);

    write_boot_record();
    disk_reset();
    done(M_XFER);
    return 0;
}
