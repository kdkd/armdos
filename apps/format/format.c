/*
 * FORMAT - ARM-DOS 4.00 disk formatter.
 *
 * A re-creation in C of MS-DOS 4.00 FORMAT.COM (CMD/FORMAT of the
 * MIT-licensed MS-DOS 4.0 source) - same dialogue, messages, switches, report
 * and on-disk result, verified against the real FORMAT.COM under DOSBox-X.
 * Tracks are formatted one by one through IOCTL 440Dh/42h (format track;
 * the block driver issues INT 13h), fixed disks are verified (62h) instead,
 * as 4.00's IO.SYS does.  The system areas are written with IOCTL write-track
 * requests (440Dh/41h), so an unformatted diskette never goes through DOS.
 *
 * FORMAT d: [/S] [/V[:label]] [/B] [/F:size] [/T:tracks /N:sectors] [/1] [/4] [/8]
 */
#include "dosutil.h"
#include "bootrec.h"
#include <stdlib.h>

#define SP(n) n
static const char M_PCT[]      = "\r%1 percent of disk formatted                                        \r";
static const char M_COMPLETE[] = "\rFormat complete                                                     \r\n";
static const char M_TERM[]     = "\rFormat terminated                                                   \r\n";
static const char M_INSERT[]   = "Insert new diskette for drive %1:\r\n";
static const char M_ENTER[]    = "\rand press ENTER when ready...";
static const char M_BADVOL[]   = "\rInvalid Volume ID                           \r\n";
static const char M_LABEL[]    = "Volume label (11 characters, ENTER for none)? ";
static const char M_ANOTHER[]  = "\rFormat another (Y/N)?";
static const char M_WARN[]     = "\r\nWARNING, ALL DATA ON NON-REMOVABLE DISK\r\nDRIVE %1: WILL BE LOST!\r\nProceed with Format (Y/N)?";
static const char M_CURLABEL[] = "\rEnter current volume label for drive %1: ";
static const char M_BADMEDIA[] = "\rInvalid media or Track 0 bad - disk unusable\r\n";
static const char M_UNSUIT[]   = "\rDisk unsuitable for system disk\r\n";
static const char M_NOTSUPP[]  = "\rFormat not supported on drive %1:\r\n";
static const char M_PNOTSUP[]  = "\rParameters not supported\r\n";
static const char M_PNOTSUPD[] = "\rParameters not supported by drive\r\n";
static const char M_PNOTCOMP[] = "\rParameters not compatible\r\n";
static const char M_PFIXED[]   = "\rParameters not compatible\r\nwith fixed disk\r\n";
static const char M_TWICE[]    = "\rSame parameter entered twice\r\n";
static const char M_TANDN[]    = "\rMust enter both /T and /N parameters\r\n";
static const char M_NETWORK[]  = "\rCannot FORMAT a network drive\r\n";
static const char M_SUBST[]    = "\rCannot format an ASSIGNed or SUBSTed drive. \r\n";
static const char M_NOSYS[]    = "\rCannot find System Files\r\n";
static const char M_DOSDISK[]  = "\rInsert DOS disk in drive %1:\r\n";
static const char M_ERRFAT[]   = "\rError writing FAT           \r\n";
static const char M_ERRDIR[]   = "\rError writing directory\r\n";
static const char M_ERRBOOT[]  = "\rUnable to write BOOT                        \r\n";
static const char M_SYSXFER[]  = "System transferred\r\n";

/* ---- options --------------------------------------------------------- */
static int drive = -1;
static char dletter[2];
static int o_s, o_v, o_b, o_1, o_4, o_8, o_f, o_t, o_n;
static unsigned tval, nval;
static char fsize[16];
static char vlabel[80];
static int have_vlabel;

/* ---- the disk --------------------------------------------------------- */
static int fixed;
static struct geom g;
static struct devparams dp;             /* the drive's default parameters */
static struct bpb bpb;                  /* the layout we write */
static unsigned devtype;
static uint32_t total, start_sector, clusters;
static int fat16;
static uint8_t *fatbuf;
static uint8_t sec[512] __attribute__((aligned(4)));
static uint8_t line[132];
static int exit_status;
static unsigned last_pct = 0xFFFF;
static uint32_t bad_clusters;
static volatile int critical;

