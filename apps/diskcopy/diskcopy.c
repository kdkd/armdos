/*
 * DISKCOPY - ARM-DOS 4.00 diskette copier.
 *
 * A re-creation in C of MS-DOS 4.00 DISKCOPY.COM (CMD/DISKCOPY in the
 * MIT-licensed MS-DOS 4.0 source): the same prompts and messages, track
 * reads and writes through IOCTL 440Dh (61h/41h, 42h to format a blank
 * target while copying), as many cylinders per pass as conventional memory
 * holds - so a 1.44 MB diskette takes several SOURCE/TARGET swaps on a
 * single-drive machine, as it did in 1989 - and a new volume serial number
 * for the copy.
 *
 * DISKCOPY [d: [d:]] [/1]
 */
#include "dosutil.h"

static const char M_CRLF[]     = "\r\n";
static const char M_USAGE[]    = "Do not specify filename(s)\r\nCommand Format: DISKCOPY d: d: [/1]\r\n";
static const char M_INVDRV[]   = "\r\nInvalid drive specification\r\nSpecified drive does not exist\r\nor is non-removable\r\n";
static const char M_NETWORK[]  = "\r\nCannot DISKCOPY to or from\r\na network drive\r\n";
static const char M_FORMAT[]   = "\r\nFormatting while copying\r\n";
static const char M_SOURCE[]   = "\r\nInsert SOURCE diskette in drive %1:\r\n";
static const char M_TARGET[]   = "\r\nInsert TARGET diskette in drive %1:\r\n";
static const char M_DOOR[]     = "Make sure a diskette is inserted into\r\nthe drive and the door is closed\r\n";
static const char M_UNUSABLE[] = "\r\nTarget diskette may be unusable\r\n";
static const char M_ANOTHER[]  = "\r\nCopy another diskette (Y/N)? ";
static const char M_COPYING[]  = "\r\nCopying %1 tracks\r\n%2 Sectors/Track, %3 Side(s)\r\n";
static const char M_NOTCOMP[]  = "\r\nDrive types or diskette types\r\nnot compatible\r\n";
static const char M_RDERR[]    = "\r\nUnrecoverable read error on drive %1\r\nSide %2, track %3\r\n";
static const char M_WRERR[]    = "\r\nUnrecoverable write error on drive %1\r\nSide %2, track %3\r\n";
static const char M_ENDED[]    = "\r\nCopy process ended\r\n";
static const char M_SRCBAD[]   = "\r\nSOURCE diskette bad or incompatible";
static const char M_TGTBAD[]   = "\r\nTARGET diskette bad or incompatible";

static int src, tgt, single, one_side;
static char sletter[3], tletter[3];
static struct geom g;                   /* the drive's physical layout (track I/O) */
static unsigned ncyl, spt, sides;       /* what is copied, from the source's BPB */
static uint8_t *buf;                    /* conventional memory: as many cylinders as fit */
static uint32_t bufsize;
static unsigned cyl_bytes;
static int errors, fatal, formatting, new_serial;
static uint32_t serial;
static uint8_t boot[512] __attribute__((aligned(4)));

static void out1(const char *m, const char *a) { outfmt(STDOUT, m, a, 0, 0); }

static void press_any_key(void)
{
    outs(M_CRLF);
    outs(M_PRESSKEY);
    getkey_flush();
}

static void usage_exit(const char *shown, const char *msg)
{
    parse_err(msg, shown);
    outs(M_USAGE);
    dos_exit(1);
}

static void parse(void)
{
    static struct arg a[6];
    int n = parse_tail(a, 6), np = 0;
    int d[2];
    for (int i = 0; i < n; i++) {
        struct arg *x = &a[i];
        if (x->sw) {
            if (!strcmp(x->text, "/1") && !x->val) { one_side = 1; continue; }
            usage_exit(x->shown, M_INVSW);
        }
        if (!is_drive_spec(x->text)) usage_exit(x->shown, M_INVPARM);
        if (np == 2) usage_exit(x->shown, M_TOOMANY);
        d[np++] = (x->text[0] & 0xDF) - 'A';
    }
    int cur = cur_drive();
    src = np > 0 ? d[0] : cur;
    tgt = np > 1 ? d[1] : cur;
}

