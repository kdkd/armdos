/*
 * char.c - console and character device I/O: INT 21h AH=01h-0Ch
 * (DOS/CPMIO.ASM, CPMIO2.ASM), the AH=0Ah line editor with DOS's template
 * keys (STRIN.ASM), Ctrl-C/^S/^P handling (CTRLC.ASM) and the handle-level
 * reads/writes of character devices (cooked and raw).
 *
 * Console functions go through handles 0 (STDIN) and 1 (STDOUT) of the
 * current process, so they follow redirection, exactly as DOS does.
 */
#include "dos.h"

uint8_t con_col;                /* CARPOS */
uint8_t printer_echo;           /* PFLAG */
static uint8_t charco;          /* output counter for the ^C check */
static uint8_t scan_flag;       /* the last console byte read was an extended lead */

struct ctx *base_ctx(void);
__attribute__((noreturn)) void restart_base(const struct armregs *regs);

/* ---------------------------------------------------------- IOFUNC */

#define IO_IN       0
#define IO_INSTAT   1
#define IO_OUT      2
#define IO_OUTSTAT  3
#define IO_FLUSH    4

static struct sft *io_sft(int h)
{
    return handle_sft(h, 0);
}

/* a device error during a character I/O: INT 24h (CHARHARD) */
static int char_hard(struct sft *s, int write, int code)
{
    int ah = (write ? 0x87 : 0x86) | 0x38;
    return crit_error(ah, 0, code, (struct devhdr *)s->devptr);
}

/* returns: IO_IN -> char; IO_INSTAT -> char or -1; IO_OUT/OUTSTAT -> 0 ready / -1 busy */
static int iofunc(struct sft *s, int op, int ch)
{
    if (!(s->flags & SF_DEVICE)) {
        uint8_t c;
        switch (op) {
        case IO_IN:
            if (file_read(s, &c, 1) != 1) return 0x1A;
            return c;
        case IO_INSTAT: {
            uint32_t pos = s->position;
            int n = file_read(s, &c, 1);
            s->position = pos;
            return n == 1 ? c : -1;
        }
        case IO_OUT:
            c = ch;
            file_write(s, &c, 1);
            return 0;
        default:
            return 0;
        }
    }
    struct devhdr *d = (struct devhdr *)s->devptr;
    for (;;) {
        union { struct req_rw rw; struct req_ndread nd; struct reqhdr h; } q;
        uint8_t byte = ch;
        memset(&q, 0, sizeof q);
        switch (op) {
        case IO_IN:
        case IO_OUT:
            q.rw.h.len = sizeof q.rw;
            q.rw.h.cmd = op == IO_IN ? CMD_READ : CMD_WRITE;
            q.rw.addr = (uint32_t)&byte;
            q.rw.count = 1;
            break;
        case IO_INSTAT:
            q.nd.h.len = sizeof q.nd;
            q.nd.h.cmd = CMD_NDREAD;
            break;
        case IO_OUTSTAT:
            q.h.len = 13;
            q.h.cmd = CMD_OUTSTAT;
            break;
        case IO_FLUSH:
            q.h.len = 13;
            q.h.cmd = CMD_INFLUSH;
            break;
        }
        int st = devcall(d, &q);
        if (st & RS_ERROR) {
            int r = char_hard(s, op == IO_OUT, st & 0xFF);
            if (r == 1) continue;
            /* ignore / fail: pretend the device is ready */
            return op == IO_INSTAT ? -1 : 0;
        }
        switch (op) {
        case IO_IN: return byte;
        case IO_INSTAT: return (st & RS_BUSY) ? -1 : q.nd.ch;
        case IO_OUTSTAT: return (st & RS_BUSY) ? -1 : 0;
        default: return 0;
        }
    }
}

/* ------------------------------------------------------------ output */

static void rawout_sft(struct sft *s, int c)
{
    if ((s->flags & (SF_DEVICE | SF_REMOTE)) == SF_DEVICE &&
        (((struct devhdr *)s->devptr)->attr & DEVA_SPECIAL)) {
        struct armregs r = { 0 };
        r.r0 = c & 0xFF;
        kint(0x29, &r);
        return;
    }
    iofunc(s, IO_OUT, c);
}

