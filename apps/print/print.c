/*
 * PRINT - ARM-DOS re-creation of the MS-DOS 4.00 background print spooler,
 * the transient part (CMD/PRINT/PRINT_T.ASM of the MS-DOS 4.0 source, MIT
 * licence, (C) Microsoft Corp.).  The resident part is res.c.
 *
 *   PRINT [/D:device] [/B:size] [/U:ticks] [/M:ticks] [/S:ticks] [/Q:qsize]
 *         [/T] [[d:][path]filename[ ...]] [/C] [/P]
 *
 * The first run asks for the list device (unless /D: gave one), installs the
 * resident part ("Resident part of PRINT installed") and stays resident;
 * later runs talk to it through INT 2Fh AH=01h.  File names are made
 * absolute, checked (an unopenable file gives DOS's error on STDERR), and
 * submitted; a /C or /P right after a name applies to it; wildcards submit
 * every match.  Every run ends with the queue listing.
 */
#include "u4.h"
#include "res.h"

/* room behind the stack for the largest queue, buffer and worker stack
 * while the transient part runs (r_keep() moves them down) */
unsigned u4_heap_bytes = MAX_QUEUE * MAXFILELEN + 1 + MAX_BUF + 8 + WSTACK + 16;

static int do21(struct armregs *r) { return u4_int21(r); }

static vect_t getvect(int n)
{
    struct armregs r;
    u4_clr(&r); r.r0 = 0x3500 | n; do21(&r);
    return (vect_t)r.r1;
}

static void setvect(int n, vect_t h)
{
    struct armregs r;
    u4_clr(&r); r.r0 = 0x2500 | n; r.r3 = (uint32_t)h; do21(&r);
}

/* ====================================================== transient part */

#define CLASS_B 0x0B
#define CLASS_C 0x0C
#define DOS_ERROR 1
#define PARSE_ERROR 2

static uint8_t pinst, canflag, ambig, devspec, qfullmes, in_int_23;
static char namebuf[MAXFILELEN + 16];
static char *nulptr, *fnamptr;
static uint8_t searchbuf[64];
static vect_t hardch, i28vec, i2fvec, i17vec, i14vec, i1cvec;
static const char *msg_start;           /* MSG_PTR: where the last parse began */
static struct u4_pstate pst;
static char *cmdline;

static char d_sw[] = "/D\0", b_sw[] = "/B\0", q_sw[] = "/Q\0", s_sw[] = "/S\0",
            u_sw[] = "/U\0", m_sw[] = "/M\0", t_sw[] = "/T\0", c_sw[] = "/C\0", p_sw[] = "/P\0";
static const struct u4_range r_buf = { MIN_BUF, MAX_BUF }, r_que = { 4, MAX_QUEUE },
                             r_255 = { 1, 255 };
static const struct u4_ctl pos1 = { P_FILE | P_REPEAT | P_OPTIONAL, P_CAP_FILE, 0, 0, 0 };
static const struct u4_ctl sw1 = { P_SIMPLE | P_OPTIONAL, P_CAP_FILE, d_sw, 0, 0 };
static const struct u4_ctl sw2 = { P_NUM | P_OPTIONAL, 0, b_sw, 1, &r_buf };
static const struct u4_ctl sw3 = { P_NUM | P_OPTIONAL, 0, q_sw, 1, &r_que };
static const struct u4_ctl sw4 = { P_NUM | P_OPTIONAL, 0, s_sw, 1, &r_255 };
static const struct u4_ctl sw5 = { P_NUM | P_OPTIONAL, 0, u_sw, 1, &r_255 };
static const struct u4_ctl sw6 = { P_NUM | P_OPTIONAL, 0, m_sw, 1, &r_255 };
static const struct u4_ctl sw7 = { P_NUM | P_OPTIONAL, 0, t_sw, 0, 0 };
static const struct u4_ctl sw8 = { P_NUM | P_REPEAT | P_OPTIONAL, 0, c_sw, 0, 0 };
static const struct u4_ctl sw9 = { P_NUM | P_REPEAT | P_OPTIONAL, 0, p_sw, 0, 0 };
static const struct u4_ctl *const pos_tab[] = { &pos1 };
static const struct u4_ctl *const sw_tab[] = { &sw1, &sw2, &sw3, &sw4, &sw5, &sw6, &sw7, &sw8, &sw9 };
static const struct u4_parms parms = { 0, 1, pos_tab, 9, sw_tab, 0, 0, 0, 0 };

