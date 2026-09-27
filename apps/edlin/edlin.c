/*
 * EDLIN.COM for ARM-DOS - the MS-DOS 4.00 line editor, ported to ARM.
 *
 * A routine-by-routine C port of Microsoft's MS-DOS 4.00 EDLIN
 * (CMD/EDLIN/EDLIN.ASM, EDLCMD1.ASM, EDLCMD2.ASM, EDLMES.ASM, EDLPARSE.ASM):
 *   Copyright (c) Microsoft Corporation (MS-DOS 4.0 source, MIT License).
 *   "MS DOS EDLIN UTILITY" "VERSION 4.00 (C) COPYRIGHT 1988 Microsoft".
 * Port for ARM-DOS: Europa Micro Systems.  The names of the routines and
 * variables are the originals (in lower case); the comments point to them.
 *
 * The text lives in one buffer, exactly as in the original:
 *
 *      start:  line 1 CR LF ... line n CR LF  ^Z  <- endtxt ... last
 *
 * `current` is the current line number and `pointer` its address.  Every
 * DOS call goes through dos(); when ^C was typed during it, the INT 23h
 * handler has recorded it and dos() then does what the original's
 * ABORTCOM / ABORTINS / ABORTMERGE did: reset the stack (longjmp) and go
 * back to the command loop.
 */
#include <stdint.h>
#include <string.h>
#include <setjmp.h>
#include "armdos.h"

typedef uint8_t u8;
typedef uint16_t u16;

#define CR      13
#define LF      10
#define EOFCH   0x1A
#define QUOTE   0x16            /* ^V */

unsigned _armdos_xms_kb = ~0u;  /* no XMS heap: the buffer is DOS memory */

/* ------------------------------------------------------------ messages */
/* EDLIN.SKL resolved against USA-MS.MSG */
static const char M_PROMPT[] = "*";
static const char M_BADDRV[] = "Invalid drive or file name\r\n";
static const char M_NDNAME[] = "File name must be specified\r\n";
static const char M_RO[]     = "File is READ-ONLY\r\n";
static const char M_BCREAT[] = "File Creation Error\r\n";
static const char M_TOOMANY[]= "Too many files open\r\n";
static const char M_NOBAK[]  = "Cannot edit .BAK file--rename file\r\n";
static const char M_NODIR[]  = "No room in directory for file\r\n";
static const char M_DSKFUL[] = "Disk full. Edits lost.\r\n";
static const char M_BADCOM[] = "Entry error\r\n";
static const char M_NEWFIL[] = "New file\r\n";
static const char M_NOSUCH[] = "Not found\r\n";
static const char M_ASK[]    = "O.K.? ";
static const char M_TOOLNG[] = "Line too long\r\n";
static const char M_EOF[]    = "End of input file\r\n";
static const char M_QMES[]   = "Abort edit (Y/N)? ";
static const char M_DEST[]   = "Must specify destination line number\r\n";
static const char M_MRGERR[] = "Not enough room to merge the entire file\r\n";
static const char M_CRLF[]   = "\r\n";
static const char M_LF[]     = "\n";
static const char M_CONT[]   = "Continue (Y/N)?";
static const char M_VERS[]   = "Incorrect DOS version\r\n";
/* class 1/2 messages (extended and parse errors) go to STDERR */
static const char M_MEMFUL[] = "Insufficient memory\r\n";
static const char M_FILENM[] = "File not found\r\n";
static const char M_OPTERR[] = "Invalid parameter\r\n";

/* ---------------------------------------------------------------- state */
static u8 *start, *last, *endtxt, *pointer, *three4th;
static u16 current, one4th;
static u16 param[4], paramct;
static u8 fourth, qflg, loadmod, delflg, fnew, haveof, ending, olddat, srchmod, srchflg;
static u8 combuf[132], editbuf[260], txt1[132], txt2[132], arg_buf[264];
static u8 *comline;
static char path_name[132], temp_path[132], mrg_path_name[132];
static int fname_len, ext_ptr;
static int rd_handle = -1, wrt_handle = -1, mrg_handle = -1;
static u16 oldlen, newlen, lstnum, srchcnt, lastlin;
static u8 *lstfnd, *numpos;
static u8 disp_len, disp_width, pg_count, lc_adj, lc_flag, cont;
static unsigned amnt_req;

/* parser registers: SI and AL of the original */
static u8 *si;
static u8 al;

/* findlin results: DX and DI */
static u16 f_dx;
static u8 *f_di;

static jmp_buf cmd_jb;
static volatile int ctrlc;
static int abort_mode;          /* INT 23h target: 0 ABORTCOM, 1 ABORTINS, 2 ABORTMERGE */
enum { JB_COMERR = 1, JB_ABORTCOM = 2, JB_ABORTINS = 3, JB_ABORTMERGE = 4 };

/* ------------------------------------------------------------ DOS calls */

static int dos(struct armregs *r)
{
    int cf = _armdos_int21(r);
    if (ctrlc) {
        ctrlc = 0;
        longjmp(cmd_jb, JB_ABORTCOM + abort_mode);
    }
    return cf;
}