static void rawout(int c)
{
    struct sft *s = io_sft(1);
    if (s) rawout_sft(s, c);
}

static void outch(int c)
{
    if ((++charco & 63) == 0) stat_check();
    rawout(c);
    if (printer_echo) {
        struct sft *o = io_sft(1);
        if (o && (o->flags & SF_DEVICE)) {
            struct sft *p = io_sft(4);
            if (p) iofunc(p, IO_OUT, c);
        }
    }
}

void con_out(int c)
{
    c &= 0xFF;
    if (c < 0x20) {
        if (c == '\r') con_col = 0;
        else if (c == '\b') con_col--;
        else if (c == '\t') {
            int n = 8 - (con_col & 7);
            while (n--) con_out(' ');
            return;
        }
    } else if (c != 0x7F) con_col++;
    outch(c);
}

void con_puts(const char *s) { while (*s) con_out(*s++); }
void con_crlf(void) { con_out('\r'); con_out('\n'); }

/* echo a control character as ^X (BUFOUT) */
static void bufout(int c)
{
    if (c >= ' ' || c == '\t' || c == ('U' - '@') || c == ('T' - '@')) { con_out(c); return; }
    con_out('^');
    con_out(c | 0x40);
}

/* messages straight to the console device (DOS's OutMes to BCON) */
void bcon_write(const char *s, int n)
{
    struct devhdr *d = LOL.con;
    struct req_rw q;
    memset(&q, 0, sizeof q);
    q.h.len = sizeof q;
    q.h.cmd = CMD_WRITE;
    q.addr = (uint32_t)s;
    q.count = n;
    devcall(d, &q);
    for (int i = 0; i < n; i++) {
        if (s[i] == '\r') con_col = 0;
        else if ((uint8_t)s[i] >= ' ') con_col++;
    }
}

/* ----------------------------------------------------------- Ctrl-C */

__attribute__((noreturn)) void ctrl_c_abort(void)
{
    struct ctx *b = base_ctx();
    bufout(3);
    con_crlf();
    struct armregs h;
    memcpy(&h, &b->orig, sizeof h);
    h.cpsr &= ~CPSR_C;
    DV.indos = 0;
    DV.errormode = 0;
    int_handler hd = (int_handler)IVT[0x23];
    h.intno = 0x23;
    if (hd) hd(&h); else h.cpsr |= CPSR_C;
    if (h.cpsr & CPSR_C) {
        DV.indos = 1;
        terminate(b->f, 0, 1);
        abort_to_base();
    }
    restart_base(&h);
}

static int ctrl_c_allowed(void)
{
    return DV.indos == 1 && !DV.errormode && cur_ctx;
}

/* DSKSTATCHK: ^C typed at the console, even with STDIN redirected */
void check_ctrl_c(void)
{
    if (!ctrl_c_allowed()) return;
    struct devhdr *d = LOL.con;
    struct req_ndread q;
    memset(&q, 0, sizeof q);
    q.h.len = sizeof q;
    q.h.cmd = CMD_NDREAD;
    devcall(d, &q);
    if ((q.h.status & RS_BUSY) || q.ch != 3) return;
    struct req_rw r;
    uint8_t c;
    memset(&r, 0, sizeof r);
    r.h.len = sizeof r;
    r.h.cmd = CMD_READ;
    r.addr = (uint32_t)&c;
    r.count = 1;
    devcall(d, &r);                     /* eat the ^C */
    ctrl_c_abort();
}

static int idle_ok(void)
{
    return cur_ctx && cur_ctx->fn >= 1 && cur_ctx->fn <= 0x0C;
}