/* ---- system files ----------------------------------------------------- */
struct sysfile { const char *name; uint8_t attr; uint8_t *data; uint32_t size; uint16_t date, time; };
static struct sysfile sf[3] = {
    { "IO.SYS", 7, 0, 0, 0, 0 }, { "ARMDOS.SYS", 7, 0, 0, 0, 0 }, { "COMMAND.COM", 0, 0, 0, 0, 0 },
};

static const struct bpb BPB1440 = { 512, 1, 1, 2, 224, 2880, 0xF0, 9, 18, 2, 0, 0 };
static const struct bpb BPB1200 = { 512, 1, 1, 2, 224, 2400, 0xF9, 7, 15, 2, 0, 0 };
static const struct bpb BPB720  = { 512, 2, 1, 2, 112, 1440, 0xF9, 3, 9, 2, 0, 0 };
static const struct bpb BPB360  = { 512, 2, 1, 2, 112, 720, 0xFD, 2, 9, 2, 0, 0 };
static const struct bpb BPB180  = { 512, 1, 1, 2, 64, 360, 0xFC, 2, 9, 1, 0, 0 };
static const struct bpb BPB320  = { 512, 2, 1, 2, 112, 640, 0xFF, 1, 8, 2, 0, 0 };
static const struct bpb BPB160  = { 512, 1, 1, 2, 64, 320, 0xFE, 1, 8, 1, 0, 0 };

/* device types (IOCTL 60h) */
#define DEV_5INCH     0
#define DEV_5INCH96   1
#define DEV_3INCH720  2
#define DEV_OTHER     7

static void outl(const char *msg) { outfmt(STDOUT, msg, dletter, 0, 0); }

static void terminate(void) { outs(M_TERM); }

static void fatal(const char *msg)
{
    if (msg) outl(msg);
    terminate();
    dos_exit(4);
}

static void ctrl_c(struct armregs *f)
{
    if (critical) { f->cpsr &= ~ARM_CPSR_C; return; }
    f->cpsr |= ARM_CPSR_C;              /* abort; DOS has echoed ^C */
}

/* ------------------------------------------------------------ parsing */

static void parse(void)
{
    static struct arg a[16];
    int n = parse_tail(a, 16);
    for (int i = 0; i < n; i++) {
        struct arg *x = &a[i];
        if (!x->sw) {
            if (drive >= 0) { parse_err(M_TOOMANY, x->shown); dos_exit(0); }
            if (!is_drive_spec(x->text)) { parse_err(M_INVPARM, x->shown); dos_exit(0); }
            upcase(x->text);
            drive = x->text[0] - 'A';
            continue;
        }
        char name[16];
        strncpy(name, x->text, 15); name[15] = 0;
        upcase(name);
        int *flag = 0;
        if (!strcmp(name, "/S")) flag = &o_s;
        else if (!strcmp(name, "/B")) flag = &o_b;
        else if (!strcmp(name, "/1")) flag = &o_1;
        else if (!strcmp(name, "/4")) flag = &o_4;
        else if (!strcmp(name, "/8")) flag = &o_8;
        else if (!strcmp(name, "/V")) flag = &o_v;
        else if (!strcmp(name, "/F") && x->val) flag = &o_f;
        else if (!strcmp(name, "/T") && x->val) flag = &o_t;
        else if (!strcmp(name, "/N") && x->val) flag = &o_n;
        if (!flag || (x->val && flag != &o_v && flag != &o_f && flag != &o_t && flag != &o_n)) {
            parse_err(M_INVSW, x->shown);
            dos_exit(0);
        }
        if (*flag) { outs(M_TWICE); dos_exit(0); }
        *flag = 1;
        if (flag == &o_v && x->val) {
            strncpy(vlabel, x->val, 79);
            upcase(vlabel);
            have_vlabel = 1;
        } else if (flag == &o_f) {
            strncpy(fsize, x->val, 15);
            upcase(fsize);
        } else if (flag == &o_t || flag == &o_n) {
            unsigned v = 0;
            const char *p = x->val;
            if (!*p) { parse_err(M_BADFMT, x->shown); dos_exit(0); }
            for (; *p; p++) {
                if (*p < '0' || *p > '9') { parse_err(M_BADFMT, x->shown); dos_exit(0); }
                v = v * 10 + (*p - '0');
            }
            if (flag == &o_t) tval = v; else nval = v;
        }
    }
    if (drive < 0) { parse_err(M_REQMISS, ""); dos_exit(0); }
    dletter[0] = 'A' + drive;
}

