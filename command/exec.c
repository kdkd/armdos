/*
 * exec.c - external commands (PATH1.ASM, PATH2.ASM, TMISC1.ASM EXTERNAL,
 * COMMAND1.ASM EXT_EXEC/EXEC_ERR), redirection (TMISC2.ASM IOSET), HEADFIX
 * (COMMAND2.ASM) and pipes (TPIPE.ASM).
 */
#include "cmd.h"

char execpath[128];


/* ------------------------------------------------------ path search -- */

static uint8_t fbuf[64];                /* DTA for the searches */
static char search_best_buf[16];
static int  search_best;
static int  ext_entered;
static char psep;

static void store_pchar(void)
{
    psep = pathchr('/') ? '/' : '\\';
}

/* Search_Ftype */
static int search_ftype(void)
{
    const char *name = (const char *)fbuf + 30;
    int l = strnlen(name, 13);
    int ax = 0;
    if (l >= 4) {
        const char *e = name + l - 4;
        if (!memcmp(e, ".COM", 4)) ax = 8;
        else if (!memcmp(e, ".EXE", 4)) ax = 4;
        else if (!memcmp(e, ".BAT", 4)) ax = 2;
    }
    if (ext_entered != 1 && ax) {
        ext_entered = ax;
        ax = 8;
    }
    return ax;
}

/* Search: look for pname (with .??? or the typed extension) */
static int search(const char *pname, int errmsg)
{
    char tmp[80];
    int drive = 0;
    if (pname[0] && pname[1] == ':') drive = upconv(pname[0]) - '@';
    if (dos_curdir(drive, tmp)) {
        std_printf(errmsg);
        return 0;
    }
    if (dos_findfirst(pname, 0x13)) return 0;
    search_best = 0;
    search_best_buf[0] = 0;
    for (;;) {
        int t = search_ftype();
        if (t > search_best) {
            search_best = t;
            memcpy(search_best_buf, fbuf + 30, 13);
            search_best_buf[13] = 0;
            if (t == 8) break;
        }
        if (dos_findnext()) break;
    }
    if (ext_entered != 1) search_best = ext_entered;
    return search_best;
}

/* Strip: argv[0] into buf, ".???" added when no extension was typed */
static int strip(char *buf, int bufsize)
{
    struct argv_ele *a = &arg.argv[0];
    ext_entered = 1;
    if (*a->argstartel == 0) return -1;
    const char *el = a->argstartel;
    const char *dot = strchr(el, '.');
    if (dot) ext_entered = 0;
    int l = a->arglen;
    if (l + 1 > bufsize - 4) return -1;
    memcpy(buf, a->argpointer, l + 1);
    if (ext_entered == 1) strcpy(buf + l, ".???");
    return 0;
}

