/*
 * batch.c - batch files (TBATCH.ASM, TBATCH2.ASM): BatCom, ReadBat with %
 * substitution, GOTO, SHIFT, CALL, IF, and FOR (TFOR.ASM).
 *
 * The batch file is re-opened, seeked, read and closed for every line, as
 * DOS 4 does.  Batch and FOR state live in DOS memory blocks like the
 * original's batch/FOR segments.
 */
#include "cmd.h"

struct batseg {
    uint8_t  type;
    uint8_t  batechoflag;       /* echo state to restore at the end */
    struct batseg *last;        /* the batch file that CALLed this one */
    struct forinfo *batforptr;  /* a FOR that was running when we started */
    uint8_t  batforflag;
    uint32_t seek;
    int16_t  parm[10];          /* offsets into args, -1 = none */
    char     file[80];
    char     args[];            /* CR-terminated parameters, then NUL */
};

struct forinfo {
    struct arg_unit args;
    int  minarg, maxarg, com_start, expand;
    char var;
    uint8_t dma[128];
    char forbuf[COMBUFLEN + 16];
};

static int bathand;                 /* the open batch file + 1 */
static uint8_t batbuf[128];
static int batbufpos, batbufend;       /* (set by batopen) */
static uint8_t suppress;

extern __attribute__((noreturn)) void jump_docom1(void);
extern __attribute__((noreturn)) void jump_docom(void);

static unsigned ptr2seg(const void *p) { return (uint32_t)p >> 4; }

/* ------------------------------------------------------------ ForOff -- */

void foroff(void)
{
    if (forptr) dos_free(ptr2seg(forptr));
    forptr = 0;
    forflag = 0;
}

/* ---------------------------------------------------------- BatchOff -- */

void batchoff(void)
{
    struct batseg *b = batch;
    if (!b) return;
    if (!(echoflag & 1)) suppress = 1;
    echoflag = b->batechoflag;
    forptr = b->batforptr;
    forflag = b->batforflag;
    struct batseg *last = b->last;
    dos_free(ptr2seg(b));
    next_batch = last;
    nest = (nest - 1) & 0xFF;          /* NEST is a byte */
    batch = 0;
}

static void batclose(void)
{
    if (bathand - 1 >= 5) dos_close(bathand - 1);
    bathand = 0;
    in_batch = 0;
}

/* BatOpen: 0 ok, else the extended error */
static int batopen(void)
{
    int h = dos_open(batch->file, 0);
    if (h < 0) return dos_error();
    bathand = h + 1;
    dos_lseek(h, (long)batch->seek, 0);
    batbufpos = -1;
    return 0;
}

void getkeystroke(void)
{
    dos_getc_noecho_flush();
    dos_flush_kbd();
}

/* is the drive of the batch file removable (the RCH_ADDR check)? */
static int removable(void)
{
    REGS r = {0};
    const char *f = batch->file;
    int drv = (f[0] && f[1] == ':') ? upconv(f[0]) - '@' : 0;
    r.r0 = 0x4408;
    r.r1 = drv;
    if (int21(&r)) return 0;
    return (r.r0 & 0xFFFF) == 0;
}

static void promptbat(void)
{
    if (batch == BOGUS_BATCH) {
        /* (see contc_user): DOS 4 reads a batch segment from memory that
         * is not one; the file cannot be opened; its BatchOff then takes
         * the echo state from that memory too.  On the real 4.00 that
         * turned echo off after one such step, and on after the 255 a
         * wrapped-around NEST makes. */
        extern int bogus_iters;
        foroff();
        pipeoff();
        ifflag = 0;
        batch = 0;
        next_batch = 0;
        in_batch = 0;
        echoflag = bogus_iters == 1 ? 0 : 1;
        nest = (nest - 1) & 0xFF;
        cerror_msg(M_BADBAT);
    }
    for (;;) {
        int e = batopen();
        if (!e) return;
        if (e == 2 || e == 3) {
            if (removable()) {
                std_eprintf(M_NEEDBAT);
                std_eprintf(M_PAUSEMES);
                getkeystroke();
                continue;
            }
            foroff();
            pipeoff();
            ifflag = 0;
            batchoff();
            cerror_msg(M_BADBAT);
        }
        char name[80];
        xstrlcpy(name, batch->file, sizeof name);
        batchoff();
        cerror_ext(e, name);
    }
}

