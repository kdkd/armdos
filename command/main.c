/*
 * main.c - COMMAND.COM: initialisation (INIT.ASM), the command loop
 * (TCODE.ASM: TCOMMAND/COMMAND/GETCOM/DOCOM/DOCOM1), the internal command
 * table (TDATA.ASM COMTAB) and dispatch (TMISC1.ASM FNDCOM), CERROR.
 */
#include "cmd.h"

/* ----------------------------------------------------------- state -- */
/* (the resident state is R->..., see res.h and the macros in cmd.h) */
struct res *R;
uint8_t  combuf[COMBUFLEN + 4];
uint8_t  hdr[256];
uint8_t  parm1, parm2;
uint16_t comsw, arg1s, arg2s, argts;
int      pathpos_len;
uint8_t  switchar;
uint8_t  dirchar;
uint8_t  dirbuf[128];
static uint8_t chkdrv;
int bogus_iters;                        /* see contc_user and promptbat */

jmp_buf jb_tcommand;
enum { J_TCOMMAND = 1, J_LODCOM, J_DOCOM1, J_DOCOM, J_NOPIPEPROC, J_GETCOM, J_EXTEXEC };

void set_psp_handlers(void);
static void init(void);

/* INIT's parse errors: RPRINT of the bare parse message (no substitution) */
static void init_perr(int n, const char *unused)
{
    (void)unused;
    out(1, parse_msg(n));
    crlf(1);
}

/* ------------------------------------------------------- the jumps -- */

__attribute__((noreturn)) void tcommand(void)
{
    longjmp(jb_tcommand, J_TCOMMAND);
}

__attribute__((noreturn)) void jump_docom1(void) { longjmp(jb_tcommand, J_DOCOM1); }
__attribute__((noreturn)) void jump_docom(void) { longjmp(jb_tcommand, J_DOCOM); }
__attribute__((noreturn)) void jump_nopipeproc(void) { longjmp(jb_tcommand, J_NOPIPEPROC); }
__attribute__((noreturn)) void jump_lodcom(void) { longjmp(jb_tcommand, J_LODCOM); }

__attribute__((noreturn)) void cerror_msg(int n)
{
    std_eprintf(n);
    tcommand();
}

__attribute__((noreturn)) void cerror_ext(int err, const char *sub)
{
    ext_error_out(err, sub);
    tcommand();
}

__attribute__((noreturn)) void cerror_parse(int n, const char *sub)
{
    parse_error_out(n, sub);
    tcommand();
}

/* $EXIT (TCMD2B.ASM): the resident part gives the parent back its
 * terminate address and PID */
__attribute__((noreturn)) void cmd_exit(int code)
{
    R->exit(code);
    for (;;) ;
}

void c_exit(void)
{
    if (permcom) return;        /* a permanent COMMAND's parent is itself */
    cmd_exit(retcode);
}

void c_rem(void) { }

/* --------------------------------------------------- internal table -- */

#define fCheckDrive     1
#define fSwitchAllowed  2
#define SC (fSwitchAllowed | fCheckDrive)
#define S  fSwitchAllowed

struct comtab { const char *name; uint8_t flags; void (*fn)(void); };
static const struct comtab comtab[] = {
    { "DIR", SC, c_dir }, { "CALL", S, c_call }, { "CHCP", S, c_chcp },
    { "RENAME", SC, c_ren }, { "REN", SC, c_ren }, { "ERASE", SC, c_del },
    { "DEL", SC, c_del }, { "TYPE", SC, c_type }, { "REM", S, c_rem },
    { "COPY", SC, c_copy }, { "PAUSE", S, c_pause }, { "DATE", S, c_date },
    { "TIME", S, c_time }, { "VER", 0, c_ver }, { "VOL", SC, c_vol },
    { "CD", SC, c_cd }, { "CHDIR", SC, c_cd }, { "MD", SC, c_md },
    { "MKDIR", SC, c_md }, { "RD", SC, c_rd }, { "RMDIR", SC, c_rd },
    { "BREAK", S, c_break }, { "VERIFY", S, c_verify }, { "SET", S, c_set },
    { "PROMPT", S, c_prompt }, { "PATH", S, c_path }, { "EXIT", 0, c_exit },
    { "CTTY", SC, c_ctty }, { "ECHO", S, c_echo }, { "GOTO", S, c_goto },
    { "SHIFT", S, c_shift }, { "IF", S, c_if }, { "FOR", S, c_for },
    { "CLS", 0, c_cls }, { "TRUENAME", SC, c_truename },
    /* ARM-DOS's hidden commands (eggs.c) */
    { "MSD", S, e_msd }, { "WIN", S, e_win }, { "DELTREE", S, e_deltree },
    { "MEMMAKER", S, e_memmaker }, { "INTEL", S, e_intel }, { "XYZZY", S, e_xyzzy },
    { "PLUGH", S, e_plugh }, { "IDDQD", S, e_iddqd }, { "IDKFA", S, e_idkfa },
    { "HAL", S, e_hal }, { "ELIZA", S, e_eliza }, { "42", S, e_42 },
    { "SUDO", S, e_sudo }, { "LS", SC, e_ls }, { "UNAME", S, e_uname },
    { 0, 0, 0 }
};

