/*
 * cmds1.c - DIR (TCMD1A.ASM), VOL, DEL/ERASE, REN, TYPE, PAUSE (TCMD1B.ASM,
 * TUCODE.ASM) and the directory helpers they share: SETPATH (TMISC2.ASM),
 * PATHCRUNCH, SAVUDIR, RESTUDIR (TENV2.ASM, TENV.ASM).
 */
#include "cmd.h"

int destinfo, destisdir, msg_numb;
char srcbuf[COMBUFLEN + 16];
char *desttail;
char bwdbuf[80];
static const char *pathstart;

/* MOVE_TO_SRCBUF */
static void move_to_srcbuf(const char *s)
{
    xstrlcpy(srcbuf, s, sizeof srcbuf - 1);
    int l = strlen(srcbuf);
    srcbuf[l] = '\r';
    srcbuf[l + 1] = 0;
}

/* SETPATH: NUL-terminate the path at its end; wildcards -> destinfo 2,
 * path characters counted in destisdir */
void setpath(const char *src)
{
    char *s = (char *)src;
    destinfo = 0;
    destisdir = 0;
    while (*s == ' ' || *s == '\t') s++;
    pathstart = s;
    for (;; s++) {
        int c = (uint8_t)*s;
        if (c == '\r' || c == 0) break;
        if (pathchr(c)) destisdir++;
        if (c == '?' || c == '*') destinfo |= 2;
        if (is_delim(c) || c == switchar) break;
    }
    *s = 0;
}

/* SAVUDIR: remember the current directory of a drive (1-based, 0 = default) */
static int savudir(int drive)
{
    char *d = userdir1;
    int letter = drive ? drive + '@' : curdrv + 'A';
    *d++ = letter;
    *d++ = ':';
    *d++ = dirchar;
    return dos_curdir(letter - '@', d);
}

void restudir(void)
{
    dos_chdir(userdir1);
    restdir = 0;
}

/* PATHCRUNCH: CHDIR to the directory part of srcbuf.  Returns CF; *zf is
 * set when the whole thing was a directory (the FCB is then "????????.???").
 * dirflag: called from DIR (don't parse the last element into the FCB). */
int pathcrunch(int dirflag, int *zf)
{
    msg_numb = 0;
    *zf = 0;
    if (savudir(FCB[0])) {
        msg_numb = dos_error();
        return 1;
    }
    setpath(srcbuf);
    char *path = (char *)pathstart;
    if (!(destinfo & 2)) {
        int e = dos_chdir(path);
        if (!e) {
            restdir = 1;
            memset(FCB + 1, '?', 11);
            *zf = 1;
            return 0;
        }
        e = dos_error();
        if (e != 3 && e != 5) {
            msg_numb = e;
            return 1;
        }
    }
    /* TRYPEEL */
    char *end = path + strlen(path);
    if (end > path && pathchr((uint8_t)end[-1])) return 1;
    char *sep = path;
    for (char *p = path; p < end; p++)
        if (pathchr((uint8_t)*p)) sep = p;
    if (sep != path) {
        if (sep[1] == '.') return 1;
        if (sep[-1] == ':') goto badret;
        if (pathchr((uint8_t)sep[-1])) return 1;
        *sep = 0;
        if (dos_chdir(path)) {
            msg_numb = dos_error();
            return 1;
        }
        goto cdsucc;
    }
badret:
    if (!pathchr((uint8_t)*sep)) return 1;
    {
        char save = sep[1];
        sep[1] = 0;
        if (dos_chdir(path)) {
            msg_numb = dos_error();
            sep[1] = save;
            return 1;
        }
        sep[1] = save;
    }
cdsucc:
    restdir = 1;
    desttail = sep + 1;
    if (!dirflag) {
        const char *t = desttail;
        dos_fcb_parse(&t, FCB, 0x02);
    }
    return 0;
}

