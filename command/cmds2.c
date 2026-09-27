/*
 * cmds2.c - CD, MD, RD (TENV2.ASM), VER, CLS (TCMD2A.ASM), ECHO, BREAK,
 * VERIFY (TUCODE.ASM), DATE, TIME (TPIPE.ASM), CTTY, CHCP, TRUENAME
 * (TCMD2B.ASM).
 */
#include "cmd.h"

extern void jump_lodcom(void) __attribute__((noreturn));

static const struct pblock pb_chdir = { 0, 1, K_FILE_OR_DRIVE, 0, 1, 0, 0 };
static const struct pblock pb_mrdir = { 1, 1, K_FILE, 0, 1, 0, 0 };

/* ----------------------------------------------------------------- CD -- */

void c_cd(void)
{
    const char *s = (const char *)TAIL + 1;
    int np = 0;
    struct pres r;
    int e = parse_with_msg(&s, &pb_chdir, &np, &r);
    if (e == PT_EOL || r.type == PT_DRIVE) {
        if (e != PT_EOL) parse_check_eol(&s, &pb_chdir, &np);
        build_dir_string();
        out(1, bwdbuf);
        crlf2();
        return;
    }
    xstrlcpy(srcbuf, r.text, sizeof srcbuf - 2);
    strcat(srcbuf, "\r");
    parse_check_eol(&s, &pb_chdir, &np);
    setpath(srcbuf);
    if (destinfo & 2) cerror_msg(M_BADCD);
    if (dos_chdir(srcbuf)) {
        int x = dos_error();
        if (x == 3) cerror_msg(M_BADCD);
        cerror_ext(x, srcbuf);
    }
}

/* ------------------------------------------------------------- MD/RD -- */

static char srcxname[COMBUFLEN];

static void setrmmk(void)
{
    const char *s = (const char *)TAIL + 1;
    int np = 0;
    struct pres r;
    parse_with_msg(&s, &pb_mrdir, &np, &r);
    xstrlcpy(srcxname, r.text, sizeof srcxname);
    parse_check_eol(&s, &pb_mrdir, &np);
}

void c_md(void)
{
    setrmmk();
    if (!dos_mkdir(srcxname)) return;
    int e = dos_error();
    if (e == 3) cerror_msg(M_BADMKD);
    if (e == 5) {
        static uint8_t dta[64];
        dos_setdta(dta);
        if (!dos_findfirst(srcxname, 0x10) && (dta[21] & 0x10)) cerror_msg(M_MDEXISTS);
        cerror_msg(M_BADMKD);
    }
    cerror_ext(e, srcxname);
}

void c_rd(void)
{
    setrmmk();
    if (!dos_rmdir(srcxname)) return;
    int e = dos_error();
    if (e == 3 || e == 5) cerror_msg(M_BADRMD);
    cerror_ext(e, srcxname);
}

/* ---------------------------------------------------------------- VER -- */

void c_ver(void)
{
    crlf2();
    print_version();
    crlf2();
    std_printf(1901);           /* ARM-DOS: the owner's copyright lines */
    std_printf(1902);
}

/* ---------------------------------------------------------------- CLS -- */

static void ansi_cls(void)
{
    static const char s[] = "\x1B[2J";
    for (int i = 0; i < 4; i++) {
        REGS r = {0};
        r.r0 = 0x0600;
        r.r3 = (uint8_t)s[i];
        int21(&r);
    }
}

void c_cls(void)
{
    REGS r = {0};
    int rows = 25, cols = 80;
    r.r0 = 0x1A00;
    intr(0x2F, &r);
    if ((r.r0 & 0xFF) == 0xFF) {
        ansi_cls();
        return;
    }
    uint8_t info[20];
    memset(info, 0, sizeof info);
    info[2] = 14;
    memset(&r, 0, sizeof r);
    r.r0 = 0x440C;
    r.r1 = 1;
    r.r2 = 0x037F;
    r.r3 = (uint32_t)info;
    if (!int21(&r)) {
        rows = info[16] | (info[17] << 8);
        cols = info[14] | (info[15] << 8);
    } else {
        int d = dos_ioctl_info(1);
        if (d < 0 || !(d & 0x80) || !(d & 0x10)) {
            ansi_cls();
            return;
        }
        memset(&r, 0, sizeof r);
        r.r0 = 0x0F00;
        intr(0x10, &r);
        int mode = r.r0 & 0xFF;
        if (mode > 3 && mode != 7) {
            memset(&r, 0, sizeof r);
            r.r0 = mode;
            intr(0x10, &r);
            return;
        }
        cols = (r.r0 >> 8) & 0xFF;
        rows = 25;
    }
    /* reg_cls: border 0, scroll the whole screen with attribute 07h, home */
    memset(&r, 0, sizeof r);
    r.r0 = 0x0B00;
    r.r1 = 0;
    intr(0x10, &r);
    memset(&r, 0, sizeof r);
    r.r0 = 0x0600;
    r.r1 = 0x0700;
    r.r2 = 0;
    r.r3 = ((rows - 1) << 8) | (cols - 1);
    intr(0x10, &r);
    memset(&r, 0, sizeof r);
    r.r0 = 0x0200;
    r.r1 = 0;
    r.r3 = 0;
    intr(0x10, &r);
}