/* the messages of PRINT.SKL (resolved against USA-MS.MSG) */
static void dispmsg(int cls, int num);
static char *param_text;

static const char *class_b_text(int n)
{
    switch (n) {
    case 1: return "Incorrect DOS version\r\n";
    case 2: return "\r\n";
    case 14: return "Resident part of PRINT installed\r\n";
    case 13: return "List output is not assigned to a device\r\n";
    case 15: return "Cannot use PRINT - Use NET PRINT\r\n";
    case 17: return "PRINT queue is full\r\n";
    case 18: return "PRINT queue is empty\r\n";
    case 19: return "Access denied\r\n";
    case 20: return "Invalid drive specification\r\n";
    case 21: return "Errors on list device indicate that it\r\nmay be off-line. Please check it.\r\n";
    }
    return "";
}

/* DispMsg: class B/C/D to STDOUT; DOS and parse errors to STDERR */
static void dispmsg(int cls, int num)
{
    if (cls == CLASS_C) {
        switch (num) {
        case 22: u4_msg1(STDOUT, "\r\n\n  %1 is currently being printed\r\n", namebuf); break;
        case 23: u4_msg1(STDOUT, "  %1 is in queue\r\n", namebuf); break;
        case 24: u4_class_msg(STDOUT, "File not found", namebuf); break;
        case 25: u4_puts(STDOUT, "Pathname too long - "); u4_puts(STDOUT, namebuf); u4_puts(STDOUT, "\r\n"); break;
        case 26: u4_puts(STDOUT, "File not in PRINT queue - "); u4_puts(STDOUT, namebuf); u4_puts(STDOUT, "\r\n"); break;
        }
    } else if (cls == DOS_ERROR) {
        u4_exterr(STDERR, num, namebuf);
    } else if (cls == PARSE_ERROR) {
        u4_parse_err(STDERR, num, param_text);
    } else {
        u4_puts(STDOUT, class_b_text(num));
    }
}

static int int2f_call(struct armregs *r) { return u4_int2f(r); }

/* IntWhileBusy */
static int print_call(unsigned ax, struct armregs *r)
{
    for (;;) {
        r->r0 = ax;
        int cf = int2f_call(r);
        if (!cf) return 0;
        if ((r->r0 & 0xFFFF) != E_BUSY) return 1;
    }
}

static void restore_ints(void)
{
    setvect(0x28, i28vec);
    setvect(0x2F, i2fvec);
    setvect(0x17, i17vec);
    setvect(0x14, i14vec);
    setvect(0x1C, i1cvec);
}

static void t_int24(struct armregs *f)
{
    if (hardch) hardch(f); else f->r0 = (f->r0 & ~0xFFu) | 3;
    if (AL(f) == 2) {
        in_int_23++;
        if (pinst != 2) restore_ints();
    }
}

static void t_int23(struct armregs *f)
{
    if (!in_int_23) {
        in_int_23++;
        if (pinst != 2) restore_ints();
        else { struct armregs r; u4_clr(&r); print_call(0x0105, &r); }
    }
    f->cpsr |= ARM_CPSR_C;              /* abort */
}

static void set_ints(void) { setvect(0x23, t_int23); setvect(0x24, t_int24); }

static void save_vectors(void)
{
    i28vec = getvect(0x28);
    i2fvec = getvect(0x2F);
    i17vec = getvect(0x17);
    i14vec = getvect(0x14);
    i1cvec = getvect(0x1C);
}

static const char *const hit17[] = { "PRN     ", "LPT1    ", "LPT2    ", "LPT3    " };
static const uint8_t hit17n[] = { 0, 0, 1, 2 };
static const char *const hit14[] = { "AUX     ", "COM1    ", "COM2    " };
static const uint8_t hit14n[] = { 0, 0, 1 };