int path_search(char *ep)
{
    struct argv_ele *a = &arg.argv[0];
    char *path;
    int ax;

    if (a->argflags & (AF_WILD | AF_SWITCH)) return 0;
    store_pchar();
    dos_setdta(fbuf);
    path = env_find("PATH=");
    if (strip(ep, 128)) return 0;
    ax = search(ep, M_BADDRV);
    if (ax) {
        /* found in the current (or the given) directory: build the full name */
        char out[128];
        char *di = out;
        const char *si = a->argpointer;
        int cx = a->argstartel - a->argpointer;
        int dl;
        if (cx < 2 || si[1] != ':') {
            *di++ = curdrv + 'A';
            *di++ = ':';
            dl = curdrv + 1;
        } else {
            dl = upconv(si[0]) - '@';
            *di++ = upconv(si[0]);
            *di++ = ':';
            si += 2;
            cx -= 2;
        }
        *di++ = psep;
        int abs = 0;
        if (cx >= 1 && *si == psep) {
            si++;
            cx--;
            abs = 1;
        }
        if (!abs) {
            if (dos_curdir(dl, di)) *di = 0;
            if (*di) {
                while (*di) di++;
                *di++ = psep;
            }
        }
        if (cx > 0 && *si == psep) { si++; cx--; }
        memcpy(di, si, cx);
        di += cx;
        strcpy(di, search_best_buf);
        strcpy(ep, out);
        return ax;
    }
    if (a->argflags & AF_PATHSEP) return 0;
    if (!path) return 0;
    /* the name part as stripped */
    char name[20];
    xstrlcpy(name, ep + (a->argstartel - a->argpointer), sizeof name);
    const char *p = path;
    for (;;) {
        const char *old = p;
        char tp[160];
        int n = 0;
        while (*p && *p != ';') {
            if (n < 140) tp[n++] = *p;
            p++;
        }
        int more = *p == ';';
        if (more) p++;
        if (n) {
            if (tp[n - 1] != psep) tp[n++] = psep;
            strcpy(tp + n, name);
            ax = search(tp, M_BADPMES);
            if (ax) {
                /* path_found: the PATH element as typed + the name */
                char *di = ep;
                const char *s = old;
                const char *e = old;
                while (*e && *e != ';') e++;
                if (e - s >= 3 && s[1] == ':' && s[2] == '.') {
                    char cur[80];
                    *di++ = s[0];
                    *di++ = ':';
                    *di++ = psep;
                    if (dos_curdir(upconv(s[0]) - '@', cur) == 0) {
                        strcpy(di, cur);
                        di += strlen(cur);
                    }
                    s += 3;
                }
                memcpy(di, s, e - s);
                di += e - s;
                if (di == ep || di[-1] != psep) *di++ = psep;
                strcpy(di, search_best_buf);
                return ax;
            }
        }
        if (!more) break;
    }
    return 0;
}

/* ------------------------------------------------------------ EXEC -- */

void external(void)
{
    execpath[0] = 0;
    int ax = path_search(execpath);
    if (!ax) cerror_msg(M_BADNAM);
    if (ax < 4) {
        batcom();
        tcommand();
    }
    /* EXECUTE: the resident part runs it (our memory is freed meanwhile) */
    ioset();
    extcom = 1;
    restdir = 0;
    memcpy(R->execpath, execpath, sizeof R->execpath);
    memcpy(R->exectail, TAIL, sizeof R->exectail);
    memcpy(R->execfcb1, FCB, sizeof R->execfcb1);
    memcpy(R->execfcb2, FCB2, sizeof R->execfcb2);
    R->exec();
    for (;;) ;
}

/* SETVECT: INT 22h = LODCOM (also in our PSP), INT 23h, INT 24h */
void install_handlers(void)
{
    mypsp->int22 = (uint32_t)R->h22;
    dos_setvect(0x22, R->h22);
    dos_setvect(0x23, R->h23);
    dos_setvect(0x24, R->h24);
}

/* a permanent COMMAND keeps its ^C and critical error handlers in its PSP */
void set_psp_handlers(void)
{
    mypsp->int22 = (uint32_t)R->h22;
    mypsp->int23 = (uint32_t)R->h23;
    mypsp->int24 = (uint32_t)R->h24;
}

/* ---------------------------------------------------- redirection -- */

/* TriageError: the extended error, "Access denied" for the network one */
__attribute__((noreturn)) static void redirerr(int was_open)
{
    int e = dos_error();
    if (e == 65) cerror_msg(M_ACCDEN);
    if (was_open) cerror_ext(e, NULL);
    cerror_msg(M_FULDIR);
}

static uint8_t *jft(void)
{
    return (uint8_t *)mypsp->jftptr;
}

void testdorein(void)
{
    if (!re_instr[0]) return;
    int h = dos_open(re_instr, 0);
    if (h < 0) redirerr(1);
    uint8_t *j = jft();
    j[0] = j[h];
    j[h] = 0xFF;
}

void testdoreout(void)
{
    int h;
    if (!re_outstr[0]) return;
    if (re_out_app) {
        h = dos_open(re_outstr, 2);
        if (h < 0) {
            if (-h == 5) redirerr(1);
            goto creat;
        }
        int info = dos_ioctl_info(h);
        if (info < 0 || !(info & 0x80)) {
            uint8_t c;
            dos_lseek(h, -1, 2);
            int n = dos_read(h, &c, 1);
            if (n < 0) redirerr(1);
            if (n != 1) dos_lseek(h, 0, 0);
            else if (c == 0x1A) dos_lseek(h, -1, 1);
        }
        goto set;
    }
creat:
    h = dos_creat(re_outstr, 0);
    if (h < 0) redirerr(0);
set:
    {
        uint8_t *j = jft();
        j[1] = j[h];
        j[h] = 0xFF;
    }
}