/* ^C: the kernel calls INT 23h with a copy of the interrupted call's
 * registers; "CF clear" re-issues the call.  Make the re-issued call a
 * harmless one and unwind in dos() when it returns. */
static void int23(struct armregs *f)
{
    ctrlc = 1;
    f->r0 = 0x3000;
    f->cpsr &= ~ARM_CPSR_C;
}

static void wr(int h, const void *p, unsigned n)
{
    struct armregs r = { 0 };
    r.r0 = 0x4000; r.r1 = h; r.r2 = n; r.r3 = (uint32_t)p;
    dos(&r);
}

static void pr(const char *s) { wr(1, s, strlen(s)); }
static void pe(const char *s) { wr(2, s, strlen(s)); }
static void crlf(void) { pr(M_CRLF); }
static void lf(void) { pr(M_LF); }

static __attribute__((noreturn)) void dos_exit(int code)
{
    struct armregs r = { 0 };
    r.r0 = 0x4C00 | (code & 0xFF);
    _armdos_int21(&r);
    for (;;) ;
}

static void do_close(int h)
{
    struct armregs r = { 0 };
    if (h < 0) return;
    r.r0 = 0x3E00; r.r1 = h;
    dos(&r);
}

static int do_unlink(const char *p, unsigned *err)
{
    struct armregs r = { 0 };
    r.r0 = 0x4100; r.r3 = (uint32_t)p;
    int cf = dos(&r);
    if (err) *err = r.r0 & 0xFFFF;
    return cf;
}

static void do_rename(const char *from, const char *to)
{
    struct armregs r = { 0 };
    r.r0 = 0x5600; r.r3 = (uint32_t)from; r.r5 = (uint32_t)to;
    dos(&r);
}

/* INT 21h AH=6Ch (extended open), as EXT_OPEN1/2/3.  (The original also
 * sets bit 8 of DX, "don't validate the code page"; left out, see README.) */
static int ext_open(const char *p, unsigned mode, unsigned action, unsigned *err)
{
    struct armregs r = { 0 };
    r.r0 = 0x6C00; r.r1 = mode; r.r2 = 0; r.r3 = action; r.r4 = (uint32_t)p;
    if (dos(&r)) { *err = r.r0 & 0xFFFF; return -1; }
    return r.r0 & 0xFFFF;
}

static unsigned rd(int h, void *buf, unsigned n)
{
    struct armregs r = { 0 };
    r.r0 = 0x3F00; r.r1 = h; r.r2 = n; r.r3 = (uint32_t)buf;
    dos(&r);
    return r.r0 & 0xFFFF;
}

static void get_line(u8 *buf)                   /* AH=0Ah */
{
    struct armregs r = { 0 };
    r.r0 = 0x0A00; r.r3 = (uint32_t)buf;
    dos(&r);
}

static int getkey(void)                         /* AH=01h (the message retriever's DOS_KEYB_INP) */
{
    struct armregs r = { 0 };
    r.r0 = 0x0100;
    dos(&r);
    return r.r0 & 0xFF;
}

/* val_yn: INT 21h AX=6523h -> 0 no, 1 yes, 2 neither */
static int val_yn(int c)
{
    struct armregs r = { 0 };
    r.r0 = 0x6523; r.r2 = 1; r.r3 = c & 0xFF;
    if (dos(&r)) {
        c &= 0xDF;
        return c == 'Y' ? 1 : c == 'N' ? 0 : 2;
    }
    return r.r0 & 0xFFFF;
}

static __attribute__((noreturn)) void xerror(const char *msg)
{
    pr(msg);
    dos_exit(0xFF);
}

static __attribute__((noreturn)) void comerr1(const char *msg)
{
    pr(msg);
    longjmp(cmd_jb, JB_COMERR);
}

static __attribute__((noreturn)) void comerr(void) { comerr1(M_BADCOM); }

static __attribute__((noreturn)) void memerr(void)
{
    pe(M_MEMFUL);
    longjmp(cmd_jb, JB_COMERR);
}

/* --------------------------------------------------------- the buffer */

/* SCANLN: count lines (LFs) from *di over *cx bytes until dx == bx.
 * Returns ZF (1 = the line was reached). */
static int scanln(u16 bx, u16 *pdx, u8 **pdi, unsigned *pcx)
{
    u16 dx = *pdx;
    u8 *di = *pdi;
    unsigned cx = *pcx;
    int z = 0;
    while (cx) {
        while (cx) { cx--; if (*di++ == LF) break; }
        dx++;
        if (bx == dx) { z = 1; break; }
    }
    *pdx = dx; *pdi = di; *pcx = cx;
    return z;
}

/* FINDLIN: BX = line (0 = last line + 1) -> f_dx = line found, f_di = its
 * address; returns 1 if it is the line asked for. */
static int findlin(u16 bx)
{
    u16 dx = current;
    u8 *di = pointer;
    if (bx == dx) goto found;
    if (!(bx > dx || bx == 0)) {
        dx = 1;
        di = start;
        if (bx == dx) goto found;
    }
    {
        unsigned cx = endtxt - di;
        int z = scanln(bx, &dx, &di, &cx);
        f_dx = dx; f_di = di;
        return z;
    }
found:
    f_dx = dx; f_di = di;
    return 1;
}