/* SETDEV: find the list device in the device chain */
static int setdev(void)
{
    struct armregs r;
    u4_clr(&r); r.r0 = 0x5200; do21(&r);
    struct devhdr *d = (struct devhdr *)(r.r1 + 0x24);
    for (; d && d != (struct devhdr *)0xFFFFFFFFu; d = d->next)
        if ((d->attr & DEVA_CHAR) && !memcmp(d->name, r_listname, 8)) break;
    if (!d || d == (struct devhdr *)0xFFFFFFFFu) return -1;
    r_listdev = d;
    r_flag17_14 = 0;
    for (int i = 0; i < 4; i++)
        if (!memcmp(r_listname, hit17[i], 8)) { r_int17num = hit17n[i]; r_flag17_14 = 1; }
    if (!r_flag17_14)
        for (int i = 0; i < 3; i++)
            if (!memcmp(r_listname, hit14[i], 8)) { r_int14num = hit14n[i]; r_flag17_14 = 2; }
    return 0;
}


/* Set_Buffer + MoveTrans: ask for the device, lay out the queue and the
 * r_buffer, hook the interrupts - the resident part is live from here */
static void set_buffer(void)
{
    if (!devspec) {
        static uint8_t tok[12];
        tok[0] = 9; tok[1] = 0;
        u4_puts(STDOUT, "Name of list device [PRN]: ");
        struct armregs r;
        u4_clr(&r); r.r0 = 0x0A00; r.r3 = (uint32_t)tok; do21(&r);
        u4_puts(STDOUT, "\r\n");
        int n = tok[1];
        if (n) {
            char *s = (char *)tok + 2;
            s[n] = 0;
            u4_strupr(s);                /* INT 21h AX=6522h */
            if (s[n - 1] == ':') n--;
            if (n > 8) n = 8;
            memcpy(r_listname, s, n);     /* (4.0 does not blank the rest of "PRN") */
        }
    }
    r_filequeue = (char *)u4_heap;
    r_filequeue[0] = 0;
    r_queuetail = r_filequeue;
    r_endqueue = r_filequeue + MAXFILELEN * r_queuelen;
    r_buffer = (uint8_t *)r_endqueue + 1;
    r_endptr = r_nxtchr = r_buffer + r_blksiz;
    r_wstack = (uint8_t *)(((uint32_t)r_endptr + 7) & ~7u);

    if (setdev() < 0) {
        u4_puts(STDOUT, class_b_text(13));
        u4_exit(0xFF);
    }
    r_next28 = getvect(0x28); setvect(0x28, r_int28);
    r_next2f = getvect(0x2F); setvect(0x2F, r_int2f);
    r_next17 = getvect(0x17); setvect(0x17, r_int17);
    r_next14 = getvect(0x14); setvect(0x14, r_int14);
    struct armregs r;
    u4_clr(&r); r.r0 = 0x3400; do21(&r);
    r_indos = (volatile uint8_t *)r.r1;
    r_next1c = getvect(0x1C);
    u4_clr(&r); r.r0 = 0xB800; u4_int2f(&r);
    if ((r.r0 & 0xFF) == 0 || !(r.r1 & 0xC4)) setvect(0x1C, r_int1c);
    u4_puts(STDOUT, class_b_text(14));
    set_ints();
    pinst = 1;
}

/* Parse_Input: one operand; returns the parser code (0, -1 EOL, error) */
static int parse_input(struct u4_result *res)
{
    msg_start = pst.si;
    return u4_parse(&parms, &pst, res);
}

static void set_param_text(void)
{
    static char buf[130];
    int n = pst.si - msg_start;
    if (n > 128) n = 128;
    memcpy(buf, msg_start, n);
    buf[n] = 0;
    param_text = buf;
}

static int map_parse_rc(int rc)
{
    if (rc > 9) rc = 9;
    static const uint8_t map[10] = { 0, 9, 9, 3, 9, 9, 6, 9, 9, 9 };
    return map[rc];
}

/* GetAbsN / GetAbsN2 / CopyName */
static void copyname(void) { strcpy(fnamptr, (char *)searchbuf + 0x1E); }

static int getabsn(void)
{
    struct armregs r;
    u4_clr(&r); r.r0 = 0x1A00; r.r3 = (uint32_t)searchbuf; do21(&r);
    u4_clr(&r); r.r0 = 0x4E00; r.r2 = 0; r.r3 = (uint32_t)namebuf;
    if (do21(&r)) return -1;
    char *p = nulptr;
    while (*p != r_pchar) p--;
    fnamptr = p + 1;
    copyname();
    return 0;
}

static int getabsn2(void)
{
    struct armregs r;
    u4_clr(&r); r.r0 = 0x1A00; r.r3 = (uint32_t)searchbuf; do21(&r);
    u4_clr(&r); r.r0 = 0x4F00;
    if (do21(&r)) return -1;
    copyname();
    return 0;
}