/* STATCHK: look at STDIN for ^C, ^S (pause) and ^P (printer echo) */
void stat_check(void)
{
    check_ctrl_c();
    struct sft *s = io_sft(0);
    if (!s) return;
    int c = iofunc(s, IO_INSTAT, 0);
    if (c < 0) { if (idle_ok()) dos_idle(); return; }
    if (c == ('S' - '@') && !scan_flag) {
        iofunc(s, IO_IN, 0);            /* eat ^S, wait for any key */
        for (;;) {
            c = iofunc(s, IO_INSTAT, 0);
            if (c >= 0) break;
            if (idle_ok()) dos_idle();
            irq_off();
            if (iofunc(s, IO_INSTAT, 0) < 0) cpu_wfi();
            irq_on();
        }
    } else if (!((c == ('P' - '@') || c == ('N' - '@')) && !scan_flag) && c != ('C' - '@')) {
        return;
    } else if (c == ('C' - '@') && !ctrl_c_allowed()) {
        return;         /* inside INT 24h: the ^C is the caller's to read (AL=03h) */
    }
    c = iofunc(s, IO_IN, 0);
    if (c == ('P' - '@') || c == ('N' - '@')) { printer_echo = !printer_echo; return; }
    if (c == ('C' - '@') && ctrl_c_allowed()) ctrl_c_abort();
}

/* ------------------------------------------------------------- input */

static uint8_t flushed_this_wait;

/* wait until STDIN has a character (sleeping in WFI), with or without
   the STATCHK processing */
static void wait_input(struct sft *s, int check)
{
    flushed_this_wait = 0;
    for (;;) {
        if (check) stat_check();
        if (iofunc(s, IO_INSTAT, 0) >= 0) return;
        if (!(s->flags & SF_DEVICE)) return;    /* files are always "ready" */
        if (!flushed_this_wait && !DV.errormode) {
            /* nothing to do while the user thinks: get the disk up to date */
            flushed_this_wait = 1;
            if (bufs_dirty(-1)) flush_bufs(-1);
        }
        if (!check && idle_ok()) dos_idle();
        irq_off();
        if (iofunc(s, IO_INSTAT, 0) < 0) cpu_wfi();
        irq_on();
    }
}

int con_in_noecho(void)                 /* AH=08h */
{
    struct sft *s = io_sft(0);
    if (!s) return 0;
    wait_input(s, 1);
    int c = iofunc(s, IO_IN, 0);
    scan_flag = c == 0;
    return c;
}

int con_in_raw(void)                    /* AH=07h */
{
    struct sft *s = io_sft(0);
    if (!s) return 0;
    wait_input(s, 0);
    return iofunc(s, IO_IN, 0);
}

/* ------------------------------------------------ AH=0Ah line editor */

static int line_width(const uint8_t *p, int n, int start)
{
    int col = start;
    for (int i = 0; i < n; i++) {
        uint8_t c = p[i];
        if (c == '\t') col = (col | 7) + 1;
        else if (c < ' ' && c != ('U' - '@') && c != ('T' - '@')) col += 2;
        else col++;
    }
    return col;
}

static void backmes(void) { con_out('\b'); con_out(' '); con_out('\b'); }