/* the volume label rules of FORLABEL.ASM Get_11_Characters: 0 = ok */
static int check_label(const char *in, char *out11, int typed)
{
    const char *p = in;
    if (typed) while (*p == ' ' || *p == '\t') p++;
    memset(out11, ' ', 11);
    int n = 0;
    for (; *p; p++) {
        if (strchr("*?[]:<|>+=;,/\\.\" ", *p) || (uint8_t)*p < ' ') return -1;
        if (n < 11) out11[n++] = *p;
    }
    return 0;
}

/* ------------------------------------------------ switches -> the BPB */

static void compute_fat(struct bpb *b)
{
    /* sectors per FAT so that all the clusters fit */
    uint32_t tot = b->total16 ? b->total16 : b->total32;
    unsigned rootsecs = (b->root * 32 + 511) / 512;
    unsigned spf = 1;
    for (int i = 0; i < 8; i++) {
        uint32_t data = tot - b->reserved - rootsecs - b->nfats * spf;
        uint32_t cl = data / b->spc;
        uint32_t bytes = cl + 1 >= 4086 ? (cl + 2) * 2 : ((cl + 2) * 3 + 1) / 2;
        unsigned need = (bytes + 511) / 512;
        if (need == spf) break;
        spf = need;
    }
    b->spf = spf;
}

/* the fixed disk's layout from its partition (IO.SYS's DiskTable2) */
static int fixed_bpb(struct bpb *b)
{
    const struct bpb *cur = &dp.bpb;
    if (cur->bps == 512 && cur->spc && !(cur->spc & (cur->spc - 1)) && cur->nfats && cur->root &&
        cur->spf && cur->media >= 0xF0 && (cur->total16 || cur->total32)) {
        *b = *cur;
        return 0;
    }
    /* no valid boot record: go by the partition table */
    uint8_t *mbr = sec;
    static struct { uint8_t size, res; uint16_t count; uint32_t buf, lba, lbahi; } dap;
    dap.size = 16; dap.count = 1; dap.buf = (uint32_t)mbr; dap.lba = 0; dap.lbahi = 0;
    R r = { 0 };
    r.r0 = 0x4200; r.r3 = 0x80; r.r4 = (uint32_t)&dap;
    if (intr(0x13, &r) || mbr[510] != 0x55 || mbr[511] != 0xAA) return -1;
    for (int p = 0; p < 4; p++) {
        const uint8_t *e = mbr + 0x1BE + p * 16;
        if (e[4] != 1 && e[4] != 4 && e[4] != 6) continue;
        uint32_t hidden = e[8] | (e[9] << 8) | (e[10] << 16) | ((uint32_t)e[11] << 24);
        uint32_t tot = e[12] | (e[13] << 8) | (e[14] << 16) | ((uint32_t)e[15] << 24);
        memset(b, 0, sizeof *b);
        b->bps = 512; b->reserved = 1; b->nfats = 2; b->media = 0xF8;
        b->spt = g.spt; b->heads = g.heads; b->hidden = hidden;
        if (tot < 65536) b->total16 = tot; else b->total32 = tot;
        if (tot <= 32680) { b->spc = tot <= 512 ? 1 : tot <= 2048 ? 2 : tot <= 8192 ? 4 : 8;
                            b->root = tot <= 512 ? 64 : tot <= 2048 ? 112 : tot <= 8192 ? 256 : 512; }
        else { uint32_t mb = tot / 2048;
               b->spc = mb <= 128 ? 4 : mb <= 256 ? 8 : mb <= 512 ? 16 : mb <= 1024 ? 32 : 64;
               b->root = 512; }
        compute_fat(b);
        return 0;
    }
    return -1;
}