static int singletest(void)
{
    return singlecom != 0 && singlecom < 0xEFFF;
}

/* the drive and name of the command, parsed like COMMAND does (INT 21h 2901h
 * into IDLEN) */
static uint8_t idlen[40];

/* LODCOM1: after an external program or an abort (COMMAND2.ASM) */
static void lodcom1(void)
{
    headfix();
    if (verval != -1) {
        REGS r = {0};
        r.r0 = 0x2E00 | (verval & 1);
        int21(&r);
        verval = -1;
    }
    if (singlecom == 0xFFFF) cmd_exit(0);      /* FATALRET2 */
}

/* CONTC's work once COMMAND has control again (see crit.c) */
static void contc_user(void)
{
    if (!singlecom) {
        REGS r = {0};
        r.r0 = 0x0D00;
        int21(&r);
    }
    if (batch && !singlecom) {
        if (askend()) {
            /* ClearBatch: free NEST batch segments, following the CALL
             * chain.  A batch file whose CALLee failed a GOTO is never
             * freed and still counts in NEST; DOS 4 then takes the IVT for
             * a batch segment and later says "Batch file missing" - we
             * just say it. */
            int ech = echoflag;
            bogus_iters = 0;
            do {
                if (batch && batch != BOGUS_BATCH) {
                    ech = batch_free_one();
                } else {
                    batch = BOGUS_BATCH;
                    bogus_iters++;
                }
            } while ((nest = (nest - 1) & 0xFF) != 0);     /* NEST is a byte */
            echoflag = ech;
            pipeflag = 0;
        }
        crlf(1);
    }
    ifflag = 0;
    forflag = 0;
    pipeoff();
    if (singlecom) singlecom = 0xFFFF;
}

/* ------------------------------------------------------ dispatching -- */

/* EXTERNAL / FNDCOM etc. after parseline: everything from OkParse on */
static void do_command(void)
{
    const char *si;
    int al;

    if (arg.argv[0].argflags & AF_WILD) cerror_msg(M_BADNAM);
    if (arg.argvcnt == 0 || arg.argv[0].arglen == 0) {
        /* nullcom */
        if (batch) nullflag = 1;
        if (singlecom == 0xFFFF) cmd_exit(retcode);
        longjmp(jb_tcommand, J_GETCOM);
    }
    si = (const char *)combuf + 2;
    al = dos_fcb_parse(&si, idlen, 0x01);
    const char *a0 = arg.argv[0].argpointer;
    if (a0[1] == ':') {
        int dl = upconv(a0[0]) - 'A';
        if (al == 0xFF) cerror_msg(M_BADDRV);
        if (*arg.argv[0].argstartel == 0) {
            /* "d:", "d:\", "d:/" - select the drive */
            dos_setdrv(dl);
            tcommand();
        }
    }
    int specdrv = idlen[0];
    int n = 0;
    while (n < 8 && idlen[1 + n] != ' ') n++;
    int namelen = n;

    /* the command tail at 80h: from the end of the command name */
    const char *p = scanoff((const char *)combuf + 2);
    for (;;) {
        int c = (uint8_t)*p++;
        if (is_delim(c) || c == '\r' || c == switchar) break;
    }
    p--;
    {
        uint8_t *d = TAIL + 1;
        int cnt = 0;
        while (*p != '\r' && cnt < 126) { *d++ = *p++; cnt++; }
        *d = '\r';
        TAIL[0] = cnt;
    }
    comsw = arg.argv[0].argsw_word;
    si = arg.argvcnt > 1 ? arg.argv[1].argpointer : (const char *)TAIL + 1 + TAIL[0];
    parm1 = dos_fcb_parse(&si, FCB, 0x01);
    arg1s = arg.argvcnt > 1 ? arg.argv[1].argsw_word : 0;
    si = arg.argvcnt > 2 ? arg.argv[2].argpointer : (const char *)TAIL + 1 + TAIL[0];
    parm2 = dos_fcb_parse(&si, FCB2, 0x01);
    arg2s = arg.argvcnt > 2 ? arg.argv[2].argsw_word : 0;
    argts = ~arg.argv[0].argsw_word & arg.argswinfo;

    if (specdrv == 0 && namelen != 0) {
        for (const struct comtab *c = comtab; c->name; c++) {
            int l = strlen(c->name);
            if (l != namelen || memcmp(c->name, idlen + 1, l)) continue;
            pathpos_len = l;
            chkdrv = c->flags;
            ioset();
            if ((chkdrv & fCheckDrive) && (parm1 | parm2) == 0xFF) cerror_msg(M_BADDRV);
            /* cmd_copy: the tail from right after the command name */
            {
                const char *q = scanoff((const char *)combuf + 2) + l;
                uint8_t *d = TAIL + 1;
                int cnt = 0;
                while (*q != '\r' && cnt < 126) { *d++ = *q++; cnt++; }
                *d = '\r';
                TAIL[0] = cnt;
            }
            if (!(chkdrv & fSwitchAllowed)) {
                if (memchr(TAIL + 1, switchar, TAIL[0])) {
                    parse_error_out(P_BADSWITCH, NULL);
                    tcommand();
                }
            }
            c->fn();
            /* Cmd_done */
            if (call_flag) {
                call_flag = 0;
                jump_docom1();
            }
            tcommand();
        }
    }
    external();
}