/* SHOWNUM: "%8u:" and '*' for the current line */
static void shownum(u16 bx)
{
    char b[10];
    unsigned v = bx;
    int i = 8;
    memset(b, ' ', 8);
    do { b[--i] = '0' + v % 10; v /= 10; } while (v && i);
    b[8] = ':';
    b[9] = bx == current ? '*' : ' ';
    wr(1, b, 10);
}

static void chkrange(u16 bx)
{
    if (param[1] && bx > param[1]) comerr();
}

static u8 make_cntrl(u8 c)
{
    if ((c & 0xE0) == 0x40) c &= 0x1F;
    return c;
}

/* unquote: ^V makes the next character a control character; compress the
 * line in place (length byte at s[-1], CR-terminated) */
static void unquote(u8 *s)
{
    u8 *di = s;
    unsigned cx = s[-1];
    while (cx) {
        int found = 0;
        while (cx) { cx--; if (*di++ == QUOTE) { found = 1; break; } }
        if (!found) break;
        *di = make_cntrl(*di);
        memmove(di - 1, di, cx + 1);
        if (cx) cx--;
        s[-1]--;
    }
}

/* LOADBUF: copy the line at si into EDITBUF (the template); returns its length */
static u16 loadbuf(const u8 *p)
{
    u8 *di = editbuf + 2;
    unsigned cx = 255;
    u16 dx = 0xFFFF;
    u8 c;
    do { c = *p++; *di++ = c; dx++; cx--; } while (c != CR && cx);
    editbuf[1] = (u8)dx;
    if (c == CR) return dx;
    do { c = *p++; dx++; } while (c != CR);
    di[-1] = CR;
    return dx;
}

/* REPLACE: cx bytes of new text at s replace dx bytes at di */
static void replace_text(unsigned cx, unsigned dx, const u8 *s, u8 *di)
{
    if (cx != dx) {
        u8 *nend = endtxt - dx + cx;
        if (nend >= last) memerr();
        memmove(di + cx, di + dx, endtxt - (di + dx) + 1);
        endtxt = nend;
    }
    memmove(di, s, cx);
}

/* MOVEFILE: open a hole at di by moving di..endtxt up to end at dest */
static void movefile(u8 *dest, u8 *di, u16 bx)
{
    unsigned cx = endtxt - di + 1;
    memmove(dest - cx + 1, di, cx);
    pointer = di;
    current = bx;
    endtxt = dest - cx;
}

/* END_INS: close the hole opened by MOVEFILE */
static void end_ins(void)
{
    u8 *bp = endtxt;
    unsigned cx = last - bp;
    memmove(pointer, bp + 1, cx);
    endtxt = pointer + cx - 1;
    abort_mode = 0;
}

/* -------------------------------------------------------------- display */

static void pg_prompt(void)                     /* EDLIN_PG_PROMPT */
{
    for (;;) {
        pr(M_CONT);
        int c = getkey();
        crlf();
        int y = val_yn(c);
        if (y == 1) { cont = 1; return; }
        if (y == 0) { cont = 0; return; }
    }
}

/* DISPLAY: n lines from p, numbered from bx; returns the last line shown */
static u16 display(u16 bx, u8 *p, u16 n)
{
    unsigned cx = endtxt - p;
    if (!cx) return bx;
    pg_count = disp_len;
    u16 dx = n;
    for (;;) {
        shownum(bx);
        u8 *d = arg_buf, c;
        do {
            c = *p++;
            cx--;
            if (d < arg_buf + 254) {
                if (c >= ' ' || c == LF || c == CR || c == 9) *d++ = c;
                else { *d++ = '^'; *d++ = c | 0x40; }
            }
        } while (c != LF && cx);
        if (d[-1] != LF) {
            if (d[-1] != CR) *d++ = CR;
            *d++ = LF;
        }
        *d = 0;
        /* EDLIN_DISP_COUNT */
        unsigned len = (d - arg_buf) + 10;
        lc_adj = len / disp_width + (len % disp_width ? 1 : 0);
        pr((char *)arg_buf);
        if (!cx) return bx;
        bx++;
        /* EDLIN_PG_COUNT */
        lc_flag = 1;
        if (pg_count <= lc_adj) pg_count = 0; else pg_count -= lc_adj;
        if (--dx) {
            if (pg_count == 0) {
                pg_prompt();
                if (cont) pg_count = disp_len; else lc_flag = 0;
            }
        } else lc_flag = 0;
        if (!lc_flag) return bx - 1;
    }
}

static int query(void)                          /* 1 = yes (ZF) */
{
    if (!qflg) return 1;
    for (;;) {
        pr(M_ASK);
        int c = getkey();
        crlf();
        if (c == CR) return 1;
        int y = val_yn(c);
        if (y == 1) return 1;
        if (y == 0) return 0;
    }
}

/* ---------------------------------------------------------- file I/O */

static void bad_read(void)
{
    pr("Read error in:\r\n");
    pr(path_name);
    xerror(M_CRLF);
}

static void check_end(u8 *di, unsigned *pcx)
{
    u8 *p = di + *pcx - 1;
    if (p == start || *p != LF) {
        p[1] = CR;
        p[2] = LF;
        *pcx += 2;
    }
}