/* CheckSwitches (MSFOR.ASM): 0 ok, or prints its message and returns -1 */
static int check_switches(void)
{
    if (fixed) {
        if (o_1 || o_4 || o_8 || o_f || o_t || o_n) { outs(M_PFIXED); return -1; }
        if (fixed_bpb(&bpb)) { outl(M_NOTSUPP); return -1; }
        return 0;
    }
    /* the diskette drive's own format, by the physical geometry */
    if (g.spt >= 18) bpb = BPB1440;
    else if (g.spt == 15) bpb = BPB1200;
    else if (g.cyls >= 80) bpb = BPB720;
    else bpb = BPB360;
    if ((o_b && o_8 && o_v) || (o_b && o_s) || (o_8 && o_v) ||
        ((o_1 || o_4 || o_8) && (o_n || o_t))) { outs(M_PNOTCOMP); return -1; }
    if (o_f) {
        if (o_1 || o_4 || o_8 || o_n || o_t) { outs(M_PNOTCOMP); return -1; }
        const char *s = fsize;
        static const char *const k1440[] = { "1440", "1440K", "1440KB", "1.44", "1.44M", "1.44MB", 0 };
        static const char *const k720[]  = { "720", "720K", "720KB", 0 };
        static const char *const k1200[] = { "1200", "1200K", "1200KB", "1.2", "1.2M", "1.2MB", 0 };
        static const char *const k360[]  = { "360", "360K", "360KB", 0 };
        static const char *const k320[]  = { "320", "320K", "320KB", 0 };
        static const char *const k180[]  = { "180", "180K", "180KB", 0 };
        static const char *const k160[]  = { "160", "160K", "160KB", 0 };
        #define IS(t) ({ int _m = 0; for (int _i = 0; t[_i]; _i++) if (!strcmp(s, t[_i])) _m = 1; _m; })
        if (devtype == DEV_OTHER || devtype == DEV_3INCH720) {
            if (IS(k1440) && devtype == DEV_OTHER) { o_t = o_n = 1; tval = 80; nval = 18; }
            else if (IS(k720)) { if (devtype == DEV_OTHER) { o_t = o_n = 1; tval = 80; nval = 9; } }
            else { outs(M_PNOTCOMP); return -1; }
        } else if (devtype == DEV_5INCH96 || devtype == DEV_5INCH) {
            if (IS(k1200) && devtype == DEV_5INCH96) { }
            else if (IS(k360)) { if (devtype == DEV_5INCH96) o_4 = 1; }
            else if (IS(k320)) { o_8 = 1; if (devtype == DEV_5INCH96) o_4 = 1; }
            else if (IS(k180)) { o_1 = 1; if (devtype == DEV_5INCH96) o_4 = 1; }
            else if (IS(k160)) { o_1 = o_8 = 1; if (devtype == DEV_5INCH96) o_4 = 1; }
            else { outs(M_PNOTCOMP); return -1; }
        } else { outs(M_PNOTSUPD); outs(M_PNOTCOMP); return -1; }
    }
    if (o_n != o_t) { outs(M_TANDN); return -1; }
    if (devtype == DEV_OTHER || devtype == DEV_3INCH720) {
        if (o_1 || o_4 || o_8) { outs(M_PNOTSUPD); return -1; }
    }
    if (o_1 || o_4 || o_8) {
        if (devtype == DEV_5INCH96 && o_1 && !o_4) { outs(M_PNOTCOMP); return -1; }
        int idx = (o_1 ? 1 : 0) + (o_8 ? 2 : 0);
        bpb = idx == 0 ? BPB360 : idx == 1 ? BPB180 : idx == 2 ? BPB320 : BPB160;
    }
    if (o_n) {
        if (tval == 80 && nval == 9) bpb = BPB720;
        else {
            if (!nval || !tval || nval > 63) { outs(M_PNOTSUP); return -1; }
            bpb.spt = nval;
            bpb.total16 = nval * bpb.heads * tval;
            bpb.spf = (bpb.total16 / bpb.spc * 3 / 2) / 512 + 1;
            bpb.media = 0xF0;
        }
    }
    return 0;
}

/* ------------------------------------------------------ system files */