/* build_dir_string: bwdbuf = "D:\CURRENT" for the drive in the FCB */
void build_dir_string(void)
{
    int d = FCB[0] ? FCB[0] + '@' : curdrv + 'A';
    bwdbuf[0] = d;
    bwdbuf[1] = ':';
    bwdbuf[2] = dirchar;
    if (dos_curdir(d - '@', bwdbuf + 3)) cerror_msg(M_BADDRV);
}

/* ------------------------------------------------------ OKVOLARG/VOL -- */

void okvolarg(void)
{
    crlf2();
    dos_putc(' ');
    uint8_t *x = FCB - 7;
    x[0] = 0xFF;
    memset(x + 1, 0, 5);
    x[6] = 0x08;
    memset(FCB + 1, '?', 11);
    dos_setdta(dirbuf);
    REGS r = {0};
    r.r0 = 0x1100;
    r.r3 = (uint32_t)x;
    int21(&r);
    char drv[2] = { FCB[0] ? FCB[0] + '@' : curdrv + 'A', 0 };
    char label[12];
    int n;
    if ((r.r0 & 0xFF) == 0) {
        memcpy(label, dirbuf + 8, 11);
        label[11] = 0;
        n = M_VOLMES;
    } else {
        n = M_VOLMES2;
    }
    /* the serial number (INT 21h 6900h) */
    uint8_t info[32];
    memset(info, 0, sizeof info);
    REGS m = {0};
    m.r0 = 0x6900;
    m.r1 = FCB[0];
    m.r3 = (uint32_t)info;
    if (int21(&m)) {
        msgout(1, n, drv, label, 0);
        return;
    }
    msgout(1, n, drv, label, 0);
    dos_putc(' ');
    char h1[6], h2[6];
    uint32_t ser = info[2] | (info[3] << 8) | (info[4] << 16) | ((uint32_t)info[5] << 24);
    fmt_hex4(h1, ser >> 16);
    fmt_hex4(h2, ser & 0xFFFF);
    msgout(1, M_VOLSERMES, h1, h2, 0);
}

static const struct pblock pb_vol = { 0, 1, K_DRIVE, 0, 1, 0, 0 };

void c_vol(void)
{
    const char *s = (const char *)TAIL + 1;
    int np = 0;
    struct pres r;
    if (parse_with_msg(&s, &pb_vol, &np, &r) != PT_EOL)
        parse_check_eol(&s, &pb_vol, &np);
    okvolarg();
}

/* ---------------------------------------------------------------- DIR -- */

static const char *const dir_sw[] = { "/P", "/W", 0 };
static const struct pblock pb_dir = { 0, 1, K_FILE, dir_sw, 1, 0, 0 };

static int dir_bits, fullscr, linperpag, linlen, lincnt;
static uint16_t last_dirtime;

static void check_for_p(void)
{
    if (dir_bits & SW_P) {
        fullscr = linperpag;
        c_pause();
    }
}

__attribute__((noreturn)) static void dir_err(int e)
{
    cerror_ext(e, NULL);
}