/* SCANEOF: returns 1 (ZF) if the end of the file is in this chunk */
static int scaneof(u8 *di, unsigned *pcx)
{
    unsigned cx = *pcx;
    int z;
    if (loadmod) {
        if (cx >= amnt_req) return 0;
        if (cx && di[cx - 1] == EOFCH) cx--;
        z = 1;
    } else if (!cx) {
        z = 1;
    } else {
        unsigned i;
        for (i = 0; i < cx; i++) if (di[i] == EOFCH) break;
        z = i < cx;
        if (z) cx = i;
    }
    if (z) check_end(di, &cx);
    *pcx = cx;
    return z;
}

/* APPEND (and the initial load) */
static void append_cmd(void)
{
    if (paramct != 1) comerr();
    if (haveof) { pr(M_EOF); return; }
    if (!param[0] && endtxt >= three4th) return;
    u8 *di = endtxt;
    unsigned cx = last - endtxt;
    if (!cx) memerr();
    amnt_req = cx;
    unsigned ax = rd(rd_handle, endtxt, cx);
    if (ax != cx) {
        if (rd(rd_handle, endtxt + ax, 1) == 0) haveof = 1;
        else ax++;
    }
    cx = ax;
    unsigned pushed = cx;
    if (scaneof(di, &cx)) haveof = 1;
    u16 dx = 0, bx = param[0];
    if (!bx) {
        u8 *e = di + cx;
        if (e > three4th) { di = three4th; cx = e - di; bx = 1; }
    }
    scanln(bx, &dx, &di, &cx);
    if (di[-1] != LF) {
        if (haveof == 1) {
            di[0] = CR; di[1] = LF;
            di += 2;
            pushed += 2;
        } else {
            u8 *p = di - 1;             /* DoBackScan: drop the partial line */
            while (p >= start && *p != LF) p--;
            di = p + 1;
            dx--;
        }
    }
    di[0] = EOFCH; di[1] = 0;
    u16 unused = (u16)((endtxt + pushed) - di);
    endtxt = di;
    if (unused) {
        struct armregs r = { 0 };
        r.r0 = 0x4201; r.r1 = rd_handle; r.r2 = 0xFFFF; r.r3 = (u16)-unused;
        if (dos(&r)) bad_read();
    }
    if (bx == dx) { haveof = 0; return; }
    if (haveof) { pr(M_EOF); return; }
    if (ending) return;
    memerr();
}

/* DELBAK: delete the old backup as soon as the first write happens */
static void delbak(void)
{
    unsigned err;
    delflg = 1;
    strcpy(temp_path + ext_ptr, ".BAK");
    if (do_unlink(temp_path, &err) && err == 5) {
        do_close(wrt_handle);
        strcpy(temp_path + ext_ptr, ".$$$");
        do_unlink(temp_path, 0);
        strcpy(temp_path + ext_ptr, ".BAK");
        pe("Access denied  - ");
        pe(temp_path);
        pe(M_CRLF);
        dos_exit(0xFF);
    }
    strcpy(temp_path + ext_ptr, ".$$$");
}

static void wrtadd(u8 *di)
{
    if (!delflg) delbak();
    unsigned cx = di - start;
    if (!cx) return;
    struct armregs r = { 0 };
    r.r0 = 0x4000; r.r1 = wrt_handle; r.r2 = cx; r.r3 = (uint32_t)start;
    if (dos(&r) || (r.r0 & 0xFFFF) != cx) {
        do_close(wrt_handle);
        xerror(M_DSKFUL);
    }
    unsigned rest = endtxt - di;
    memmove(start, di, rest + 1);
    pointer = start;
    endtxt = start + rest;
    current = 1;
}

static void wrt(u16 bx)
{
    findlin((u16)(bx + 1));
    wrtadd(f_di);
}

static void ewrite(void)
{
    if (paramct > 1) comerr();
    u16 bx = param[0];
    if (bx) { wrt(bx); return; }
    if ((unsigned)(endtxt - start) <= one4th) return;
    u8 *di = endtxt - one4th;
    unsigned cx = one4th;
    u16 dx = 0;
    scanln(1, &dx, &di, &cx);
    wrtadd(di);
}

/* ------------------------------------------------------------ commands */

static void kill_bl(void)
{
    do al = *si++; while (al == 9 || al == LF || al == ' ');
}

/* GETNUM: one argument: nnn, +nnn, -nnn, ., # */
static u16 getnum(int idx)
{
    kill_bl();
    if (idx == 3) fourth = 1;
    if (al == '.') {
        if (fourth) comerr();
        al = *si++;
        return current;
    }
    if (al == '#') {
        if (fourth) comerr();
        u16 dx = 1;
        for (u8 *p = start; p < endtxt; p++) if (*p == LF) dx++;
        al = *si++;
        return dx;
    }
    if (al == '+') {
        if (fourth) comerr();
        return (u16)(getnum(idx) + current);
    }
    if (al == '-') {
        if (fourth) comerr();
        u16 dx = getnum(idx);
        return current > dx ? (u16)(current - dx) : 1;
    }
    u16 dx = 0;
    int seen = 0;
    while (al >= '0' && al <= '9') {
        if (dx >= 6553) comerr();
        seen = 1;
        dx = dx * 10 + (al - '0');
        al = *si++;
    }
    if (!seen) return 0;
    if (!dx) comerr();
    return dx;
}