static int read_file(const char *path, struct sysfile *f)
{
    R r = { 0 };
    r.r0 = 0x3D00; r.r3 = (uint32_t)path;
    if (dos(&r)) return r.r0 & 0xFFFF;
    int h = r.r0 & 0xFFFF;
    memset(&r, 0, sizeof r);
    r.r0 = 0x4202; r.r1 = h;
    dos(&r);
    f->size = ((r.r3 & 0xFFFF) << 16) | (r.r0 & 0xFFFF);
    memset(&r, 0, sizeof r);
    r.r0 = 0x4200; r.r1 = h;
    dos(&r);
    memset(&r, 0, sizeof r);
    r.r0 = 0x5700; r.r1 = h;
    dos(&r);
    f->time = r.r2; f->date = r.r3;
    free(f->data);
    f->data = malloc(f->size ? f->size : 1);
    int err = f->data ? 0 : 8;
    for (uint32_t done = 0; !err && done < f->size; ) {
        uint32_t n = f->size - done > 0x8000 ? 0x8000 : f->size - done;
        memset(&r, 0, sizeof r);
        r.r0 = 0x3F00; r.r1 = h; r.r2 = n; r.r3 = (uint32_t)(f->data + done);
        if (dos(&r) || (r.r0 & 0xFFFF) != n) err = 5;
        done += n;
    }
    memset(&r, 0, sizeof r);
    r.r0 = 0x3E00; r.r1 = h;
    dos(&r);
    return err;
}

/* READDOS / SYSPRM */
static void load_system(void)
{
    for (;;) {
        int bd = boot_drive(), err = 0;
        char path[80];
        for (int i = 0; i < 3 && !err; i++) {
            path[0] = 'A' + bd; path[1] = ':'; path[2] = '\\';
            strcpy(path + 3, sf[i].name);
            if (i == 2) {
                /* COMMAND.COM: COMSPEC if it is on the boot drive */
                extern char **environ;
                for (char **e = environ; e && *e; e++)
                    if (!strncmp(*e, "COMSPEC=", 8) && ((*e)[8] & 0xDF) == 'A' + bd && (*e)[9] == ':') {
                        strncpy(path, *e + 8, 79); path[79] = 0;
                    }
            }
            err = read_file(path, &sf[i]);
        }
        if (!err) return;
        if (err == 4) { errs("Too many open files\r\n"); dos_exit(4); }
        if (err == 8) { errs("Insufficient memory\r\n"); dos_exit(4); }
        int def = cur_drive();
        int letter = def;
        if (ioctl_removable(def) != 1) {
            letter = 0;
            if (ioctl_removable(0) != 1) { outs(M_NOSYS); dos_exit(4); }
        }
        char dl[2] = { 'A' + letter, 0 };
        outfmt(STDOUT, M_DOSDISK, dl, 0, 0);
        outs(M_ENTER);
        getline_flush(line, 80);
        outs("\r\n\r\n");
    }
}

static int write_file(struct sysfile *f)
{
    char path[16] = { 'A' + drive, ':', '\\' };
    strcpy(path + 3, f->name);
    R r = { 0 };
    r.r0 = 0x3C00; r.r2 = f->attr; r.r3 = (uint32_t)path;
    if (dos(&r)) return -1;
    int h = r.r0 & 0xFFFF, err = 0;
    for (uint32_t done = 0; !err && done < f->size; ) {
        uint32_t n = f->size - done > 0x8000 ? 0x8000 : f->size - done;
        memset(&r, 0, sizeof r);
        r.r0 = 0x4000; r.r1 = h; r.r2 = n; r.r3 = (uint32_t)(f->data + done);
        if (dos(&r) || (r.r0 & 0xFFFF) != n) err = -1;
        done += n;
    }
    memset(&r, 0, sizeof r);
    r.r0 = 0x5701; r.r1 = h; r.r2 = f->time; r.r3 = f->date;
    dos(&r);
    memset(&r, 0, sizeof r);
    r.r0 = 0x3E00; r.r1 = h;
    if (dos(&r)) err = -1;
    return err;
}

/* ------------------------------------------------------ volume labels */

static uint8_t xfcb[44], dta[64];

static void set_dta(void *p)
{
    R r = { 0 };
    r.r0 = 0x1A00; r.r3 = (uint32_t)p;
    dos(&r);
}

static void make_xfcb(const char *name11)
{
    memset(xfcb, 0, sizeof xfcb);
    xfcb[0] = 0xFF;
    xfcb[6] = 0x08;
    xfcb[7] = drive + 1;
    memcpy(xfcb + 8, name11, 11);
}