/* ------------------------------------------------- ECHO, BREAK, VERIFY -- */

static const struct pblock pb_onoff = { 0, 1, K_ONOFF, 0, 1, 0, 0 };

/* ON_OFF: 0 = ON, 1 = OFF, -1 = nothing on the line, -2 = something else */
static int on_off(void)
{
    const char *s = (const char *)TAIL + 1;
    int np = 0;
    struct pres r;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '=') s++;
    int e = sysparse(&s, &pb_onoff, &np, &r);
    if (e == PT_EOL) return -1;
    if (e) return -2;
    int v = r.num ? 1 : 0;
    e = sysparse(&s, &pb_onoff, &np, &r);
    if (e != PT_EOL) return -2;
    return v;
}

static void pyn(int n, int on)
{
    msgout(1, n, msg(on ? M_ONMES : M_OFFMES), 0, 0);
}

void c_echo(void)
{
    int v = on_off();
    if (v == 0) {
        echoflag |= 1;
        return;
    }
    if (v == 1) {
        echoflag &= ~1;
        return;
    }
    if (v == -1) {
        pyn(M_ECHOMES, echoflag & 1);
        return;
    }
    /* text: from 82h (one character after ECHO) up to the CR */
    const char *p = (const char *)TAIL + 2;
    const char *e = p;
    if (TAIL[0] == 0) e = p = (const char *)TAIL + 1;
    while (*e != '\r') e++;
    outn(1, p, e - p);
    crlf2();
}

void c_break(void)
{
    int v = on_off();
    REGS r = {0};
    if (v >= 0) {
        r.r0 = 0x3301;
        r.r3 = v == 0 ? 1 : 0;
        int21(&r);
        return;
    }
    if (v == -2) cerror_msg(M_BADONOFF);
    r.r0 = 0x3300;
    int21(&r);
    pyn(M_CTRLCMES, r.r3 & 0xFF);
}

void c_verify(void)
{
    int v = on_off();
    REGS r = {0};
    if (v >= 0) {
        r.r0 = 0x2E00 | (v == 0 ? 1 : 0);
        int21(&r);
        return;
    }
    if (v == -2) cerror_msg(M_BADONOFF);
    r.r0 = 0x5400;
    int21(&r);
    pyn(M_VERIMES, r.r0 & 0xFF);
}

/* ---------------------------------------------------------- DATE/TIME -- */

static const struct pblock pb_date = { 0, 1, K_DATE, 0, 0, 0, 0 };
static const struct pblock pb_time = { 0, 1, K_TIME, 0, 0, 0, 0 };

static void read_line(void)
{
    combuf[0] = COMBUFLEN;
    init_special = 1;
    dos_bufinput(combuf);
    init_special = 0;
    crlf2();
}

static int getdat(struct pres *r)
{
    int fmt = country[0] | (country[1] << 8);
    msgout(1, M_NEWDAT, msg(fmt == 0 ? M_USADAT : fmt == 1 ? M_EURDAT : M_JAPDAT), 0, 0);
    read_line();
    const char *s = (const char *)combuf + 2;
    int np = 0;
    int e = sysparse(&s, &pb_date, &np, r);
    if (e == 0) {
        int e2 = sysparse(&s, &pb_date, &np, r + 1);
        if (e2 != PT_EOL) return 99;
    }
    return e;
}

static void set_date_loop(int e, struct pres *r)
{
    for (;;) {
        if (e == PT_EOL) return;
        if (e == 0 && dos_setdate(r->y, r->m, r->d) == 0) return;
        crlf2();
        std_printf(M_BADDAT);
        e = getdat(r);
    }
}

void c_date(void)
{
    const char *s = (const char *)TAIL + 1;
    int np = 0;
    struct pres r[2];
    int e = sysparse(&s, &pb_date, &np, &r[0]);
    if (e == PT_EOL) {
        int y, m, d, wd;
        char day[4], date[16];
        dos_getdate(&y, &m, &d, &wd);
        memcpy(day, msg(M_WEEKTAB) + 3 * wd, 3);
        day[3] = 0;
        fmt_date(date, y, m, d, 1);
        msgout(1, M_CURDAT, day, date, 0);
        e = getdat(r);
    } else if (e == 0) {
        if (sysparse(&s, &pb_date, &np, &r[1]) != PT_EOL) e = 99;
    }
    set_date_loop(e, r);
}

static int gettim(struct pres *r)
{
    std_printf(M_NEWTIM);
    read_line();
    const char *s = (const char *)combuf + 2;
    int np = 0;
    int e = sysparse(&s, &pb_time, &np, r);
    if (e == 0) {
        int e2 = sysparse(&s, &pb_time, &np, r + 1);
        if (e2 != PT_EOL) return 99;
    }
    return e;
}