void ioset(void)
{
    if (pipeflag || ifflag) return;
    testdorein();
    testdoreout();
}

/* HEADFIX: our vectors again, stdin/stdout back, handles 5-19 closed */
void headfix(void)
{
    uint8_t *j = jft();
    install_handlers();
    copy_cleanup();
    if (j[0] != io_save[0]) {
        dos_close(0);
        j[0] = io_save[0];
    }
    if (j[1] != io_save[1]) {
        dos_close(1);
        j[1] = io_save[1];
    }
    for (int h = 5; h < 20; h++)
        if (j[h] != 0xFF) dos_close(h);
}

/* ------------------------------------------------------------ pipes -- */

void pipeoff(void)
{
    if (pipeflag) {
        pipeflag = 0;
        echoflag >>= 1;
    }
}

void pipedel(void)
{
    dos_unlink(pipe1);
    dos_unlink(pipe2);
    pipeoff();
    pipefiles = 0;
}

__attribute__((noreturn)) static void pipeerr(void)
{
    int e = dos_error();
    pipedel();
    std_eprintf(M_PIPEEMES);
    if (e == 65) cerror_msg(M_ACCDEN);
    tcommand();
}

__attribute__((noreturn)) static void pipeerrsyn(void)
{
    pipedel();
    cerror_msg(M_SYNTMES);
}

static int create_temp(char *name)
{
    REGS r = {0};
    r.r0 = 0x5A00;
    r.r2 = 0;
    r.r3 = (uint32_t)name;
    if (int21(&r)) return -1;
    dos_close(r.r0 & 0xFFFF);
    return 0;
}

/* copy the next pipe element into combuf; set up its output */
static void pipe_element(const char *si)
{
    uint8_t *di = combuf + 2;
    int cx = 0;
    if (*si == '\r' || *si == '|') pipeerrsyn();
    for (;;) {
        int al = (uint8_t)*si++;
        *di++ = al;
        if (al == '\r') {
            combuf[1] = cx;
            pipeptr = (char *)si - 1;
            testdoreout();
            return;
        }
        cx++;
        if (al == '|') {
            di[-1] = '\r';
            cx--;
            combuf[1] = cx;
            pipeptr = (char *)si - 1;
            int h = dos_creat(outpipeptr, 0);
            if (h < 0) pipeerr();
            uint8_t *j = jft();
            j[1] = j[h];
            j[h] = 0xFF;
            char *t = inpipeptr;
            inpipeptr = outpipeptr;
            outpipeptr = t;
            return;
        }
    }
}

void pipeprocstrt(void)
{
    pipefiles++;
    int d = dos_getdrv() + 'A';
    pipe1[0] = pipe2[0] = d;
    pipe1[1] = pipe2[1] = ':';
    pipe1[2] = pipe2[2] = pathchr('/') ? '/' : '\\';
    pipe1[3] = pipe2[3] = 0;
    if (create_temp(pipe1)) pipeerr();
    if (create_temp(pipe2)) pipeerr();
    testdorein();
    if (singlecom == 0xFFFF) singlecom = 0xF000;
    pipe_element(pipeptr);
}

/* PIPEPROC: the next element of a pipe, or PIPEEND */
int pipeproc(void)
{
    echoflag &= 0xFE;
    const char *si = pipeptr;
    int al = (uint8_t)*si++;
    if (al == '|') {
        int h = dos_open(inpipeptr, 0);
        if (h < 0) pipeerr();
        uint8_t *j = jft();
        j[0] = j[h];
        j[h] = 0xFF;
        pipe_element(si);
        return 1;
    }
    pipedel();
    if (singlecom == 0xF000) singlecom = 0xFFFF;
    return 0;
}