static int find_label(char *out11)
{
    set_dta(dta);
    make_xfcb("???????????");
    R r = { 0 };
    r.r0 = 0x1100; r.r3 = (uint32_t)xfcb;
    dos(&r);
    if ((r.r0 & 0xFF) != 0) return 0;
    memcpy(out11, dta + 8, 11);
    return 1;
}

static void create_label(const char *lab11)
{
    R r = { 0 };
    make_xfcb("???????????");
    r.r0 = 0x1300; r.r3 = (uint32_t)xfcb;
    dos(&r);
    char up[12];
    memcpy(up, lab11, 11); up[11] = 0;
    upcase(up);                         /* DOS keeps names upper case */
    lab11 = up;
    make_xfcb(lab11);
    memset(&r, 0, sizeof r);
    r.r0 = 0x1600; r.r3 = (uint32_t)xfcb;
    dos(&r);
    if ((r.r0 & 0xFF) == 0) {
        memset(&r, 0, sizeof r);
        r.r0 = 0x1000; r.r3 = (uint32_t)xfcb;
        dos(&r);
        /* the label in the boot record too (DOS 4's DOS_Create does this) */
        struct { uint16_t level; uint32_t serial; char label[11]; char fs[8]; } __attribute__((packed)) mid;
        memset(&mid, 0, sizeof mid);
        if (!gen_ioctl(drive, 0x66, &mid)) {
            memcpy(mid.label, lab11, 11);
            gen_ioctl(drive, 0x46, &mid);
        }
    }
}

static void volume_label(void)
{
    char lab[11];
    if (have_vlabel) {
        check_label(vlabel, lab, 0);
    } else {
        for (;;) {
            outs(M_LABEL);
            int n = getline_flush(line, 128);
            line[2 + n] = 0;
            crlf();
            if (!check_label((char *)line + 2, lab, 1)) break;
            outs(M_BADVOL);
        }
        if (!memcmp(lab, "           ", 11)) return;
    }
    create_label(lab);
}

/* ------------------------------------------------------------ format */

static void wait_ticks(unsigned n)
{
    uint32_t t0 = ARMDOS_BIOS_TICKS;
    while (ARMDOS_BIOS_TICKS - t0 < n) armdos_halt();
}

static void check_break(void)
{
    R r = { 0 };
    r.r0 = 0x0B00;
    dos(&r);
}

static void show_pct(unsigned pct)
{
    if (pct == last_pct) return;
    last_pct = pct;
    char b[8];
    fmtnum(b, pct, 3, ' ');
    outfmt(STDOUT, M_PCT, b, 0, 0);
}

static void mark_bad(uint32_t lba)
{
    uint32_t c = (lba - start_sector) / bpb.spc + 2;
    if (c > clusters + 1) return;
    unsigned v = fat16 ? 0xFFF7 : 0xFF7;
    if (fat16) { fatbuf[c * 2] = v; fatbuf[c * 2 + 1] = v >> 8; }
    else {
        uint32_t o = c + c / 2;
        if (c & 1) { fatbuf[o] = (fatbuf[o] & 0x0F) | (v << 4); fatbuf[o + 1] = v >> 4; }
        else { fatbuf[o] = v; fatbuf[o + 1] = (fatbuf[o + 1] & 0xF0) | (v >> 8); }
    }
    bad_clusters++;
}

static const char *ext_error(int e)
{
    return e == 21 ? "Not ready\r\n" : e == 19 ? "Write protect error\r\n" : 0;
}

/* returns 0, or -1 after "Format terminated" */
static int format_tracks(void)
{
    uint32_t ntracks = (total + g.spt - 1) / g.spt;
    unsigned per_tick = fixed ? 16 : 1;
    for (uint32_t t = 0; t < ntracks; t++) {
        unsigned cyl = t / g.heads, head = t % g.heads;
        int e = trk_format(drive, cyl, head, fixed);
        if (e) {
            const char *x = ext_error(e);
            if (x) {
                outs("\r\n\r\n");
                errs(x);
                terminate();
                return -1;
            }
            if (t == 0) { outs(M_BADMEDIA); terminate(); return -1; }
            uint32_t lba0 = t * g.spt;
            if (lba0 < start_sector) { outs(M_BADMEDIA); terminate(); return -1; }
            for (unsigned s = 0; s < g.spt && lba0 + s < total; s++) mark_bad(lba0 + s);
        }
        /* a real drive needs a moment per track; the emulated one does not */
        if (fixed) { if ((t % per_tick) == per_tick - 1) wait_ticks(1); }
        else wait_ticks(3);
        check_break();
        show_pct((t + 1) * 100 / ntracks);
    }
    outs(M_COMPLETE);
    return 0;
}