static int removable_floppy(int drive)
{
    unsigned attr;
    if (ioctl_remote(drive, &attr)) return -1;
    if (attr & 0x1000) return -2;
    if (ioctl_removable(drive) != 1) return -1;
    return 0;
}

static void check_drives(void)
{
    for (int k = 0; k < 2; k++) {
        int r = removable_floppy(k ? tgt : src);
        if (r == -2) { outs(M_NETWORK); dos_exit(1); }
        if (r) { outs(M_INVDRV); dos_exit(1); }
    }
    /* one physical drive answering to two letters (A: and B:) = one drive */
    single = src == tgt || (get_logical(src) != 0 && get_logical(tgt) != 0);
    if (single) tgt = src;
    set_logical(src);
    sletter[0] = 'A' + src; sletter[1] = 0;
    tletter[0] = 'A' + tgt; tletter[1] = 0;
    if (drive_geometry(src, &g)) { outs(M_INVDRV); dos_exit(1); }
    cyl_bytes = g.heads * g.spt * 512;
    /* all the conventional memory there is */
    R r = { 0 };
    r.r0 = 0x4800; r.r1 = 0xFFFF;
    dos(&r);
    unsigned paras = r.r1 & 0xFFFF;
    if (paras > 64) paras -= 64;        /* leave DOS a little */
    memset(&r, 0, sizeof r);
    r.r0 = 0x4800; r.r1 = paras;
    if (dos(&r) || (uint32_t)paras * 16 < cyl_bytes) { errs("Insufficient memory\r\n"); dos_exit(1); }
    buf = (uint8_t *)((r.r0 & 0xFFFF) << 4);
    bufsize = (uint32_t)paras * 16;
}

/* a DOS 4 serial number, DISKCOPY's MAYBE_ADJUST_SERIAL */
static uint32_t copy_serial(void)
{
    unsigned y, mo, d, h, mi, s, hs;
    get_date(&y, &mo, &d);
    get_time(&h, &mi, &s, &hs);
    unsigned lo = (((mo << 8) | d) + ((s << 8) | hs)) & 0xFFFF;
    unsigned hi = (y + ((h << 8) | mi)) & 0xFFFF;
    return ((uint32_t)hi << 16) | lo;
}

/* "A:" style argument for the extended error messages */
static void ext_error(int e, int drive)
{
    char dl[4] = { 'A' + drive, ':', 0 };
    if (e == 21) { errs("Not ready - "); errs(dl); errs("\r\n"); outs(M_DOOR); }
    else { errs("Write protect error - "); errs(dl); errs("\r\n"); }
    press_any_key();
}

/* one track, retried after "not ready"/"write protect" like 4.00 */
static int track_io(int drive, int write, unsigned cyl, unsigned head, uint8_t *p)
{
    for (;;) {
        int e = trk_rw(drive, write, &g, ((uint32_t)cyl * g.heads + head) * g.spt, g.spt, p);
        if (e == 21 || e == 19) { ext_error(e, drive); continue; }
        return e;
    }
}

static int format_rest(unsigned from)
{
    for (unsigned c = from; c < ncyl; c++)
        for (unsigned h = 0; h < sides; h++) {
            int e;
            while ((e = trk_format(tgt, c, h, 0)) == 21 || e == 19) ext_error(e, tgt);
            if (e) {
                outs(c == 0 && h == 1 ? M_NOTCOMP : M_TGTBAD);
                return -1;
            }
        }
    return 0;
}

static int check_source(void)
{
    if (track_io(src, 0, 0, 0, buf)) { outs(M_SRCBAD); return -1; }
    const struct bpb *b = (const struct bpb *)(buf + 0x0B);
    uint32_t tot = b->total16 ? b->total16 : b->total32;
    if (b->bps != 512 || !b->spt || !b->heads || b->heads > 2 || b->spt > g.spt || tot > g.total) {
        outs(M_SRCBAD);
        return -1;
    }
    spt = b->spt;
    sides = one_side ? 1 : b->heads;
    ncyl = tot / b->spt / b->heads;
    if (ncyl > g.cyls) { outs(M_NOTCOMP); return -1; }
    char a[8], c2[8], d[8];
    fmtnum(a, ncyl, 1, ' '); fmtnum(c2, spt, 1, ' '); fmtnum(d, sides, 1, ' ');
    outfmt(STDOUT, M_COPYING, a, c2, d);
    return 0;
}