/* GetBatByt: the next byte; at the end (or ^Z) the batch ends and CR */
static int batch_eof_reset;

static int getbatbyt(void)
{
    if (batch_abort || !batch) goto bateof;
    batch->seek++;
    if (batbufpos == -1) {
        int n = dos_read(bathand - 1, batbuf, sizeof batbuf);
        if (n < 0) {
            char name[80];
            xstrlcpy(name, batch->file, sizeof name);
            ext_error_out(dos_error(), name);
            combuf[2] = '\r';
            goto bateof;
        }
        if (n == 0) goto bateof;
        batbufend = n;
        batbufpos = 0;
    }
    int c = batbuf[batbufpos++];
    if (batbufpos >= batbufend) batbufpos = -1;
    if (c == 0x1A) goto bateof;
    return c;
bateof:
    batchoff();
    batclose();
    if (batch_abort) {
        batch_abort = 0;
        batch_eof_reset = 1;
        return '\r';
    }
    if (singlecom == 0xFFF0 && nest == 0) singlecom = 0xFFFF;
    return '\r';
}

/* SkipDelim: -1 when no batch file */
static int skipdelim(void)
{
    for (;;) {
        if (!batch) return -1;
        int c = getbatbyt();
        if (!is_delim(c)) return c;
    }
}

static void skiptoeol(void)
{
    while (batch) {
        if (getbatbyt() == '\r') return;
    }
}

/* ------------------------------------------------------------ ReadBat -- */

void readbat(void)
{
    char *di;
    int cx;
    suppress = 0;
    if (!batch_abort) {
        in_batch = 1;
        promptbat();
    }
    batch_eof_reset = 0;
    di = (char *)combuf + 2;
testnop:
    {
        uint32_t saved = batch ? batch->seek : 0;
        int al = skipdelim();
        if (al == ':') {
            skiptoeol();
            getbatbyt();
            if (batch) goto testnop;
            return;
        }
        cx = 0;
        if (batch) {
            if (al == '@') {
                suppress = 1;
            } else {
                batch->seek = saved;
                dos_lseek(bathand - 1, (long)saved, 0);
                batbufpos = -1;
            }
        }
    }
    for (;;) {
        int al = getbatbyt();
        if (batch_eof_reset) {
            batch_eof_reset = 0;
            di = (char *)combuf + 2;
            cx = 0;
        }
        cx++;
        if (cx >= COMBUFLEN) goto toolong_al;
        if (al == '%') {
            al = getbatbyt();
            if (al == '%' || al == '\r') goto savbatbyt_al;
            if (al >= '0' && al <= '9') {
                int16_t o = batch ? batch->parm[al - '0'] : -1;
                if (!batch || o == -1) continue;
                const char *p = batch->args + o;
                cx--;
                while (*p != '\r') {
                    cx++;
                    if (cx >= COMBUFLEN) {
                        al = 0;
                        goto toolong_al;
                    }
                    *di++ = *p++;
                }
                continue;
            }
            /* NEEDENV: %name% or %name<CR> */
            {
                char id[COMBUFLEN + 2];
                int n = 0;
                id[n++] = al;
                for (;;) {
                    al = getbatbyt();
                    if (al == '\r') {
                        id[n] = 0;
                        /* %name at the end of the line: name, then CR */
                        for (int i = 0; i < n; i++) {
                            cx++;
                            if (cx >= COMBUFLEN) { al = 0; goto toolong_al; }
                            *di++ = id[i];
                        }
                        al = '\r';
                        goto savbatbyt_al;
                    }
                    if (al == '%') break;
                    if (n < COMBUFLEN) id[n++] = al;
                }
                id[n++] = '=';
                id[n] = 0;
                const char *v = env_find(id);
                if (v) {
                    while (*v) {
                        cx++;
                        if (cx >= COMBUFLEN) { al = 0; goto toolong_al; }
                        *di++ = *v++;
                    }
                }
                continue;
            }
        }
    savbatbyt_al:
        *di++ = al;
        if (al == '\r') break;
        continue;
    toolong_al:
        if (al != '\r') skiptoeol();
        *di++ = '\r';
        break;
    }
    /* Found_EOL */
    combuf[1] = (uint8_t *)di - (combuf + 3);
    getbatbyt();                /* the LF */
    batclose();
    if (suppress) return;
    if (!(echoflag & 1)) return;
    if (!nullflag) crlf2();
    print_prompt();
    {
        const char *p = (const char *)combuf + 2;
        const char *e = p;
        while (*e != '\r') e++;
        outn(1, p, e - p);
    }
    crlf2();
}