static int write_system_area(uint32_t serial)
{
    uint8_t *root;
    unsigned rootsecs = (bpb.root * 32 + 511) / 512;
    critical = 1;
    make_bootrec(sec, &bpb, fixed, serial, 0, fat16);
    if (trk_rw(drive, 1, &g, 0, 1, sec)) {
        outs(M_ERRBOOT); outs(M_BADMEDIA); terminate(); critical = 0; return -1;
    }
    for (unsigned f = 0; f < bpb.nfats; f++) {
        if (trk_rw(drive, 1, &g, bpb.reserved + f * bpb.spf, bpb.spf, fatbuf)) {
            outs(M_ERRFAT); critical = 0; return -1;
        }
    }
    root = calloc(1, 512);
    if (o_8) for (unsigned i = 0; i < 512; i += 32) root[i] = 0xE5;
    for (unsigned s = 0; s < rootsecs; s++) {
        if (trk_rw(drive, 1, &g, bpb.reserved + bpb.nfats * bpb.spf + s, 1, root)) {
            outs(M_ERRDIR); free(root); critical = 0; return -1;
        }
    }
    free(root);
    /* tell DOS: the parameters changed (the DPB is rebuilt), buffers are stale */
    struct devparams np = dp;
    np.special = 4;
    np.bpb = bpb;
    np.tracks = 0;
    gen_ioctl(drive, 0x40, &np);
    disk_reset();
    forget_free(drive, 0);
    critical = 0;
    return 0;
}

static uint32_t cluster_round(uint32_t size, uint32_t csize)
{
    return (size + csize - 1) / csize * csize;
}

static void report(uint32_t serial)
{
    R r = { 0 };
    r.r0 = 0x3600; r.r3 = drive + 1;
    dos(&r);
    uint32_t spc = r.r0 & 0xFFFF, freec = r.r1 & 0xFFFF, bps = r.r2 & 0xFFFF, totc = r.r3 & 0xFFFF;
    uint32_t csize = spc * bps;
    uint32_t sys = 0;
    if (o_s) for (int i = 0; i < 3; i++) sys += cluster_round(sf[i].size, csize);
    uint32_t used = (totc - freec) * csize;
    uint32_t bad = used > sys ? used - sys : 0;
    crlf();
    outnum(totc * csize, " bytes total disk space");
    if (sys) outnum(sys, " bytes used by system");
    if (bad) outnum(bad, " bytes in bad sectors");
    outnum(freec * csize, " bytes available on disk");
    crlf();
    outnum(csize, " bytes in each allocation unit");
    outnum(freec, " allocation units available on disk");
    crlf();
    if (!o_8) { serial_line(serial); crlf(); }
}

/* USER_STRING + Yes? */
static int ask_yes(const char *prompt, int reprompt)
{
    for (;;) {
        outl(prompt);
        int n = getline_flush(line, 80);
        int yn = n ? yesno(line[2]) : -1;
        if (yn == 1) { outs("\r\n\r\n"); return 1; }
        if (yn == 0 || !reprompt) return 0;
        crlf();
    }
}