/* Submit_File: a file name from the command line.  Returns 0, or an error
 * message (class << 8 | number) that has to be shown. */
static int submit_file(const char *fn)
{
    if (!pinst) set_buffer();
    ambig = 0;
    char *di = namebuf;
    const char *si = fn;
    int drive;
    struct armregs r;
    if (si[1] != ':') {
        u4_clr(&r); r.r0 = 0x1900; do21(&r);
        drive = (r.r0 & 0xFF) + 1;
        *di++ = 'A' + drive - 1;
        *di++ = ':';
    } else {
        int d = (uint8_t)si[0] - '@';
        if (d <= 0) { dispmsg(CLASS_B, 20); return 0; }
        drive = d;
        *di++ = *si++;
        *di++ = *si++;
    }
    if (*si != r_pchar) {
        *di++ = r_pchar;
        u4_clr(&r); r.r0 = 0x4700; r.r3 = drive; r.r4 = (uint32_t)di;
        if (do21(&r)) { dispmsg(CLASS_B, 20); return 0; }
        char *start = di;
        while (*di) di++;
        if (di != start) *di++ = r_pchar;
    } else {
        u4_clr(&r); r.r0 = 0x4700; r.r3 = drive; r.r4 = (uint32_t)di;
        if (do21(&r)) { dispmsg(CLASS_B, 20); return 0; }
    }
    int left = MAXFILELEN - (di - namebuf);
    if (left <= 0) left = 1;
    for (;;) {
        char c = *si++;
        *di++ = c;
        if (c == '*' || c == '?') ambig = 1;
        if (!c) break;
        if (--left == 0) {
            di--; *di = 0;
            nulptr = di;
            { dispmsg(CLASS_C, 25); return 0; }
        }
    }
    di--;
    nulptr = di;

    /* a /C or /P right after the name applies to it */
    struct u4_pstate save = pst;
    const char *save_msg = msg_start;
    struct u4_result res;
    int rc = parse_input(&res);
    int consumed = 0;
    if (rc == 0 && res.synonym == c_sw) { canflag = 1; consumed = 1; }
    else if (rc == 0 && res.synonym == p_sw) { canflag = 0; consumed = 1; }
    if (!consumed) { pst = save; msg_start = save_msg; }

    if (canflag) {
        u4_clr(&r); r.r3 = (uint32_t)namebuf;
        if (print_call(0x0102, &r)) { dispmsg(CLASS_C, 26); return 0; }
        return 0;
    }
    if (ambig && getabsn() < 0) {
        dispmsg(CLASS_C, 24);
        return 0;
    }
    for (;;) {
        int err = 0;
        /* a local drive: work on the true name (INT 21h AH=60h) */
        u4_clr(&r); r.r0 = 0x4409; r.r1 = (uint8_t)namebuf[0] - 0x40;
        if (!do21(&r) && !(r.r3 & 0x1200)) {
            static char tok[MAXFILELEN + 16];
            u4_clr(&r); r.r0 = 0x6000; r.r4 = (uint32_t)namebuf; r.r5 = (uint32_t)tok;
            if (!do21(&r)) memcpy(namebuf, tok, sizeof namebuf);
        }
        u4_clr(&r); r.r0 = 0x6C00; r.r1 = 0x2000; r.r2 = 0; r.r3 = 0x0001;   /* (4.0: 0101h, "ignore code page") */
        r.r4 = (uint32_t)namebuf; r.r5 = 0xFFFF;
        if (do21(&r)) {
            u4_clr(&r); r.r0 = 0x5900; r.r1 = 0; do21(&r);
            err = (DOS_ERROR << 8) | (r.r0 & 0xFF);
        } else {
            u4_close(r.r0 & 0xFFFF);
            static uint8_t subpack[5];
            uint32_t p = (uint32_t)namebuf;
            subpack[0] = 0;
            subpack[1] = p; subpack[2] = p >> 8; subpack[3] = p >> 16; subpack[4] = p >> 24;
            u4_clr(&r); r.r3 = (uint32_t)subpack;
            int cf = print_call(0x0101, &r);
            if (cf || (r.r0 & 0xFFFF) == E_QFULL) {
                if (!qfullmes) { qfullmes = 1; err = (CLASS_B << 8) | 17; }
            } else qfullmes = 0;
        }
        if (err) dispmsg(err >> 8, err & 0xFF);
        if (!ambig || getabsn2() < 0) break;
    }
    return 0;
}