/* ------------------------------------------------------------- BatCom -- */

static struct batseg *newbatch(const char *file, const char *line, struct batseg *last)
{
    unsigned l = strlen(file);
    unsigned bytes = sizeof(struct batseg) + COMBUFLEN + 16 + 15;
    (void)l;
    unsigned seg = dos_alloc((bytes + 15) >> 4, 0);
    if (!seg) return 0;
    struct batseg *b = (struct batseg *)ARMDOS_SEG2PTR(seg);
    b->type = 0;
    b->last = last;
    b->batforflag = forflag;
    b->batforptr = forflag ? forptr : 0;
    if (forflag) forflag = 0;
    forptr = 0;
    b->batechoflag = echoflag;
    b->seek = 0;
    for (int i = 0; i < 10; i++) b->parm[i] = -1;
    xstrlcpy(b->file, file, sizeof b->file);
    /* the parameters: %0 is the command as typed */
    char *d = b->args;
    int cnt = 0;
    if (line) {
        const char *s = line;
        for (;;) {
            s = scanoff(s);
            if (*s == '\r') break;
            if (cnt < 10) b->parm[cnt++] = d - b->args;
            for (;;) {
                int c = (uint8_t)*s++;
                if (is_delim(c)) {
                    *d++ = '\r';
                    break;
                }
                *d++ = c;
                if (c == '\r') goto havparm;
            }
        }
    }
havparm:
    *d++ = 0;
    dos_setblock(seg, (((uint8_t *)d - (uint8_t *)b) + 15) >> 4, 0);
    return b;
}

void batcom(void)
{
    struct batseg *last;
    if (!call_batch_flag) ioset();
    if (!call_batch_flag) foroff();
    pipeoff();
    int saved_echo = echoflag & 1;
    last = 0;
    if (batch) {
        last = batch;
        if (!call_batch_flag) last = batch->last;
    }
    if (!call_batch_flag) batchoff();
    call_batch_flag = 0;
    struct batseg *b = newbatch(execpath, (const char *)combuf + 2, last);
    if (!b) cerror_ext(8, NULL);
    batch = b;
    nest = (nest + 1) & 0xFF;
    if (singlecom == 0xFFFF) singlecom = 0xFFF0;
    echoflag = saved_echo;
    tcommand();
}

/* AUTOEXEC.BAT at start-up (INIT.ASM dodate): no parameters, echo on */
void batcom_init(const char *file)
{
    struct batseg *b = newbatch(file, 0, 0);
    if (!b) return;
    b->batechoflag = 1;
    batch = b;
    nest = 1;
    echoflag = 3;
}

/* ClearBatch's step: free the current batch segment (and its FOR); returns
 * its saved echo state */
int batch_free_one(void)
{
    struct batseg *b = batch;
    if (b->batforptr) dos_free(ptr2seg(b->batforptr));
    int e = b->batechoflag;
    batch = b->last;
    dos_free(ptr2seg(b));
    return e;
}

/* ------------------------------------------------------------ ASKEND -- */