static int one_disk(void)
{
    exit_status = 0;
    bad_clusters = 0;
    if (!fixed) {
        outl(M_INSERT);
        outs(M_ENTER);
        getline_flush(line, 80);
        outs("\r\n\r\n");
        if (o_n && !o_8) {
            /* STATUS_FOR_FORMAT: can the drive do this layout? */
            uint8_t st[5] = { 1, 0, 0, 0, 0 };
            if (gen_ioctl(drive, 0x42, st) == 21) {
                outs("\r\n\r\n"); errs("Not ready\r\n"); terminate(); return 4;
            }
        }
    }
    total = bpb.total16 ? bpb.total16 : bpb.total32;
    unsigned rootsecs = (bpb.root * 32 + 511) / 512;
    start_sector = bpb.reserved + bpb.nfats * bpb.spf + rootsecs;
    clusters = (total - start_sector) / bpb.spc;
    fat16 = clusters + 1 >= 4086;
    fatbuf = realloc(fatbuf, bpb.spf * 512);
    if (!fatbuf) { errs("Insufficient memory\r\n"); dos_exit(4); }
    memset(fatbuf, 0, bpb.spf * 512);
    fatbuf[0] = bpb.media; fatbuf[1] = 0xFF; fatbuf[2] = 0xFF; fatbuf[3] = fat16 ? 0xFF : 0x00;

    if (format_tracks()) return 4;
    uint32_t serial = format_serial();
    if (write_system_area(serial)) return 4;
    if (o_s) {
        uint32_t csize = bpb.spc * 512;
        uint32_t sys_end = start_sector + cluster_round(sf[0].size, csize) / 512;
        int unsuitable = 0;
        (void)sys_end;
        if (bad_clusters) {
            /* IO.SYS must be contiguous from the first data cluster */
            uint32_t need = cluster_round(sf[0].size, csize) / csize;
            for (uint32_t c = 2; c < 2 + need; c++) {
                uint32_t v = fat16 ? (fatbuf[c * 2] | (fatbuf[c * 2 + 1] << 8))
                                   : ((c & 1) ? (fatbuf[c + c / 2] >> 4) | (fatbuf[c + c / 2 + 1] << 4)
                                              : fatbuf[c + c / 2] | ((fatbuf[c + c / 2 + 1] & 0x0F) << 8));
                if (v) unsuitable = 1;
            }
        }
        critical = 1;
        for (int i = 0; i < 3 && !unsuitable; i++) if (write_file(&sf[i])) unsuitable = 1;
        critical = 0;
        if (unsuitable) { outs(M_UNSUIT); exit_status = 4; }
        else if (!unsuitable) outs(M_SYSXFER);
    }
    crlf();
    disk_reset();
    if (!o_8) volume_label();
    report(serial);
    return exit_status;
}

int main(void)
{
    check_version();
    parse();

    unsigned attr;
    if (ioctl_remote(drive, &attr)) { errs(M_INVDRIVE); errs("\r\n"); dos_exit(0); }
    if (attr & 0x1200) { outs(M_NETWORK); dos_exit(0); }
    if ((attr & 0x8000) || truename_letter(drive) != 'A' + drive) { outs(M_SUBST); dos_exit(0); }
    armdos_setvect(0x23, ctrl_c);

    memset(&dp, 0, sizeof dp);
    if (gen_ioctl(drive, 0x60, &dp)) { outl(M_NOTSUPP); dos_exit(4); }
    devtype = dp.devtype;
    fixed = ioctl_removable(drive) == 0;
    if (drive_geometry(drive, &g)) { outl(M_NOTSUPP); dos_exit(4); }
    if (check_switches()) fatal(0);
    if (have_vlabel) {
        char lab[11];
        if (check_label(vlabel, lab, 0)) { outs(M_BADVOL); dos_exit(0); }
    }
    if (o_s) load_system();

    if (fixed) {
        char cur[11];
        if (find_label(cur)) {
            outl(M_CURLABEL);
            int n = getline_flush(line, 80);
            crlf();
            char typed[12];
            memset(typed, ' ', 11);
            for (int i = 0; i < n && i < 11; i++) typed[i] = line[2 + i];
            typed[11] = 0;
            upcase(typed);
            if (!n || memcmp(typed, cur, 11)) { outs(M_BADVOL); terminate(); dos_exit(4); }
        }
        if (!ask_yes(M_WARN, 0)) dos_exit(5);
        dos_exit(one_disk());
    }
    for (;;) {
        int st = one_disk();
        for (;;) {
            outs(M_ANOTHER);
            int n = getline_flush(line, 80);
            int yn = n ? yesno(line[2]) : -1;
            if (yn == 1) { outs("\r\n\r\n"); break; }
            if (yn == 0) dos_exit(st);
            crlf();
        }
    }
}