void c_time(void)
{
    const char *s = (const char *)TAIL + 1;
    int np = 0;
    struct pres r[2];
    int e = sysparse(&s, &pb_time, &np, &r[0]);
    if (e == PT_EOL) {
        int h, m, sec, hs;
        char t[16], tr[16];
        dos_gettime(&h, &m, &sec, &hs);
        fmt_time(t, h, m, sec, hs, 4, 1);
        int l = strlen(t), i = 0;
        while (l + i < 12) tr[i++] = ' ';
        strcpy(tr + i, t);
        msgout(1, M_CURTIM, tr, 0, 0);
        e = gettim(r);
    } else if (e == 0) {
        if (sysparse(&s, &pb_time, &np, &r[1]) != PT_EOL) e = 99;
    }
    for (;;) {
        if (e == PT_EOL) return;
        if (e == 0 && dos_settime(r->h, r->mi, r->s, r->hs) == 0) return;
        crlf2();
        std_printf(M_BADTIM);
        e = gettim(r);
    }
}

/* DATINIT: the date and time prompts at start-up (no AUTOEXEC.BAT) */
void datinit(void)
{
    TAIL[0] = 0;
    TAIL[1] = '\r';
    combuf[0] = COMBUFLEN;
    combuf[1] = 0;
    combuf[2] = '\r';
    c_date();
    c_time();
}

/* --------------------------------------------------------------- CTTY -- */

static const struct pblock pb_ctty = { 1, 1, K_STRING, 0, 1, 0, 0 };

void c_ctty(void)
{
    const char *s = (const char *)TAIL + 1;
    int np = 0;
    struct pres r;
    int e = sysparse(&s, &pb_ctty, &np, &r);
    if (e != 0) goto bad;
    {
        struct pres r2;
        if (sysparse(&s, &pb_ctty, &np, &r2) != PT_EOL) goto bad;
    }
    int h = dos_open(r.text, 2);
    if (h < 0) goto bad;
    int info = dos_ioctl_info(h);
    if (info < 0 || !(info & 0x80)) {
        dos_close(h);
        goto bad;
    }
    if (dos_write(h, "\r\n", 2) < 0) {
        dos_close(h);
        goto bad;
    }
    dos_ioctl_set(h, (info | 3) & 0xFF);
    dos_close(0);
    dos_close(1);
    dos_close(2);
    dos_dup(h);
    dos_dup(h);
    dos_dup(h);
    dos_close(h);
    goto resret;
bad:
    std_printf(M_BADDEV);
resret:
    {
        uint8_t *j = (uint8_t *)mypsp->jftptr;
        io_save[0] = j[0];
        io_save[1] = j[1];
    }
    jump_lodcom();
}

/* --------------------------------------------------------------- CHCP -- */

static const struct pblock pb_chcp = { 0, 1, K_NUM, 0, 0, 100, 999 };

void c_chcp(void)
{
    const char *s = (const char *)TAIL + 1;
    int np = 0;
    struct pres r;
    int e = parse_with_msg(&s, &pb_chcp, &np, &r);
    char n[8];
    if (e == PT_EOL) {
        REGS g = {0};
        g.r0 = 0x6601;
        int21(&g);
        fmt_uint(n, g.r1 & 0xFFFF, 0, ' ');
        msgout(1, M_CPACTIVE, n, 0, 0);
        return;
    }
    unsigned cp = r.num;
    parse_check_eol(&s, &pb_chcp, &np);
    REGS m = {0};
    m.r0 = 0x1400;
    intr(0x2F, &m);
    if ((m.r0 & 0xFF) != 0xFF) cerror_msg(M_NLSFUNC);
    REGS g = {0};
    g.r0 = 0x6602;
    g.r1 = cp;
    if (!int21(&g)) return;
    fmt_uint(n, cp, 0, ' ');
    int err = g.r0 & 0xFFFF;
    int x = dos_error();
    if (err == 2) {
        if (x == 13) cerror_msg(M_INVCP);
        cerror_ext(2, NULL);
    }
    msgout(2, x == 65 ? M_CPNOTALL : M_CPNOTSET, n, 0, 0);
    tcommand();
}

/* ----------------------------------------------------------- TRUENAME -- */

void c_truename(void)
{
    const char *s = (const char *)TAIL + 1;
    int np = 0;
    struct pres r;
    char src[COMBUFLEN];
    int e = parse_with_msg(&s, &pb_chdir, &np, &r);
    if (e == PT_EOL) {
        strcpy(src, ".");
    } else if (r.type == PT_DRIVE) {
        src[0] = r.drive + 'A' - 1;
        strcpy(src + 1, ":.");
        parse_check_eol(&s, &pb_chdir, &np);
    } else {
        xstrlcpy(src, r.text, sizeof src);
        parse_check_eol(&s, &pb_chdir, &np);
    }
    char out_[COMBUFLEN + 16];
    if (dos_truename(src, out_)) cerror_ext(dos_error(), src);
    crlf2();
    printf_crlf(1, out_);
}