int askend(void)
{
    for (;;) {
        out(1, msg(M_ENDBATMES));
        int c = upconv(dos_getc_echo_flush());
        if (c == msg(206)[0]) return 0;
        if (c == msg(205)[0]) return 1;
    }
}

/* -------------------------------------------------------------- SHIFT -- */

void c_shift(void)
{
    struct batseg *b = batch;
    if (!b) return;
    int16_t p9 = b->parm[9];
    for (int i = 0; i < 9; i++) b->parm[i] = b->parm[i + 1];
    if (p9 == -1) return;
    b->parm[9] = -1;
    const char *s = b->args + p9;
    while (*s++ != '\r') ;
    if (*s == 0) return;
    b->parm[9] = s - b->args;
}

/* --------------------------------------------------------------- CALL -- */

void c_call(void)
{
    const char *s = scanoff((const char *)combuf + 2) + 4;
    int n = combuf + 2 + COMBUFLEN - (uint8_t *)s;
    memmove(combuf + 2, s, n > 0 ? n : 0);
    call_flag = 1;
    call_batch_flag = 1;
    if (pipefiles) pipedel();
}

/* --------------------------------------------------------------- GOTO -- */

void c_goto(void)
{
    if (!batch) return;
    batch->seek = 0;
    promptbat();
    const uint8_t *lab = FCB + 1;
    int gotolen = 0;
    while (gotolen < 11 && lab[gotolen] != ' ') gotolen++;
    int al = skipdelim();
    if (al < 0) goto badgoto;
    if (al == ':') goto chklabel;
    for (;;) {
        /* LABLKLP: look for the next line starting with ':' */
        while (batch) {
            al = getbatbyt();
            if (al != '\n') continue;
            al = skipdelim();
            if (al < 0) goto badgoto;
            if (al == ':') goto chklabel;
        }
        goto badgoto;
    chklabel:
        al = skipdelim();
        if (al < 0) goto badgoto;
        {
            int i, ok = 1;
            for (i = 0; i < gotolen; i++) {
                if (i) al = getbatbyt();
                if (upconv(al) != lab[i]) { ok = 0; break; }
            }
            if (!ok) continue;
            al = getbatbyt();
            if (gotolen < 8 && al > ' ') continue;
            if (al != '\r') {
                while (batch && getbatbyt() != '\r') ;
            }
            getbatbyt();
            batclose();
            return;
        }
    }
badgoto:
    batclose();
    cerror_msg(M_BADLAB);
}

/* ----------------------------------------------------------------- IF -- */

__attribute__((noreturn)) static void iferror(void)
{
    cerror_msg(M_SYNTMES);
}

__attribute__((noreturn)) static void iftrue_then(const char *si)
{
    si = scanoff(si);
    const char *e = si;
    while (*e != '\r') e++;
    int n = e - si;
    memmove(combuf + 2, si, n);
    combuf[2 + n] = '\r';
    combuf[1] = n;
    ifflag = 1;
    jump_docom1();
}