void c_dir(void)
{
    const char *s = (const char *)TAIL + 1;
    int np = 0, comsw_dir = 0;
    struct pres r;

    dos_setdta(dirbuf);
    srcbuf[0] = '*';
    srcbuf[1] = '\r';
    srcbuf[2] = 0;
    dir_bits = 0;
    linperpag = 25;
    linlen = lincnt = 1;
    for (;;) {
        int e = parse_with_msg(&s, &pb_dir, &np, &r);
        if (e == PT_EOL) break;
        if (r.type == PT_SWITCH) {
            if (r.sw == 1) {
                if (dir_bits & SW_W) cerror_parse(P_TOOMANY, err_sub);
                dir_bits |= SW_W;
                linlen = lincnt = 5;
            } else {
                if (dir_bits & SW_P) cerror_parse(P_TOOMANY, err_sub);
                dir_bits |= SW_P;
                /* rows of the display (generic IOCTL 440Ch CL=7Fh) */
                uint8_t info[20];
                memset(info, 0, sizeof info);
                info[2] = 14;
                REGS g = {0};
                g.r0 = 0x440C;
                g.r1 = 1;
                g.r2 = 0x037F;
                g.r3 = (uint32_t)info;
                if (!int21(&g)) {
                    int rows = info[16] | (info[17] << 8);
                    if (rows) linperpag = rows;
                }
                linperpag -= 2;
                fullscr = linperpag;
            }
            continue;
        }
        move_to_srcbuf(r.text);
        int fe = dos_findfirst(r.text, np);
        if (!fe) {
            if (dirbuf[21] & 0x40) comsw_dir = -2;
        } else {
            int x = dos_error();
            if (x != 18 && x != 3) dir_err(x);
        }
    }
    /* ScanDone */
    okvolarg();
    int filecnt = 0;
    if (comsw_dir == 0) {
        int zf;
        int cf = pathcrunch(1, &zf);
        if (!cf) {
            if (!zf) {
                const char *t = desttail;
                dos_fcb_parse(&t, FCB, 0x0E);
            }
        } else {
            if (msg_numb) dir_err(msg_numb);
            if (destisdir) {
                comsw_dir = 1;
            } else {
                const char *t = srcbuf;
                if (t[1] == ':') t += 2;
                if (t[0] == '.' && t[1] == '.' && (t[2] == 0 || t[2] == '\r')) {
                    comsw_dir = 1;
                } else {
                    dos_fcb_parse(&t, FCB, 0x0E);
                }
            }
        }
    }
    /* DoHeader */
    dos_putc(' ');
    build_dir_string();
    msgout(1, M_DIRHEAD, bwdbuf, 0, 0);
    crlf(1);
    if (comsw_dir) {
        restudir();
        if (comsw_dir < 0) dir_err(2);
        cerror_msg(M_BADCD);
    }
    /* DoSearch */
    uint8_t *x = FCB - 7;
    x[0] = 0xFF;
    memset(x + 1, 0, 5);
    x[6] = 0x10;
    dos_setdta(dirbuf);
    REGS q = {0};
    q.r0 = 0x1100;
    q.r3 = (uint32_t)x;
    int21(&q);
    if ((q.r0 & 0xFF) == 0xFF) {
        int e = dos_error();
        restudir();
        dir_err(e == 18 ? 2 : e);
    }
    restudir();
    for (;;) {
        if ((q.r0 & 0xFF) == 0xFF) break;
        filecnt++;
        const uint8_t *de = dirbuf + 8;
        char name[16];
        memcpy(name, de, 8);
        name[8] = ' ';
        memcpy(name + 9, de + 8, 3);
        name[12] = 0;
        out(1, name);
        if (!(dir_bits & SW_W)) {
            if (de[11] & 0x10) {
                std_printf(M_DMES);
            } else {
                char sz[16];
                uint32_t size = de[28] | (de[29] << 8) | (de[30] << 16) | ((uint32_t)de[31] << 24);
                fmt_uint(sz, size, 10, ' ');
                out(1, sz);
            }
            uint16_t date = de[24] | (de[25] << 8);
            if (date) {
                uint16_t tm = de[22] | (de[23] << 8);
                if (tm) last_dirtime = tm;
                int y = (date >> 9) + 80;
                if (y >= 100) y -= 100;
                char ds[16], ts[16], tsr[16];
                fmt_date(ds, y, (date >> 5) & 15, date & 31, 0);
                fmt_time(ts, last_dirtime >> 11, (last_dirtime >> 5) & 63, 0, 0, 2, 1);
                int l = strlen(ts), i = 0;
                while (l + i < 6) tsr[i++] = ' ';
                strcpy(tsr + i, ts);
                msgout(1, 1077, ds, tsr, 0);
            }
            crlf2();
            if (--fullscr == 0) check_for_p();
        } else {
            int bl = lincnt;
            if (--lincnt == 0) {
                lincnt = linlen;
                crlf2();
                if (fullscr == 0) check_for_p();
            } else {
                if (bl == linlen) fullscr--;
                std_printf(1067);
            }
        }
        memset(&q, 0, sizeof q);
        q.r0 = 0x1200;
        q.r3 = (uint32_t)x;
        int21(&q);
    }
    /* DirDone */
    {
        int e = dos_error();
        if (e != 18) dir_err(e);
        if (!filecnt) dir_err(2);
    }
    if (linlen != lincnt) {
        crlf2();
        if (fullscr == 0) check_for_p();
    }
    {
        char n[16];
        fmt_uint(n, filecnt, 9, ' ');
        msgout(1, M_DIRMES, n, 0, 0);
    }
    REGS f = {0};
    f.r0 = 0x3600;
    f.r3 = FCB[0];
    int21(&f);
    if ((f.r0 & 0xFFFF) == 0xFFFF) return;
    uint32_t bytes = (f.r0 & 0xFFFF) * (f.r2 & 0xFFFF) * (f.r1 & 0xFFFF);
    char b[16];
    fmt_uint(b, bytes, 10, ' ');
    msgout(1, M_BYTMES, b, 0, 0);
}