static void closestd(void) { for (int h = 0; h < 5; h++) u4_close(h); }

int main(void)
{
    /* free the environment: the resident part has no use for it */
    if (_armdos_psp->envseg) {
        struct armregs r;
        u4_clr(&r); r.r0 = 0x4900; r.r8 = _armdos_psp->envseg; do21(&r);
    }
    r_my_psp = (uint32_t)_armdos_psp >> 4;
    int err = 0;
    if (!u4_version_ok()) {
        err = (0xFF << 8) | 1;
    } else {
        struct armregs r;
        u4_clr(&r); r.r0 = 0x0100; u4_int2f(&r);
        int al = r.r0 & 0xFF;
        if (al == 0) save_vectors();
        else if (al == 1) err = (CLASS_B << 8) | 15;
        else {
            pinst = 2;
            d_sw[0] = b_sw[0] = q_sw[0] = s_sw[0] = u_sw[0] = m_sw[0] = ' ';
        }
    }
    if (!err) {
        hardch = getvect(0x24);
        set_ints();
        struct armregs r;
        u4_clr(&r); r.r0 = 0x3700; do21(&r);
        if ((r.r3 & 0xFF) == '-') r_pchar = '/';
    }
    cmdline = u4_cmdline();
    pst.si = cmdline;
    pst.ordinal = 0;
    while (!err) {
        struct u4_result res;
        int rc = parse_input(&res);
        if (rc == P_RC_EOL) break;
        if (rc != 0) {
            set_param_text();
            err = (PARSE_ERROR << 8) | map_parse_rc(rc);
            break;
        }
        if (res.type == R_FILE) {
            err = submit_file(res.str);
            continue;
        }
        const char *syn = res.synonym;
        int bad = 0;
        if (syn == d_sw) {
            int n = strlen(res.str);
            if (pinst || !n) bad = 1;
            else {
                memset(r_listname, ' ', 3);
                if (res.str[n - 1] == ':') n--;
                if (n > 8) n = 8;
                memcpy(r_listname, res.str, n);
                devspec = 1;
            }
            d_sw[0] = ' ';
        } else if (syn == b_sw) {
            if (pinst) bad = 1; else r_blksiz = res.value;
            b_sw[0] = ' ';
        } else if (syn == q_sw) {
            if (pinst) bad = 1; else r_queuelen = res.value;
            q_sw[0] = ' ';
        } else if (syn == s_sw) {
            if (pinst) bad = 1; else r_timeslice = r_slicecnt = res.value;
            s_sw[0] = ' ';
        } else if (syn == u_sw) {
            if (pinst) bad = 1; else r_busytick = res.value;
            u_sw[0] = ' ';
        } else if (syn == m_sw) {
            if (pinst) bad = 1; else r_maxtick = res.value;
            m_sw[0] = ' ';
        } else if (syn == t_sw) {
            if (!pinst) set_buffer();
            struct armregs r;
            u4_clr(&r); print_call(0x0103, &r);
        } else if (syn == c_sw) {
            if (pinst) canflag = 1; else set_buffer();
        } else if (syn == p_sw) {
            if (pinst) canflag = 0; else set_buffer();
        }
        if (bad) {
            set_param_text();
            err = (PARSE_ERROR << 8) | 9;
            if (pinst) dispmsg(PARSE_ERROR, 9);
        }
    }
    if (err) {
        dispmsg(err >> 8, err & 0xFF);
        if (pinst == 1) restore_ints();  /* (4.0 leaves them hooked) */
        u4_exit(0);
    }
    if (!pinst) set_buffer();
    struct armregs r;
    u4_clr(&r);
    print_call(0x0104, &r);
    if ((r.r3 & 0xFFFF) >= ERRCNT1) dispmsg(CLASS_B, 21);
    const char *q = (const char *)r.r4;
    if (*q) {
        int which = 22;
        while (*q) {
            strcpy(namebuf, q);
            dispmsg(CLASS_C, which);
            which = 23;
            q += MAXFILELEN;
        }
    } else dispmsg(CLASS_B, 18);
    u4_clr(&r);
    print_call(0x0105, &r);
    if (pinst == 1) {
        closestd();
        r_keep();
    }
    u4_exit(0);
}