void c_if(void)
{
    const char *si = (const char *)TAIL + 1;
    int notflag = 0, if_not_count = 0;
    static const char *const kw[] = { "NOT", "ERRORLEVEL", "EXIST" };
    if (pipefiles) pipedel();
    for (;;) {
        si = scanoff(si);
        if (*si == '\r') iferror();
        const char *bp = si;
        int which = -1;
        for (int k = 0; k < 3; k++) {
            int l = strlen(kw[k]), i;
            for (i = 0; i < l; i++) {
                int c = (uint8_t)bp[i];
                if (c != kw[k][i] && c != (kw[k][i] | 0x20)) break;
            }
            if (i != l) continue;
            if (bp[l] == '\r') iferror();
            if (!is_delim((uint8_t)bp[l])) continue;
            which = k;
            si = scanoff(bp + l + 1);
            break;
        }
        if (which == 0) {
            notflag = !notflag;
            if_not_count++;
            continue;
        }
        int result;
        if (which == 1) {
            /* IFERLEV */
            uint8_t bl = 0;
            for (;;) {
                int c = (uint8_t)*si++;
                if (c == '\r') iferror();
                if (is_delim(c)) break;
                bl = (uint8_t)(bl * 10 + (uint8_t)(c - '0'));
            }
            result = (retcode & 0xFF) >= bl;
        } else if (which == 2) {
            /* IFEXISTS: the file name is argv[2 + nots] */
            while (!is_delim((uint8_t)*si++)) ;
            dos_setdta(dirbuf);
            int idx = 2 + if_not_count;
            const char *f = idx < arg.argvcnt ? arg.argv[idx].argpointer : "";
            result = dos_findfirst(f, 0x06) == 0;
        } else {
            /* IFSTRING */
            const char *first = si;
            int cx = 0;
            int c;
            for (;;) {
                c = (uint8_t)*si++;
                if (c == '\r') iferror();
                if (is_delim(c)) break;
                cx++;
            }
            while (c != '=') {
                if (c == '\r') iferror();
                c = (uint8_t)*si++;
            }
            c = (uint8_t)*si++;
            if (c != '=') iferror();
            si = scanoff(si);
            if (*si == '\r') iferror();
            if (cx == 0) {
                result = 0;
                goto ifret;
            }
            int i;
            for (i = 0; i < cx; i++)
                if (si[i] != first[i]) break;
            if (i == cx) {
                si += cx;
                c = (uint8_t)*si++;
                if (is_delim(c)) {
                    result = 1;
                    goto ifret;
                }
            } else {
                si += i;
                if (*si == '\r') iferror();
                si++;
                c = (uint8_t)si[-1];
            }
            /* not the same: skip the rest of the second string */
            for (;;) {
                if (c == '\r') iferror();
                if (is_delim(c)) break;
                c = (uint8_t)*si++;
            }
            result = 0;
        }
    ifret:
        if (notflag) result = !result;
        if (!result) tcommand();
        iftrue_then(si);
    }
}

/* ---------------------------------------------------------------- FOR -- */

__attribute__((noreturn)) static void forerror(void)
{
    cerror_msg(M_SYNTMES);
}

static void rebase(struct arg_unit *dst, const struct arg_unit *src)
{
    memcpy(dst, src, sizeof *dst);
    long delta = (const char *)dst - (const char *)src;
    for (int i = 0; i < dst->argvcnt; i++) {
        struct argv_ele *a = &dst->argv[i];
        const char *lo = (const char *)src, *hi = (const char *)(src + 1);
        if (a->argpointer >= lo && a->argpointer < hi) a->argpointer += delta;
        if (a->argstartel >= lo && a->argstartel < hi) a->argstartel += delta;
        if (a->arg_ocomptr >= lo && a->arg_ocomptr < hi) a->arg_ocomptr += delta;
    }
}

