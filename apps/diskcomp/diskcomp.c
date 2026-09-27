/*
 * DISKCOMP - ARM-DOS 4.00 diskette compare.
 *
 * A re-creation in C of MS-DOS 4.00 DISKCOMP.COM (CMD/DISKCOMP in the
 * MIT-licensed MS-DOS 4.0 source): track-by-track compare of two diskettes
 * through IOCTL 440Dh/61h, as many cylinders per pass as conventional memory
 * holds; on one drive the FIRST and SECOND diskettes alternate, each pass
 * starting with the one already in the drive.  Volume serial numbers are
 * ignored (a DISKCOPY gets a new one); "Compare OK" only when nothing differs.
 *
 * DISKCOMP [d: [d:]] [/1] [/8]
 */
#include "dosutil.h"

static const char M_CRLF[]    = "\r\n";
static const char M_USAGE[]   = "Do not specify filename(s)\r\nCommand format: DISKCOMP d: d: [/1][/8]\n\r";
static const char M_INVDRV[]  = "\r\nInvalid drive specification\r\nSpecified drive does not exist\r\nor is non-removable\r\n";
static const char M_NETWORK[] = "\r\nCannot DISKCOMP to or from\r\na network drive\r\n";
static const char M_FIRST[]   = "\r\nInsert FIRST diskette in drive %1:\r\n";
static const char M_SECOND[]  = "\r\nInsert SECOND diskette in drive %1:\r\n";
static const char M_FBAD[]    = "\r\nFIRST diskette bad or incompatible\r\n";
static const char M_SBAD[]    = "\r\nSECOND diskette bad or incompatible\r\n";
static const char M_DOOR[]    = "Make sure a diskette is inserted into\r\nthe drive and the door is closed\r\n";
static const char M_ANOTHER[] = "\r\nCompare another diskette (Y/N) ?";
static const char M_COMPARE[] = "\r\nComparing %1 tracks\r\n%2 sectors per track, %3 side(s)\r\n";
static const char M_NOTCOMP[] = "\r\nDrive types or diskette types\r\nnot compatible\r\n";
static const char M_RDERR[]   = "\r\nUnrecoverable read error on drive %1\r\nside %2, track %3\r\n";
static const char M_CMPERR[]  = "\r\nCompare error on\r\nside %1, track %2\r\n";
static const char M_ENDED[]   = "\r\nCompare process ended\r\n";
static const char M_OK[]      = "\r\nCompare OK\r\n";

static int d1, d2, single, one_side, eight;
static struct geom g;
static unsigned ncyl, spt, sides, bps;
static uint8_t *buf, *cylbuf;
static uint32_t bufsize, cyl_bytes;
static int errors, fatal;

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
    int n = parse_tail(a, 6), np = 0, d[2];
    for (int i = 0; i < n; i++) {
        struct arg *x = &a[i];
        if (x->sw) {
            if (!strcmp(x->text, "/1") && !x->val) { one_side = 1; continue; }
            if (!strcmp(x->text, "/8") && !x->val) { eight = 1; continue; }
            usage_exit(x->shown, M_INVSW);
        }
        if (!is_drive_spec(x->text)) usage_exit(x->shown, M_INVPARM);
        if (np == 2) usage_exit(x->shown, M_TOOMANY);
        d[np++] = (x->text[0] & 0xDF) - 'A';
    }
    int cur = cur_drive();
    d1 = np > 0 ? d[0] : cur;
    d2 = np > 1 ? d[1] : cur;
}

static void check_drives(void)
{
    for (int k = 0; k < 2; k++) {
        int drv = k ? d2 : d1;
        unsigned attr;
        if (ioctl_remote(drv, &attr)) { outs(M_INVDRV); dos_exit(1); }
        if (attr & 0x1000) { outs(M_NETWORK); dos_exit(1); }
        if (ioctl_removable(drv) != 1) { outs(M_INVDRV); dos_exit(1); }
    }
    single = d1 == d2 || (get_logical(d1) != 0 && get_logical(d2) != 0);
    if (single) d2 = d1;
    set_logical(d1);
    if (drive_geometry(d1, &g)) { outs(M_INVDRV); dos_exit(1); }
    cyl_bytes = g.heads * g.spt * 512;
    R r = { 0 };
    r.r0 = 0x4800; r.r1 = 0xFFFF;
    dos(&r);
    unsigned paras = r.r1 & 0xFFFF;
    if (paras > 64) paras -= 64;
    memset(&r, 0, sizeof r);
    r.r0 = 0x4800; r.r1 = paras;
    if (dos(&r) || (uint32_t)paras * 16 < 2 * cyl_bytes) { errs("Insufficient memory\r\n"); dos_exit(1); }
    cylbuf = (uint8_t *)((r.r0 & 0xFFFF) << 4);
    buf = cylbuf + cyl_bytes;
    bufsize = (uint32_t)paras * 16 - cyl_bytes;
}

static void ext_error(int e, int drive)
{
    char dl[4] = { 'A' + drive, ':', 0 };
    if (e == 21) { errs("Not ready - "); errs(dl); errs("\r\n"); outs(M_DOOR); }
    else { errs("Write protect error - "); errs(dl); errs("\r\n"); }
    press_any_key();
}