/* the command loop, recycle point TCOMMAND; lodcom: entered after a
 * program ran or DOS aborted us (the resident part's LODCOM) */
static void command_loop(int lodcom)
{
    int j = setjmp(jb_tcommand);
    if (lodcom && !j) j = J_LODCOM;
    lodcom = 0;
    switch (j) {
    case J_LODCOM:
        in_init = 0;
        if (ctrlc_hit) {
            ctrlc_hit = 0;
            contc_user();
        }
        lodcom1();
        goto command;
    case J_DOCOM1: goto docom1;
    case J_DOCOM: goto docom;
    case J_NOPIPEPROC: goto nopipeproc;
    case J_GETCOM: goto getcom;
    default: break;
    }
    /* TCOMMAND */
    if (verval != -1) {
        REGS r = {0};
        r.r0 = 0x2E00 | (verval & 1);
        int21(&r);
        verval = -1;
    }
    headfix();
    if (singlecom == 0xFFFF) cmd_exit(retcode);
command:
    if (restdir) {
        restdir = 0;
        dos_chdir(userdir1);
    }
    if (pipefiles && !pipeflag) pipedel();
    extcom = 0;
    load_country();
    if ((echoflag & 1) && !singletest() && !pipeflag && !forflag && !batch) crlf2();
getcom:
    call_flag = 0;
    call_batch_flag = 0;
    curdrv = dos_getdrv();
    if (pipeflag) {
        if (pipeproc()) goto nopipeproc;
        tcommand();
    }
    if ((echoflag & 1) && !singletest() && !forflag) {
        if (batch) goto testforbat;
        print_prompt();
    }
    if (forflag) forproc();     /* continues at DOCOM/DOCOM1 or TCOMMAND */
testforbat:
    re_instr[0] = 0;
    re_outstr[0] = 0;
    re_out_app = 0;
    ifflag = 0;
    if (batch) {
        readbat();
        nullflag = 0;
        if (!batch && next_batch) {
            batch = next_batch;
            next_batch = 0;
        }
        goto docom1;
    }
    if (singlecom == 1) {
        const char *s = R->single_buf;
        uint8_t *d = combuf + 2;
        int n = 0;
        singlecom = 0xFFFF;
        while (*s && *s != '\r' && n < COMBUFLEN - 2) { *d++ = *s++; n++; }
        *d = '\r';
        combuf[1] = n;
        goto docom1;
    }
    /* REGCOM: read a line (the kernel does the editing, with the previous
     * line as the template) */
    R->ucombuf[0] = COMBUFLEN;
    dos_bufinput(R->ucombuf);
    memcpy(combuf, R->ucombuf, R->ucombuf[1] + 3);
docom:
    crlf2();
docom1:
    if (prescan()) {
        pipeprocstrt();
        goto nopipeproc;
    }
nopipeproc:
    if (parseline() < 0) cerror_msg(M_BADNAM);
    do_command();
    tcommand();
}

/* ------------------------------------------------------- initialise -- */