/* NOCOM: edit one line */
static void nocom(void)
{
    if (paramct >= 2) comerr();
    comline--;
    u16 bx = param[0];
    if (!bx) { bx = current + 1; chkrange(bx); }
    int z = findlin(bx);
    current = f_dx;
    pointer = f_di;
    if (!z || pointer == endtxt) return;
    oldlen = loadbuf(pointer);
    display(bx, pointer, 1);
    shownum(bx);
    get_line(editbuf);
    lf();
    if (!editbuf[1]) return;
    unquote(editbuf + 2);
    replace_text(editbuf[1], oldlen, editbuf + 2, pointer);
}

static void delete_cmd(void)
{
    if (paramct > 2) comerr();
    if (!param[0]) param[0] = current;
    if (!param[1]) param[1] = param[0];
    u16 bx = param[0];
    chkrange(bx);
    if (!findlin(bx)) return;
    u8 *first = f_di;
    findlin((u16)(param[1] + 1));
    u8 *s = f_di;
    current = bx;
    pointer = first;
    unsigned n = endtxt - s;
    memmove(first, s, n + 1);
    endtxt = first + n;
}

static void list_cmd(void)
{
    if (paramct > 2) comerr();
    u16 bx = param[0];
    if (!bx) bx = current > 11 ? current - 11 : 1;
    if (!findlin(bx)) return;
    u8 *p = f_di;
    u16 n = (u16)(param[1] + 1 - bx);
    if (!((u16)(param[1] + 1) > bx)) n = disp_len;
    display(bx, p, n);
}

static void pager(void)
{
    if (paramct > 2) comerr();
    findlin(0);
    lastlin = f_dx;
    u16 bx = param[0];
    if (!bx) { bx = current; if (bx != 1) bx++; }
    if (bx > lastlin) return;
    u16 dx = param[1];
    if (!dx) dx = bx + (u16)(disp_len - 2);
    dx++;
    if (dx > lastlin) dx = lastlin;
    if (dx == bx) return;
    if (dx < bx) comerr();
    findlin(dx - 1);
    pointer = f_di;
    current = f_dx;
    findlin(bx);
    display(bx, f_di, dx - bx);
}

static void insert(void)
{
    if (paramct > 1) comerr();
    abort_mode = 1;
    u16 bx = param[0];
    if (!bx) { bx = current; chkrange(bx); }
    findlin(bx);
    bx = f_dx;
    movefile(last, f_di, bx);
    u8 *di = pointer, *bp = endtxt;
    for (;;) {
        pointer = di; current = bx; endtxt = bp;        /* SETPTS */
        shownum(bx);
        get_line(editbuf);
        lf();
        u8 *s = editbuf + 2;
        if (*s == EOFCH) { end_ins(); return; }
        unquote(s);
        unsigned cx = s[-1] + 1u;
        if (di + cx >= bp) { end_ins(); memerr(); }
        memcpy(di, s, cx);
        di += cx;
        *di++ = LF;
        bx++;
    }
}

/* BLKMOVE: MOVE (movflg 1) and COPY (0) */
static void blkmove(int movflg)
{
    u8 *ptr_1, *ptr_2, *ptr_3;
    if (!param[2]) comerr1(M_DEST);
    u16 bx = param[0];
    if (!bx) { bx = current; chkrange(bx); param[0] = bx; }
    if (!findlin(bx)) comerr();
    ptr_1 = f_di;
    bx = param[1];
    if (!bx) { bx = current; param[1] = bx; }
    if (!findlin(bx)) comerr();
    findlin((u16)(param[1] + 1));
    ptr_2 = f_di;
    if (param[0] > param[1]) comerr();
    if (!(param[2] <= param[0] || param[2] > param[1])) comerr();
    unsigned copylen = ptr_2 - ptr_1;
    uint32_t copysiz = copylen;
    if (param[3]) {
        copysiz = (uint32_t)param[3] * copylen;
        if (copysiz > 0xFFFF) memerr();
    }
    if ((uint32_t)(last - endtxt) < copysiz) memerr();
    findlin(param[2]);
    ptr_3 = f_di;
    memmove(ptr_3 + copysiz, ptr_3, endtxt - ptr_3 + 1);
    endtxt += copysiz;
    if (!(ptr_3 > ptr_1)) { ptr_1 += copysiz; ptr_2 += copysiz; }
    int count = (int16_t)param[3];
    u8 *di = ptr_3;
    do {
        memmove(di, ptr_1, copylen);
        di += copylen;
    } while (--count > 0);
    if (movflg) {
        unsigned n = endtxt - ptr_2;
        memmove(ptr_1, ptr_2, n);
        ptr_1[n] = EOFCH;
        endtxt = ptr_1 + n;
        if (param[2] > param[0]) param[2] = param[2] + param[0] - param[1] - 1;
    }
    bx = param[2];
    findlin(bx);
    pointer = f_di;
    current = bx;
}