void con_string_input(uint8_t *ubuf)
{
    unsigned max = ubuf[0];
    if (!max) return;
    unsigned tlen = ubuf[1];
    uint8_t *tmpl = ubuf + 2;
    if (!(max > tlen && tmpl[tlen] == '\r')) tlen = 0;
    unsigned dl = max - 1;
    uint8_t inbuf[256];
    unsigned ti, n, ins, startpos;
    int c;

newlin:
    startpos = con_col;
    ti = 0; n = 0; ins = 0;
    c = con_in_noecho();
    if (c == '\n') goto getch;
    goto gotch;

getch:
    c = con_in_noecho();
gotch:
    if (c == ('F' - '@')) goto getch;
    if (c == 0) {                       /* function key lead byte */
        c = con_in_noecho();
        switch (c) {
        case 59: case 77:               /* F1, right arrow: copy one */
            c = 1; goto copyeach;
        case 61:                        /* F3: copy the rest */
            c = tlen - ti; goto copyeach;
        case 60: case 62: {             /* F2 / F4 x: copy / skip up to x */
            int k = con_in_noecho();
            if (k == 0) { con_in_noecho(); goto getch; }
            int cl = (int)tlen - (int)ti;
            if (cl <= 1) goto getch;
            int p;
            for (p = ti + 1; p < (int)tlen; p++) if (tmpl[p] == k) break;
            if (p >= (int)tlen) goto getch;
            int dist = p - ti;
            if (c == 62) { ti += dist; goto getch; }
            c = dist; goto copyeach;
        }
        case 83:                        /* Del: skip one */
            if (ti < tlen) ti++;
            goto getch;
        case 63:                        /* F5: re-edit - the line becomes the template */
            con_out('@');
            memcpy(tmpl, inbuf, n);
            tlen = n;
            goto putnew;
        case 75:                        /* left arrow: backspace */
            goto backsp;
        case 82:                        /* Ins */
            ins = !ins;
            goto getch;
        case 64:                        /* F6: ^Z */
            c = 0x1A; goto savch;
        case 65:                        /* F7: a real 00h */
            c = 0; goto savch;
        default:
            goto getch;
        }
    }
    if (c == 0x7F || c == '\b') goto backsp;
    if (c == '\r') goto endlin;
    if (c == '\n') { con_crlf(); goto getch; }
    if (c == 0x1B) goto kilnew;         /* Esc: cancel the line */

savch:
    if (n >= dl) { con_out(7); goto getch; }
    inbuf[n++] = c;
    bufout(c);
    if (!ins && ti < tlen) ti++;
    goto getch;

copyeach:
    ins = 0;
    while (c-- > 0) {
        if (n >= dl || ti >= tlen) break;
        uint8_t k = tmpl[ti++];
        inbuf[n++] = k;
        bufout(k);
    }
    goto getch;

backsp:
    if (n > 0) {
        n--;
        uint8_t k = inbuf[n];
        backmes();
        if (k == '\t') {
            int before = line_width(inbuf, n, startpos);
            int width = ((before | 7) + 1) - before;
            while (--width > 0) backmes();
        } else if (k < ' ' && k != ('U' - '@') && k != ('T' - '@')) {
            backmes();
        }
        if (!ins && ti > 0) ti--;
    }
    goto getch;

kilnew:
    con_out('\\');
putnew:
    con_crlf();
    for (unsigned i = 0; i < startpos; i++) con_out(' ');
    goto newlin;

endlin:
    inbuf[n] = '\r';
    con_out('\r');
    ubuf[1] = n;
    memcpy(ubuf + 2, inbuf, n + 1);
}

/* ------------------------------------------- handle I/O of devices */

static uint8_t conbuf[132] = { 128, 0 };
static int conpos = -1, conlen;

int dev_read(struct sft *s, uint8_t *buf, unsigned n)
{
    struct devhdr *d = (struct devhdr *)s->devptr;
    if (!n) return 0;
    if (d->attr & DEVA_NUL) return 0;
    if (!(s->flags & DI_NOEOF)) return 0;               /* at EOF */
    if (!(s->flags & DI_RAW) && (d->attr & DEVA_STDIN)) {
        /* cooked console: a line at a time through the editor */
        if (conpos < 0 || conpos >= conlen) {
            conbuf[0] = 128;
            con_string_input(conbuf);
            conlen = conbuf[1] + 2;                     /* text CR LF */
            conbuf[2 + conlen - 1] = '\n';
            con_out('\n');
            conpos = 0;
        }
        unsigned k = 0;
        while (k < n && conpos < conlen) buf[k++] = conbuf[2 + conpos++];
        return k;
    }
    if (!(s->flags & DI_RAW)) {
        /* cooked, not the console: a byte at a time; ^Z is end of file */
        unsigned k = 0;
        while (k < n) {
            int c = iofunc(s, IO_IN, 0);
            if (c == 0x1A) { s->flags &= ~DI_NOEOF; break; }
            buf[k++] = c;
            if (c == '\r') break;
        }
        return k;
    }
    for (;;) {
        struct req_rw q;
        memset(&q, 0, sizeof q);
        q.h.len = sizeof q;
        q.h.cmd = CMD_READ;
        q.addr = (uint32_t)buf;
        q.count = n;
        int st = devcall(d, &q);
        if (!(st & RS_ERROR)) return q.count;
        int r = char_hard(s, 0, st & 0xFF);
        if (r == 1) continue;
        if (r == 0) return q.count;
        return -(0x13 + (st & 0xFF));
    }
}