/* -------------------------------------------------------------- PAUSE -- */

void c_pause(void)
{
    std_printf(M_PAUSEMES);
    getkeystroke();
    crlf2();
}

/* --------------------------------------------------------- DEL/ERASE -- */

static const char *const p_sw[] = { "/P", 0 };
static const struct pblock pb_erase = { 1, 1, K_FILE, p_sw, 1, 0, 0 };

__attribute__((noreturn)) static void eraerr(void)
{
    int e = dos_error();
    restudir();
    cerror_ext(e == 18 ? 2 : e, NULL);
}

static void fcb_to_ascz(const uint8_t *f, char *d)
{
    for (int i = 0; i < 8; i++) if (f[i] != ' ') *d++ = f[i];
    if (f[8] != ' ') {
        *d++ = '.';
        for (int i = 8; i < 11; i++) if (f[i] != ' ') *d++ = f[i];
    }
    *d = 0;
}

/* read a Y/N answer line (INT 21h 0C0Ah); -1 = empty line */
static int yn_line(void)
{
    uint8_t buf[COMBUFLEN + 4];
    buf[0] = COMBUFLEN;
    buf[1] = 0;
    dos_bufinput_flush(buf);
    if (!buf[1]) return -1;
    buf[2 + buf[1]] = '\r';
    const char *p = scanoff((const char *)buf + 2);
    return dos_yesno((uint8_t)*p);
}

void c_del(void)
{
    const char *s = (const char *)TAIL + 1;
    int np = 0, slashp = 0;
    struct pres r;
    for (;;) {
        int e = parse_with_msg(&s, &pb_erase, &np, &r);
        if (e == PT_EOL) break;
        if (r.type == PT_SWITCH) {
            if (slashp) cerror_parse(P_TOOMANY, err_sub);
            slashp = 1;
            continue;
        }
        if (r.text[1] == ':' && r.text[2] == 0) cerror_ext(2, NULL);
        move_to_srcbuf(r.text);
    }
    int zf;
    if (pathcrunch(0, &zf)) {
        if (msg_numb) cerror_ext(msg_numb, NULL);
        if (destisdir) cerror_ext(3, NULL);
    }
    if (slashp) {
        /* SLASHP_ERASE */
        static uint8_t destdir[64];
        char dest[16];
        build_dir_string();
        dos_setdta(destdir);
        REGS q = {0};
        q.r0 = 0x1100;
        q.r3 = (uint32_t)FCB;
        int21(&q);
        if ((q.r0 & 0xFF) == 0xFF) eraerr();
        for (;;) {
            dest[0] = dirchar;
            fcb_to_ascz(destdir + 1, dest + 1);
            for (;;) {
                crlf2();
                if (bwdbuf[3] == 0) bwdbuf[2] = 0;
                out(1, bwdbuf);
                out(1, dest);
                std_printf(M_DELYN);
                int a = yn_line();
                if (a == 0) break;
                if (a == 1) {
                    REGS d = {0};
                    d.r0 = 0x1300;
                    d.r3 = (uint32_t)destdir;
                    int21(&d);
                    if ((d.r0 & 0xFF) == 0xFF) eraerr();
                    break;
                }
            }
            memset(&q, 0, sizeof q);
            q.r0 = 0x1200;
            q.r3 = (uint32_t)FCB;
            int21(&q);
            if ((q.r0 & 0xFF) == 0xFF) break;
        }
        int e = dos_error();
        if (e != 18) cerror_ext(e, NULL);
        restudir();
        crlf2();
        return;
    }
    /* NOTEST2 */
    int all = 1;
    for (int i = 1; i <= 11; i++) if (FCB[i] != '?') all = 0;
    if (all) {
        for (;;) {
            std_printf(M_SUREMES);
            int a = yn_line();
            if (a < 0) continue;
            if (a == 0) return;
            crlf2();
            if (a == 1) break;
        }
    }
    REGS d = {0};
    d.r0 = 0x1300;
    d.r3 = (uint32_t)FCB;
    int21(&d);
    if ((d.r0 & 0xFF) == 0xFF) eraerr();
    restudir();
}