static void move_cmd(void) { if (paramct != 3) comerr(); blkmove(1); }
static void copy_cmd(void) { if (paramct < 3) comerr(); blkmove(0); }

/* MERGE (T): read a file in before a line */
static void merge(void)
{
    if (paramct != 1) comerr();
    kill_bl();
    si--;
    char *d = mrg_path_name;
    for (;;) {
        al = *si++;
        if (al == ' ' || al == 9 || al == CR || al == ';') break;
        *d++ = al;
    }
    *d = 0;
    si--;
    comline = si;
    unsigned err;
    int h = ext_open(mrg_path_name, 0x0080, 0x0001, &err);
    if (h < 0) {
        if (err == 2) { pe(M_FILENM); longjmp(cmd_jb, JB_COMERR); }
        comerr1(M_BADDRV);
    }
    mrg_handle = h;
    abort_mode = 2;
    u16 bx = param[0];
    if (!bx) { bx = current; chkrange(bx); }
    findlin(bx);
    movefile(last, f_di, f_dx);
    unsigned want = endtxt - pointer;
    unsigned got = rd(mrg_handle, pointer, want);
    u8 *c;
    if (want > got) {
        c = pointer + got;
        if (c[-1] == EOFCH) c--;
    } else {
        pr(M_MRGERR);
        c = pointer;
    }
    u8 *s = endtxt + 1;
    unsigned n = last - s + 1;
    memmove(c, s, n);
    endtxt = c + n - 1;
    do_close(mrg_handle);
}

/* GETTEXT: copy command-line text up to ^Z or CR into a TXT buffer (length
 * at buf[0]); returns the terminator */
static u8 get_text(u8 *buf, unsigned *pcx)
{
    u8 *di = buf + 1;
    unsigned cx = 0;
    for (;;) {
        al = *si++;
        if (al == QUOTE) {
            al = make_cntrl(*si++);
        } else if (al == EOFCH) break;
        if (al == CR) break;
        *di++ = al;
        cx++;
    }
    if (cx) buf[0] = cx;
    else if (olddat == 1) cx = buf[0];
    else buf[0] = 0;
    *pcx = cx;
    return al;
}

/* FNDNEXT: returns 1 (ZF) on a match */
static int fndnext(void)
{
    u8 first = txt1[1];
    unsigned cx = srchcnt;
    u8 *di = lstfnd;
    for (;;) {
        int found = 0;
        while (cx) { cx--; if (*di++ == first) { found = 1; break; } }
        if (!found) return 0;
        unsigned k = oldlen - 1, i;
        u8 *lim = last + 1;
        for (i = 0; i < k; i++) if (di + i >= lim || di[i] != txt1[2 + i]) break;
        if (i == k) break;
    }
    srchcnt = cx;
    lstfnd = di;
    /* determine the line number of the match */
    u8 *p = numpos, *bx = p;
    unsigned c = lstfnd - numpos;
    u16 dx = lstnum;
    for (;;) {
        dx++;
        bx = p;
        int z = 0;
        while (c) { c--; if (*p++ == LF) { z = 1; break; } }
        if (!z) break;
    }
    dx--;
    lstnum = dx;
    numpos = bx;
    return 1;
}

/* FNDFIRST: returns 1 (ZF) on a match */
static int fndfirst(void)
{
    unsigned cx;
    olddat = 1;
    u8 t = get_text(txt1, &cx);
    if (!cx) return 0;
    if (t == EOFCH) olddat = 0;
    oldlen = cx;
    if (t == CR || srchflg) si--;
    comline = si;
    t = get_text(txt2, &cx);
    if (!srchflg) {
        if (t == CR) si--;
        comline = si;
    }
    newlen = cx;
    u16 bx = param[0];
    if (!bx) {
        bx = srchmod ? current + 1 : 1;
        chkrange(bx);
    }
    findlin(bx);
    lstfnd = numpos = f_di;
    lstnum = f_dx;
    bx = param[1] ? (u16)(param[1] + 1) : 0;
    findlin(bx);
    unsigned n = f_di - lstfnd;
    if (!n || n < oldlen) return 0;
    srchcnt = n;
    return fndnext();
}

static void putcursor(u16 bx)
{
    findlin(bx);
    current = f_dx;
    pointer = f_di;
}

static void search_cmd(void)
{
    if (paramct > 2) comerr();
    srchmod = 1;
    srchflg = 1;
    if (!fndfirst()) { pr(M_NOSUCH); return; }
    for (;;) {
        display(lstnum, numpos, 1);
        u8 *di = lstfnd;
        unsigned cx = srchcnt;
        int z = 0;
        while (cx) { cx--; if (*di++ == LF) { z = 1; break; } }
        if (!z) { pr(M_NOSUCH); return; }
        lstfnd = numpos = di;
        srchcnt = cx;
        lstnum++;
        if (query()) { putcursor(lstnum - 1); return; }
        if (!fndnext()) { pr(M_NOSUCH); return; }
    }
}