/* does the target already have the source's layout? */
static int target_matches(void)
{
    if (trk_rw(tgt, 0, &g, 0, 1, boot)) return 0;
    const struct bpb *b = (const struct bpb *)(boot + 0x0B);
    uint32_t tot = b->total16 ? b->total16 : b->total32;
    return b->bps == 512 && b->spt == spt && b->heads >= sides && b->spt && b->heads &&
           tot / b->spt / b->heads == ncyl;
}

static void copy_disk(void)
{
    errors = fatal = formatting = new_serial = 0;
    unsigned per_pass = bufsize / cyl_bytes;
    if (!single) { out1(M_SOURCE, sletter); out1(M_TARGET, tletter); press_any_key(); }
    unsigned cur = 0;
    do {
        if (single) { out1(M_SOURCE, sletter); press_any_key(); }
        if (cur == 0 && check_source()) { fatal = 1; break; }
        unsigned n = ncyl - cur < per_pass ? ncyl - cur : per_pass;
        for (unsigned c = 0; c < n; c++)
            for (unsigned h = 0; h < sides; h++) {
                uint8_t *p = buf + (uint32_t)c * cyl_bytes + h * g.spt * 512;
                if (track_io(src, 0, cur + c, h, p)) {
                    char s1[6], s2[6];
                    fmtnum(s1, h, 1, ' '); fmtnum(s2, cur + c, 1, ' ');
                    char dl[4] = { 'A' + src, ':', 0 };
                    outfmt(STDOUT, M_RDERR, dl, s1, s2);
                    errors++;
                } else if (cur + c == 0 && h == 0) {
                    /* the copy gets a serial number of its own */
                    if ((p[0x15] & 0xF0) == 0xF0 && (p[0x26] == 0x28 || p[0x26] == 0x29)) {
                        serial = copy_serial();
                        p[0x27] = serial; p[0x28] = serial >> 8; p[0x29] = serial >> 16; p[0x2A] = serial >> 24;
                        new_serial = 1;
                    }
                }
            }
        if (single) { out1(M_TARGET, tletter); press_any_key(); }
        if (cur == 0 && !target_matches()) {
            outs(M_FORMAT);
            formatting = 1;
            if (format_rest(0)) { fatal = 1; break; }
        }
        for (unsigned c = 0; c < n; c++)
            for (unsigned h = 0; h < sides; h++) {
                uint8_t *p = buf + (uint32_t)c * cyl_bytes + h * g.spt * 512;
                int e = track_io(tgt, 1, cur + c, h, p);
                if (e && !formatting) {
                    outs(M_FORMAT);
                    formatting = 1;
                    if (format_rest(cur + c)) { fatal = 1; goto end; }
                    e = track_io(tgt, 1, cur + c, h, p);
                }
                if (e) {
                    char s1[6], s2[6];
                    fmtnum(s1, h, 1, ' '); fmtnum(s2, cur + c, 1, ' ');
                    char dl[4] = { 'A' + tgt, ':', 0 };
                    outfmt(STDOUT, M_WRERR, dl, s1, s2);
                    errors++;
                }
            }
        cur += n;
    } while (cur < ncyl);
end:
    /* DOS must re-read the diskette now in the drive */
    {
        struct devparams dp;
        memset(&dp, 0, sizeof dp);
        if (!gen_ioctl(tgt, 0x60, &dp)) { dp.special = 4; dp.tracks = 0; gen_ioctl(tgt, 0x40, &dp); }
        disk_reset();
    }
    if (errors) outs(M_UNUSABLE);
    else if (fatal) outs(M_ENDED);
    else if (new_serial) { outs(M_CRLF); serial_line(serial); }
}

int main(void)
{
    check_version();
    parse();
    if (!drive_valid(src) || !drive_valid(tgt)) { outs(M_INVDRV); dos_exit(1); }
    check_drives();
    for (;;) {
        copy_disk();
        for (;;) {
            outs(M_ANOTHER);
            int c = getche_flush();
            outs(M_CRLF);
            int yn = yesno(c);
            if (yn == 1) break;
            if (yn == 0) { set_logical(src); dos_exit(0); }
            outs(M_CRLF);
        }
    }
}