static int track_read(int drive, unsigned cyl, unsigned head, uint8_t *p)
{
    for (;;) {
        int e = trk_rw(drive, 0, &g, ((uint32_t)cyl * g.heads + head) * g.spt, spt, p);
        if (e == 21) { ext_error(e, drive); continue; }
        if (e) {
            char dl[4] = { 'A' + drive, 0 }, s1[6], s2[6];
            fmtnum(s1, head, 1, ' '); fmtnum(s2, cyl, 1, ' ');
            outfmt(STDOUT, M_RDERR, dl, s1, s2);
            errors++;
        }
        return e;
    }
}

/* the layout of the diskette in the drive, from its boot record */
static int layout(unsigned *cyl, unsigned *s, unsigned *h, unsigned *b)
{
    static uint8_t boot[512] __attribute__((aligned(4)));
    int e;
    while ((e = trk_rw(d1, 0, &g, 0, 1, boot)) == 21) ext_error(e, d1);
    if (e) return -1;
    const struct bpb *p = (const struct bpb *)(boot + 0x0B);
    uint32_t tot = p->total16 ? p->total16 : p->total32;
    if (p->bps != 512 || !p->spt || !p->heads || p->heads > 2 || p->spt > g.spt || tot > g.total) return -1;
    *s = p->spt; *h = p->heads; *b = p->bps; *cyl = tot / p->spt / p->heads;
    return 0;
}

static void compare(void)
{
    errors = fatal = 0;
    int in_drive = 1;                   /* 1 = FIRST, 2 = SECOND */
    char dl[3] = { 'A' + d1, 0 };
    char dl2[3] = { 'A' + d2, 0 };
    if (single) { outfmt(STDOUT, M_FIRST, dl, 0, 0); press_any_key(); }
    else { outfmt(STDOUT, M_FIRST, dl, 0, 0); outfmt(STDOUT, M_SECOND, dl2, 0, 0); press_any_key(); }
    unsigned h;
    if (layout(&ncyl, &spt, &h, &bps)) { outs(M_FBAD); fatal = 1; goto end; }
    if (eight) { if (spt < 8) { outs(M_FBAD); fatal = 1; goto end; } spt = 8; }
    sides = one_side ? 1 : h;
    {
        char a[8], b[8], c[8];
        fmtnum(a, ncyl, 1, ' '); fmtnum(b, spt, 1, ' '); fmtnum(c, sides, 1, ' ');
        outfmt(STDOUT, M_COMPARE, a, b, c);
    }
    unsigned per_pass = bufsize / cyl_bytes, cur = 0;
    while (cur < ncyl) {
        unsigned n = ncyl - cur < per_pass ? ncyl - cur : per_pass;
        for (unsigned c = 0; c < n; c++)
            for (unsigned hd = 0; hd < sides; hd++)
                track_read(d1, cur + c, hd, buf + c * cyl_bytes + hd * g.spt * 512);
        /* the other diskette */
        if (single) {
            outfmt(STDOUT, in_drive == 1 ? M_SECOND : M_FIRST, dl, 0, 0);
            press_any_key();
            in_drive = 3 - in_drive;
        }
        if (cur == 0) {
            unsigned c2, s2, h2, b2;
            if (layout(&c2, &s2, &h2, &b2)) { outs(M_SBAD); fatal = 1; goto end; }
            if (c2 != ncyl || s2 < spt || b2 != bps || (sides == 2 && h2 != 2)) { outs(M_NOTCOMP); fatal = 1; goto end; }
        }
        for (unsigned c = 0; c < n; c++)
            for (unsigned hd = 0; hd < sides; hd++) {
                uint8_t *mine = buf + c * cyl_bytes + hd * g.spt * 512, *other = cylbuf + hd * g.spt * 512;
                if (track_read(single ? d1 : d2, cur + c, hd, other)) continue;
                if (cur + c == 0 && hd == 0 && (other[0x15] & 0xF0) == 0xF0 && (other[0x26] == 0x28 || other[0x26] == 0x29))
                    memcpy(mine + 0x27, other + 0x27, 4);      /* the serial number does not count */
                if (memcmp(mine, other, spt * 512)) {
                    char s1[6], s2[6];
                    fmtnum(s1, hd, 1, ' '); fmtnum(s2, cur + c, 1, ' ');
                    outfmt(STDOUT, M_CMPERR, s1, s2, 0);
                    errors++;
                }
            }
        cur += n;
        if (!single) { int t = d1; d1 = d2; d2 = t; }
    }
end:
    disk_reset();
    if (fatal) outs(M_ENDED);
    else if (!errors) outs(M_OK);
}

int main(void)
{
    check_version();
    parse();
    check_drives();
    int first = d1;
    for (;;) {
        compare();
        for (;;) {
            outs(M_ANOTHER);
            int c = getche_flush();
            outs(M_CRLF);
            int yn = yesno(c);
            if (yn == 1) break;
            if (yn == 0) { set_logical(first); dos_exit(0); }
        }
    }
}