int dev_write(struct sft *s, const uint8_t *buf, unsigned n)
{
    struct devhdr *d = (struct devhdr *)s->devptr;
    if (!n) return 0;
    if (d->attr & DEVA_NUL) return n;
    if (!(s->flags & DI_RAW) && (d->attr & DEVA_STDOUT)) {
        /* cooked console output: columns, tabs, ^C/^S checks */
        for (unsigned i = 0; i < n; i++) {
            int c = buf[i];
            if (c == 0x1A) return i;
            if (c < 0x20) {
                if (c == '\r') con_col = 0;
                else if (c == '\b') con_col--;
                else if (c == '\t') {
                    int k = 8 - (con_col & 7);
                    while (k--) { con_col++; rawout_sft(s, ' '); }
                    continue;
                }
            } else if (c != 0x7F) con_col++;
            if ((++charco & 63) == 0) stat_check();
            rawout_sft(s, c);
        }
        return n;
    }
    unsigned done = 0;
    if (!(s->flags & DI_RAW)) {
        for (unsigned i = 0; i < n; i++) if (buf[i] == 0x1A) { n = i; break; }
    }
    while (done < n) {
        struct req_rw q;
        memset(&q, 0, sizeof q);
        q.h.len = sizeof q;
        q.h.cmd = CMD_WRITE;
        q.addr = (uint32_t)(buf + done);
        q.count = n - done;
        int st = devcall(d, &q);
        if (!(st & RS_ERROR)) return n;
        done += q.count;
        int r = char_hard(s, 1, st & 0xFF);
        if (r == 1) continue;
        if (r == 0) return n;
        return -(0x13 + (st & 0xFF));
    }
    return n;
}

/* ------------------------------------------------ INT 21h 01h-0Ch */

void char_functions(struct armregs *f)
{
    struct sft *s;
    int c;
    switch (AH(f)) {
    case 0x01:
        c = con_in_noecho();
        if (!(c == 3 && DV.errormode)) con_out(c);  /* the INT 24h handler echoes its own ^C */
        set_al(f, c);
        break;
    case 0x02:
        con_out(DL(f));
        set_al(f, DL(f));
        break;
    case 0x03:
        stat_check();
        s = io_sft(3);
        if (!s) break;
        while (iofunc(s, IO_INSTAT, 0) < 0 && (s->flags & SF_DEVICE)) { dos_idle(); irq_off(); cpu_wfi(); irq_on(); }
        set_al(f, iofunc(s, IO_IN, 0));
        break;
    case 0x04:
    case 0x05:
        stat_check();
        s = io_sft(AH(f) == 4 ? 3 : 4);
        if (s) iofunc(s, IO_OUT, DL(f));
        break;
    case 0x06:
        if (DL(f) == 0xFF) {
            s = io_sft(0);
            if (!s) { set_al(f, 0); set_zf(f, 1); break; }
            c = iofunc(s, IO_INSTAT, 0);
            if (c < 0) { set_zf(f, 1); set_al(f, 0); dos_idle(); break; }
            set_zf(f, 0);
            set_al(f, iofunc(s, IO_IN, 0));
        } else {
            rawout(DL(f));
            set_al(f, DL(f));
        }
        break;
    case 0x07:
        set_al(f, con_in_raw());
        break;
    case 0x08:
        set_al(f, con_in_noecho());
        break;
    case 0x09: {
        const char *p = (const char *)f->r3;
        while (*p != '$') con_out(*p++);
        set_al(f, '$');
        break;
    }
    case 0x0A:
        con_string_input((uint8_t *)f->r3);
        break;
    case 0x0B:
        stat_check();
        s = io_sft(0);
        set_al(f, (s && iofunc(s, IO_INSTAT, 0) >= 0) ? 0xFF : 0x00);
        break;
    case 0x0C: {
        s = io_sft(0);
        if (s) iofunc(s, IO_FLUSH, 0);
        if (s && !(s->flags & SF_DEVICE)) { /* a file: nothing to flush */ }
        int fn = AL(f);
        if (fn == 1 || fn == 6 || fn == 7 || fn == 8 || fn == 0x0A) {
            f->r0 = (fn << 8) | (f->r0 & 0xFF);
            cur_ctx->fn = fn;
            char_functions(f);
        } else set_al(f, 0);
        break;
    }
    }
}