void c_for(void)
{
    int dx = 0;
    const char *si;
    int al, ah;
    if (forflag) {
        foroff();
        if (singlecom == 0xFF00) singlecom = 0xFFFF;
        cerror_msg(M_FORNEST);
    }
    if (pipefiles) pipedel();
#define NEXTARG() do { dx++; if (dx >= arg.argvcnt) forerror(); si = arg.argv[dx].argpointer; al = (uint8_t)si[0]; ah = al ? (uint8_t)si[1] : 0; si += 2; } while (0)
    NEXTARG();
    if (al != '%') forerror();
    int var = ah;
    if (!ah || *si) forerror();
    NEXTARG();
    if ((al & ~0x20) != 'I' || (ah & ~0x20) != 'N') forerror();
    struct argv_ele *a = &arg.argv[dx];
    if (*si == 0) {
        NEXTARG();
        a = &arg.argv[dx];
    } else if (*si == '(') {
        a->argpointer += 2;
        a->arg_ocomptr += 2;
        a->arglen -= 2;
        al = '(';
        ah = (uint8_t)si[1];
        si += 2;
    } else {
        forerror();
    }
    if (al != '(') forerror();
    if (ah == 0) {
        NEXTARG();
        if (al == ')' && ah == 0) goto forterm;
    } else if (ah == ')') {
        goto forterm;
    } else {
        arg.argv[dx].argpointer++;
        arg.argv[dx].arglen--;
        si++;
    }
    int first = dx;
    int last;
    for (;;) {
        struct argv_ele *e = &arg.argv[dx];
        char *lastc = e->argpointer + e->arglen - 1;
        if (e->arglen && *lastc == ')') {
            last = dx;
            *lastc = 0;
            int onlyparen = e->arglen == 1;
            if (!onlyparen) last++;
            break;
        }
        NEXTARG();
    }
    NEXTARG();
    if ((al & ~0x20) != 'D' || (ah & ~0x20) != 'O' || *si) forerror();
    NEXTARG();
    (void)al;
    /* Save_Args */
    foroff();
    unsigned seg = dos_alloc((sizeof(struct forinfo) + 15) >> 4, 0);
    if (!seg) cerror_ext(8, NULL);
    struct forinfo *f = (struct forinfo *)ARMDOS_SEG2PTR(seg);
    rebase(&f->args, &arg);
    f->minarg = first - 1;
    f->maxarg = last - 1;
    f->com_start = dx;
    f->expand = -1;
    f->var = var;
    forptr = f;
    forflag = 1;
    if (singlecom == 0xFFFF) singlecom = 0xFF00;
    return;
forterm:
    foroff();
    if (singlecom == 0xFF00 && nest == 0) {
        singlecom = 0xFFFF;
        tcommand();
    }
    if ((echoflag & 1) && batch) crlf2();
    tcommand();
#undef NEXTARG
}

/* FORPROC: the next iteration */
void forproc(void)
{
    struct forinfo *f = forptr;
    dos_setdta(f->dma);
    for (;;) {
        if (f->expand != 0) f->minarg++;
        if (f->minarg > f->maxarg) {
            /* FORTERM */
            foroff();
            if (singlecom == 0xFF00 && nest == 0) {
                singlecom = 0xFFFF;
                tcommand();
            }
            if ((echoflag & 1) && batch) crlf2();
            tcommand();
        }
        struct argv_ele *a = &f->args.argv[f->minarg];
        char *cx = a->argstartel;
        char *dx = a->argpointer;
        if (!(a->argflags & AF_PATHSEP)) {
            if (dx[-1] == '(') {
                cx++;
                if (dx[1] == ':') cx += 2;
            }
        }
        int plen = cx - dx;
        if (plen < 0) plen = 0;
        int r;
        if (f->expand == 0) {
            r = dos_findnext();
        } else if (a->argflags & AF_WILD) {
            r = dos_findfirst(dx, 0);
        } else {
            int n = a->arglen;
            memcpy(f->forbuf, dx, n);
            f->forbuf[n] = 0;
            goto make_com;
        }
        f->expand = r ? -1 : 0;
        if (r) continue;
        memcpy(f->forbuf, dx, plen);
        strcpy(f->forbuf + plen, (const char *)f->dma + 30);
        break;
    }
make_com:
    {
        uint8_t *di = combuf + 2;
        int n = 0;
        const char *s = f->args.argv[f->com_start].arg_ocomptr + 1;
        for (;;) {
            int c = (uint8_t)*s++;
            if (c == '%' && *s == f->var) {
                s++;
                for (const char *p = f->forbuf; *p && n < COMBUFLEN - 1; p++) {
                    *di++ = *p;
                    n++;
                }
                continue;
            }
            *di++ = c;
            if (c == '\r') break;
            n++;
        }
        combuf[1] = n;
    }
    if (echoflag & 1) {
        if (!nullflag) crlf2();
        nullflag = 0;
        print_prompt();
        const char *p = (const char *)combuf + 2;
        outn(1, p, combuf[1]);
        jump_docom();
    }
    nullflag = 0;
    jump_docom1();
}