static void replace_cmd(void)
{
    if (paramct > 2) comerr();
    srchmod = 1;
    srchflg = 0;
    if (!fndfirst()) { pr(M_NOSUCH); return; }
    do {
        u16 dx = loadbuf(numpos);
        dx = dx - oldlen + newlen;
        if (dx > 254) { pr(M_TOOLNG); return; }
        shownum(lstnum);
        u8 *d = arg_buf, *s = numpos;
        unsigned n = lstfnd - numpos - 1;
        while (n--) { *d++ = *s++; dx--; }
        for (unsigned i = 0; i < newlen; i++) { *d++ = txt2[1 + i]; dx--; }
        s += oldlen;
        n = (u16)(dx + 2);
        while (n--) *d++ = *s++;
        *d = 0;
        pr((char *)arg_buf);
        if (query()) {
            putcursor(lstnum);
            u8 *di = lstfnd - 1;
            lstfnd += newlen - 1;
            u16 k = oldlen - 1;
            srchcnt = srchcnt >= k ? srchcnt - k : 0;
            replace_text(newlen, oldlen, txt2 + 1, di);
        }
    } while (fndnext());
}

static void quit_cmd(void)
{
    if (paramct != 1 || param[0]) comerr();
    for (;;) {
        pr(M_QMES);
        int y = val_yn(getkey());
        if (y == 1) {
            do_close(wrt_handle);
            do_unlink(temp_path, 0);
            dos_exit(0);
        }
        crlf();
        if (y == 0) return;
    }
}

static void ended(void)
{
    for (;;) {
        if (paramct != 1 || param[0]) comerr();
        ending = 1;
        wrt(0xFFFF);
        if (haveof) break;
        param[0] = 0xFFFF;
        paramct = 1;
        append_cmd();
        param[0] = 0;
    }
    wr(wrt_handle, endtxt, 1);                  /* the ^Z */
    do_close(rd_handle);
    do_close(wrt_handle);
    strcpy(temp_path + ext_ptr, ".BAK");
    do_rename(path_name, temp_path);
    strcpy(temp_path + ext_ptr, ".$$$");
    do_rename(temp_path, path_name);
    dos_exit(0);
}

static void append_a(void) { append_cmd(); }

static const char comtab[] = "\r;ACDEILMPQRSTW";
static void (*const table[])(void) = {
    nocom, nocom, append_a, copy_cmd, delete_cmd, ended, insert, list_cmd,
    move_cmd, pager, quit_cmd, replace_cmd, search_cmd, merge, ewrite,
};

/* ----------------------------------------------------- the command loop */

static __attribute__((noreturn)) void command_loop(void)
{
    int r = setjmp(cmd_jb);
    if (r == JB_ABORTCOM) crlf();
    else if (r == JB_ABORTINS || r == JB_ABORTMERGE) {
        crlf();
        end_ins();
        goto comover;
    }
    for (;;) {
        abort_mode = 0;
        {
            struct armregs v = { 0 };
            v.r0 = 0x2523; v.r3 = (uint32_t)int23;
            _armdos_int21(&v);
        }
        pr(M_PROMPT);
        get_line(combuf);
        comline = combuf + 2;
        lf();
    parse:
        param[1] = param[2] = param[3] = 0;
        fourth = 0;
        qflg = 0;
        si = comline;
        {
            int di = 0;
            for (;;) {
                u16 dx = getnum(di);
                if (di < 4) param[di] = dx;
                di++;
                paramct = di;
                si--;
                kill_bl();
                if (al != ',') break;
            }
        }
        si--;
        kill_bl();
        if (al == '?') { qflg = 1; kill_bl(); }
        if (al > 0x5F && al <= 'z') al &= 0x5F;
        {
            const char *c = memchr(comtab, al, sizeof comtab - 1);
            if (!c) comerr();
            if (param[1] && param[1] < param[0]) comerr();
            comline = si;
            table[c - comtab]();
        }
    comover:
        si = comline;
        kill_bl();
        if (al == CR) continue;
        if (al != EOFCH && al != ';') si--;
        comline = si;
        goto parse;
    }
}

/* ---------------------------------------------------------- start-up */

static int is_delim(u8 c) { return c == ' ' || c == 9 || c == ',' || c == ';' || c == '='; }

/* PARSER_COMMAND: filespec (required) and /B */
static int parse_command(void)
{
    const u8 *p = _armdos_psp->cmdtail + 1;
    const u8 *e = p + _armdos_psp->cmdtail[0];
    int files = 0, sw = 0;
    for (;;) {
        while (p < e && is_delim(*p)) p++;
        if (p >= e || *p == CR) break;
        if (*p == '/') {
            const u8 *q = p + 1;
            while (q < e && !is_delim(*q) && *q != '/' && *q != CR) q++;
            if (q - p != 2 || (p[1] & 0xDF) != 'B') return 3;
            if (sw) return 1;
            sw = 1;
            p = q;
        } else {
            if (files) return 1;
            files = 1;
            while (p < e && !is_delim(*p) && *p != '/' && *p != CR) {
                if (fname_len < (int)sizeof path_name - 6) {
                    u8 c = *p;
                    if (c >= 'a' && c <= 'z') c -= 32;
                    path_name[fname_len++] = c;
                }
                p++;
            }
        }
    }
    if (!files) return 2;
    /* /B is accepted, but in 4.00 it has no effect: EDLPARSE's val_sw
     * compares the parser's synonym pointer with the wrong address and so
     * never sets parse_switch_b (verified with the genuine EDLIN).  To
     * make /B work, set loadmod = sw here. */
    return 0;
}