/* ---------------------------------------------------------------- REN -- */

static const struct pblock pb_rename = { 2, 2, K_FILE, 0, 1, 0, 0 };

void c_ren(void)
{
    const char *s = (const char *)TAIL + 1;
    int np = 0;
    struct pres r;
    parse_with_msg(&s, &pb_rename, &np, &r);
    move_to_srcbuf(r.text);
    const char *second = s;
    parse_with_msg(&s, &pb_rename, &np, &r);
    if (r.text[1] == ':') cerror_parse(P_BADPARM, NULL);     /* the new name may not have a drive */
    const char *t = second;
    while (*t == ' ' || *t == '\t') t++;
    dos_fcb_parse(&t, FCB2, 0x01);
    int one_char_val = (uint8_t)*t;
    parse_check_eol(&s, &pb_rename, &np);
    int zf;
    int cf = pathcrunch(0, &zf);
    if (!cf && zf) cerror_msg(M_BADCPMES);
    if (cf) {
        if (msg_numb) cerror_ext(msg_numb, NULL);
        if (destisdir) cerror_ext(3, NULL);
    }
    if (pathchr(one_char_val)) cerror_msg(M_INORNOT);
    REGS q = {0};
    q.r0 = 0x1700;
    q.r3 = (uint32_t)FCB;
    int21(&q);
    int err = 0;
    if ((q.r0 & 0xFF) == 0xFF) err = dos_error();
    restudir();
    if (!err) return;
    if (err == 2 || err == 5) cerror_msg(M_RENERR);
    cerror_ext(err, NULL);
}

/* --------------------------------------------------------------- TYPE -- */

static const struct pblock pb_mrdir = { 1, 1, K_FILE, 0, 1, 0, 0 };

void c_type(void)
{
    const char *s = (const char *)TAIL + 1;
    int np = 0;
    struct pres r;
    parse_with_msg(&s, &pb_mrdir, &np, &r);
    move_to_srcbuf(r.text);
    parse_check_eol(&s, &pb_mrdir, &np);
    setpath(srcbuf);
    if (destinfo & 2) cerror_msg(M_INORNOT);
    int h = dos_open(srcbuf, 0);
    if (h < 0) cerror_ext(dos_error(), srcbuf);
    static uint8_t buf[512];
    for (;;) {
        int n = dos_read(h, buf, sizeof buf);
        if (n < 0) {
            int e = dos_error();
            dos_close(h);
            cerror_ext(e, srcbuf);
        }
        if (n == 0) break;
        int z = 0;
        uint8_t *p = memchr(buf, 0x1A, n);
        if (p) {
            n = p - buf;
            z = 1;
        }
        int w = dos_write(1, buf, n);
        if (w != n) {
            dos_close(h);
            int info = dos_ioctl_info(1);
            if (info >= 0 && (info & 0x80)) return;
            if (pipeflag) {
                pipeoff();
                cerror_msg(M_PIPEEMES);
            }
            cerror_msg(M_NOSPACE);
        }
        if (z) break;
    }
    dos_close(h);
}