static const char *const init_sw[] = { "/P", "/C", "/E", "/D", "/F", "/MSG", 0 };

static int streqi_n(const char *a, const char *b, int n)
{
    for (int i = 0; i < n; i++)
        if (upconv(a[i]) != upconv(b[i])) return 0;
    return 1;
}

/* the transient part's entry (tstart.S), every time the resident part
 * (re)enters it */
__attribute__((noreturn)) void tran_main(struct res *r, int reason)
{
    REGS rg;
    R = r;
    memset(&rg, 0, sizeof rg);
    rg.r0 = 0x3700;
    int21(&rg);
    switchar = rg.r3 & 0xFF;
    dirchar = '\\';
    load_country();
    if (reason != T_INIT) {
        command_loop(1);
        for (;;) ;
    }
    init();
    for (;;) ;
}

/* start-up (INIT.ASM): switches, environment, AUTOEXEC.BAT or date/time */
static void init(void)
{
    REGS r;
    memset(&r, 0, sizeof r);
    r.r0 = 0x3000;
    int21(&r);
    if ((r.r0 & 0xFF) != 4) {
        out(1, msg(M_BADVER));
        cmd_exit(1);
    }
    R->parent_psp = mypsp->parent;
    R->old_term = mypsp->int22;
    pipe1[1] = pipe2[1] = ':';
    inpipeptr = pipe1;
    outpipeptr = pipe2;

    /* ---- the command line (INIT.ASM parse_command) */
    uint8_t *t = mypsp->cmdtail;
    char line[130];
    int len = t[0];
    if (len > 126) len = 126;
    memcpy(line, t + 1, len);
    line[len] = '\r';
    line[len + 1] = 0;
    unsigned envsiz = 160 / 16;
    int prdattm = -1;       /* -1 not set, 0 prompt, 1 don't */
    int dswitch = 0, eswitch = 0, ext_msg_sw = 0;
    char comspec_dir[80];
    comspec_dir[0] = 0;
    const char *s = line;
    int npos = 0;
    int fcb_drive = mypsp->fcb1[0];
    for (;;) {
        while (*s == ' ' || *s == '\t' || *s == ',' || *s == ';' || *s == '=') s++;
        if (*s == '\r') break;
        if (*s == (char)switchar) {
            const char *e = s + 1;
            while (*e != '\r' && *e != ' ' && *e != '\t' && *e != (char)switchar && *e != ':' && *e != ',') e++;
            int l = e - s;
            int which = -1;
            for (int i = 0; init_sw[i]; i++)
                if ((int)strlen(init_sw[i]) == l && streqi_n(s, init_sw[i], l)) { which = i; break; }
            if (which < 0) {
                /* "/Cxxx" is COMMAND /C xxx (A057) */
                if (upconv(s[1]) == 'C') {
                    s += 2;
                    goto setsswitch;
                }
                init_perr(P_BADSWITCH, NULL);
                s = e;
                continue;
            }
            switch (which) {
            case 0:     /* /P */
                if (permcom) { init_perr(P_TOOMANY, NULL); break; }
                permcom = 1;
                if (prdattm == -1) prdattm = 0;
                break;
            case 1:     /* /C */
                s = e;
                while (*s == ' ' || *s == '\t') s++;
            setsswitch:
                {
                    int n = 0;
                    while (s[n] != '\r' && n < 128) { R->single_buf[n] = s[n]; n++; }
                    R->single_buf[n] = '\r';
                    R->single_buf[n + 1] = 0;
                    singlecom = 1;
                    permcom = 0;
                    prdattm = 1;
                }
                goto argsdone;
            case 2: {   /* /E:nnnnn */
                if (eswitch) { init_perr(P_TOOMANY, NULL); break; }
                eswitch = 1;
                if (*e == ':') {
                    const char *q = e + 1;
                    unsigned long v = 0;
                    int ok = *q >= '0' && *q <= '9';
                    while (*q >= '0' && *q <= '9') { v = v * 10 + (*q - '0'); if (v > 99999) v = 99999; q++; }
                    e = q;
                    if (!ok) { init_perr(P_VALUE, NULL); break; }
                    if (v < 160 || v > 32768) {
                        init_perr(P_RANGE, NULL);
                        break;
                    }
                    envsiz = (v + 15) >> 4;
                }
                break;
            }
            case 3:     /* /D */
                if (dswitch) { init_perr(P_TOOMANY, NULL); break; }
                dswitch = 1;
                prdattm = 1;
                break;
            case 4:     /* /F */
                if (ffail) { init_perr(P_TOOMANY, NULL); break; }
                ffail = 1;
                break;
            case 5:     /* /MSG */
                ext_msg_sw = 1;
                break;
            }
            s = e;
            continue;
        }
        /* a positional: a device, or the directory to find COMMAND.COM in */
        const char *e = s;
        while (*e != '\r' && *e != ' ' && *e != '\t' && *e != (char)switchar && *e != ',') e++;
        char name[80];
        int l = e - s;
        if (l > 78) l = 78;
        for (int i = 0; i < l; i++) name[i] = upconv(s[i]);
        name[l] = 0;
        s = e;
        if (npos++ >= 2) {
            init_perr(P_TOOMANY, NULL);
            continue;
        }
        int h = dos_open(name, 2);
        if (h >= 0) {
            int info = dos_ioctl_info(h);
            if (info >= 0 && (info & 0x80)) {
                dos_ioctl_set(h, (info | 3) & 0xFF);
                dos_close(0);
                dos_close(1);
                dos_close(2);
                dos_dup(h);
                dos_dup(h);
                dos_dup(h);
                dos_close(h);
                continue;
            }
            dos_close(h);
        }
        /* CHKSRCHSPEC */
        strcpy(comspec_dir, name);
    }
argsdone:
    if (ext_msg_sw && !permcom) init_perr(P_MISSING, NULL);
    if (prdattm == -1) prdattm = 1;

    /* ---- COMSPEC and the environment */
    {
        char built_comspec[96];
        int build = mypsp->envseg == 0;
        if (comspec_dir[0]) {
            char tryname[96];
            int l = strlen(comspec_dir);
            strcpy(tryname, comspec_dir);
            if (l && !pathchr(tryname[l - 1])) tryname[l++] = '\\';
            strcpy(tryname + l, "COMMAND.COM");
            int h = dos_open(tryname, 0);
            if (h >= 0) {
                dos_close(h);
                strcpy(built_comspec, tryname);
            } else {
                int e = dos_error();
                out(1, msg(e == 65 ? M_BADCOMACC : M_BADCOMLK));
                goto defcomspec;
            }
            build = 1;
        } else {
        defcomspec:
            if (fcb_drive) {
                built_comspec[0] = '@' + fcb_drive;
                built_comspec[1] = ':';
                strcpy(built_comspec + 2, "\\COMMAND.COM");
            } else {
                strcpy(built_comspec, "\\COMMAND.COM");
            }
        }
        env_init(envsiz, build ? 1 : 0);
        if (build) {
            /* the default environment (ENVDATA.ASM): PATH= then COMSPEC= */
            char *d = env_end();
            const char *a = "PATH=";
            while (*a) env_store(&d, *a++);
            env_store(&d, 0);
            a = "COMSPEC=";
            while (*a) env_store(&d, *a++);
            a = built_comspec;
            while (*a) env_store(&d, *a++);
        }
        char *cs = env_find("COMSPEC=");
        xstrlcpy(comspec, cs ? cs : built_comspec, sizeof comspec);
    }

    /* ---- vectors, PSP */
    /* only a permanent COMMAND puts its ^C and critical error handlers in its
     * PSP (INIT.ASM); a secondary one leaves its parent's there */
    if (permcom) set_psp_handlers();
    install_handlers();
    mypsp->parent = (uint16_t)((uint32_t)mypsp >> 4);      /* parent is me */
    {
        uint8_t *jft = (uint8_t *)mypsp->jftptr;
        io_save[0] = jft[0];
        io_save[1] = jft[1];
    }

    /* ---- AUTOEXEC.BAT, or the date and time prompts; the banner */
    int autoexec = 0;
    if (prdattm == 0) {
        char ab[20];
        int drv = fcb_drive ? fcb_drive - 1 : dos_getdrv();
        ab[0] = 'A' + drv;
        strcpy(ab + 1, ":\\AUTOEXEC.BAT");
        int h = dos_open(ab, 0);
        if (h >= 0) {
            dos_close(h);
            autoexec = 1;
            echoflag = 3;
            strcpy(execpath, ab);
            /* the command line for %0 is the file name */
            strcpy((char *)combuf + 2, ab);
            combuf[1] = strlen(ab);
            combuf[2 + combuf[1]] = '\r';
            batcom_init(ab);
        } else {
            if (dos_error() == 65) out(1, msg(M_RACCDEN));
            echoflag = 1;
            datinit();
        }
    }
    if (!autoexec && singlecom == 0) out(1, msg(M_HEADER));
    in_init = 0;
    command_loop(0);
}