int main(void)
{
    struct armregs r;

    /* EDLIN_DISP_GET: screen size from IOCTL 440Ch CX=037Fh, else 25x80 */
    {
        static u8 vb[16] = { 0, 0, 14, 0 };
        memset(&r, 0, sizeof r);
        r.r0 = 0x440C; r.r1 = 1; r.r2 = 0x037F; r.r3 = (uint32_t)vb;
        unsigned rows = 25, cols = 80;
        if (!_armdos_int21(&r)) {
            unsigned w = vb[12] | vb[13] << 8, l = vb[14] | vb[15] << 8;
            if (w >= 20 && w <= 255 && l >= 3 && l <= 255) { rows = l; cols = w; }
        }
        disp_len = rows - 1;
        disp_width = cols;
    }

    /* PRE_LOAD_MESSAGE: the message retriever insists on DOS 4.00 */
    memset(&r, 0, sizeof r);
    r.r0 = 0x3000;
    _armdos_int21(&r);
    if ((r.r0 & 0xFFFF) != 0x0004) { pr(M_VERS); dos_exit(0); }

    /* the drive of the first operand (AL at entry on a PC) */
    {
        static u8 fcb[40];
        memset(&r, 0, sizeof r);
        r.r0 = 0x2901; r.r4 = (uint32_t)(_armdos_psp->cmdtail + 1); r.r5 = (uint32_t)fcb;
        _armdos_int21(&r);
        if ((r.r0 & 0xFF) == 0xFF) xerror(M_BADDRV);
    }

    switch (parse_command()) {
    case 1: case 3: pe(M_OPTERR); dos_exit(0xFF);
    case 2: xerror(M_NDNAME);
    }
    path_name[fname_len] = 0;

    /* .BAK check */
    if (fname_len >= 4 && !memcmp(path_name + fname_len - 4, ".BAK", 4)) xerror(M_NOBAK);

    /* NOTBAK: open the file for read/write */
    unsigned err;
    int h = ext_open(path_name, 0x0082, 0x0001, &err);
    if (h >= 0) {
        rd_handle = h;
    } else if (err == 3) {
        xerror(M_BADDRV);
    } else if (err == 4) {
        xerror(M_TOOMANY);
    } else if (err == 5) {
        memset(&r, 0, sizeof r);
        r.r0 = 0x4300; r.r3 = (uint32_t)path_name;
        if (_armdos_int21(&r) || !(r.r2 & 1)) xerror(M_BCREAT);
        xerror(M_RO);
    } else if (err == 2) {
        /* try to create it, to see whether it could be */
        h = ext_open(path_name, 0x0082, 0x0010, &err);
        if (h < 0) {
            if (err == 5) xerror(M_NODIR);
            xerror(M_BADDRV);
        }
        do_close(h);
        if (do_unlink(path_name, 0)) xerror(M_BCREAT);
        haveof = 0xFF;
        fnew = 1;
    } else {
        xerror(M_BCREAT);
    }

    /* HAVFIL */
    if (fnew) pr(M_NEWFIL);
    memcpy(temp_path, path_name, fname_len + 1);
    ext_ptr = fname_len;
    for (int i = fname_len - 1; i >= 0; i--) if (temp_path[i] == '.') { ext_ptr = i; break; }
    strcpy(temp_path + ext_ptr, ".$$$");

    /* MAKFIL: create the .$$$ file to make sure the directory has room */
    for (;;) {
        h = ext_open(temp_path, 0x0082, 0x0012, &err);
        if (h >= 0) break;
        if (delflg) xerror(M_NODIR);
        delbak();
    }
    wrt_handle = h;

    /* the text buffer: up to 64 KB of DOS memory, as the original's segment */
    memset(&r, 0, sizeof r);
    r.r0 = 0x4800; r.r1 = 0x1000;
    if (_armdos_int21(&r)) {
        unsigned paras = r.r1 & 0xFFFF;
        memset(&r, 0, sizeof r);
        r.r0 = 0x4800; r.r1 = paras;
        if (paras < 0x20 || _armdos_int21(&r)) { pe(M_MEMFUL); dos_exit(0xFF); }
        r.r1 = paras;
    } else r.r1 = 0x1000;
    {
        u8 *blk = (u8 *)((r.r0 & 0xFFFF) << 4);
        unsigned size = r.r1 << 4;
        memset(blk, 0, 16);
        start = blk + 8;                         /* start[-1] = 0, as in the original */
        last = blk + size - 8 - 1;
    }
    if (!fnew) {
        unsigned avail = last - start;
        unsigned half = avail >> 1;
        one4th = half >> 1;
        three4th = start + one4th + half;
    }
    start[0] = EOFCH;
    endtxt = start;
    combuf[0] = 128;
    editbuf[0] = 255;
    pointer = start;
    current = 1;
    paramct = 1;
    param[0] = 0;
    if (!fnew) {
        if (setjmp(cmd_jb) == 0) {
            ending = 1;
            append_cmd();
        }
        ending = 0;
    }
    command_loop();
}
