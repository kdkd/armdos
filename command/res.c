/*
 * res.c - COMMAND.COM's resident part (COMMAND1.ASM, COMMAND2.ASM,
 * RUCODE.ASM): start-up, the INT 22h/23h/24h handlers, EXEC of external
 * commands and the transient loader.
 *
 * The transient part (the command processor: everything else) is an AR1
 * image stored inside COMMAND.COM (tran_blob).  At start-up it is copied to
 * a block at the top of memory and relocated there, and the resident part
 * shrinks to a few KB.  To run a program the resident part frees the
 * transient's block, EXECs, then allocates it again; if the program
 * overwrote it (checksum) the transient is read back from COMSPEC, with DOS
 * 4's prompts when the disk holding COMMAND.COM is not in the drive.
 *
 * Everything here runs either on the resident stack (EXEC, INT 22h) or on
 * the kernel's stack (the INT 23h/24h handlers); all of it must work while
 * the transient part is overwritten.
 */
#include <string.h>
#include <armdos.h>
#include "res.h"

typedef struct armregs REGS;

struct res R;
uint8_t res_stack[1536] __attribute__((aligned(8)));

extern const uint8_t tran_blob[];
extern uint8_t __resident_end[];

extern void lodcom_entry(void);
extern void res_exec_entry(void);
extern __attribute__((noreturn)) void res_call_on_stack(void *fn, void *arg, int reason, void *sp);

#define CRLF "\r\n"

static int d21(REGS *r) { return _armdos_int21(r); }

/* ------------------------------------------------------ messages -- */

static const char *const rmsg_t[] = {
    /* 210 */ "Abort", ", Retry", ", Ignore", ", Fail", "?", "reading", "writing",
    /* 217 */ " %1 drive %2" CRLF, " %1 device %2" CRLF,
    /* 219 */ "Please insert volume %1 serial %2-%3" CRLF,
    /* 220 */ "File allocation table bad, drive %1" CRLF,
    /* 221 */ "Invalid COMMAND.COM" CRLF,
    /* 222 */ "Insert disk with %1 in drive %2" CRLF,
    /* 223 */ "Press any key to continue . . ." CRLF,
    /* 224 */ CRLF "Terminate batch job (Y/N)?",
    /* 225 */ "Cannot execute %1" CRLF,
    /* 226 */ "Error in EXE file" CRLF,
    /* 227 */ "Program too big to fit in memory" CRLF,
    /* 228 */ CRLF "No free file handles",
    /* 229 */ "Bad Command or file name" CRLF,
    /* 230 */ "Access denied ",
    /* 231 */ CRLF "Memory allocation error",
    /* 232 */ CRLF "Cannot load COMMAND, system halted" CRLF,
    /* 233 */ CRLF "Cannot start COMMAND, exiting" CRLF,
    /* 234 */ CRLF "Top level process aborted, cannot continue" CRLF,
    /* 235 */ CRLF,
};

/* EXTEND19..39: the critical error texts */
static const char *const crit_t[] = {
    "Write protect error", "Invalid unit", "Not ready", "Invalid device request",
    "Data error", "Invalid device request parameters", "Seek error",
    "Invalid media type", "Sector not found", "Printer out of paper error",
    "Write fault error", "Read fault error", "General failure", "Sharing violation",
    "Lock violation", "Invalid disk change", "FCB unavailable",
    "System resource exhausted", "Code page mismatch", "Out of input",
    "Insufficient disk space",
};

static const char *rmsg(int n) { return rmsg_t[n - 210]; }

static void rputc(int c)
{
    REGS r = {0};
    r.r0 = 0x0200;
    r.r3 = c & 0xFF;
    d21(&r);
}

static void rputs(const char *s)
{
    while (*s) rputc((uint8_t)*s++);
}

/* RPRINT with %1..%3 */
static void rprint(int n, const char *s1, const char *s2, const char *s3)
{
    for (const char *t = rmsg(n); *t; t++) {
        if (*t == '%' && t[1] >= '1' && t[1] <= '3') {
            const char *s = t[1] == '1' ? s1 : t[1] == '2' ? s2 : s3;
            if (s) rputs(s);
            t++;
        } else {
            rputc((uint8_t)*t);
        }
    }
}

static int rupper(int c)
{
    c &= 0xFF;
    return c >= 'a' && c <= 'z' ? c - 32 : c;
}

static int getc_echo_flush(void)
{
    REGS r = {0};
    r.r0 = 0x0C01;
    d21(&r);
    return r.r0 & 0xFF;
}

static void hex4(char *b, unsigned v)
{
    static const char hx[] = "0123456789ABCDEF";
    for (int i = 0; i < 4; i++) b[i] = hx[(v >> (12 - 4 * i)) & 15];
    b[4] = 0;
}

/* ------------------------------------------------------ INT 23h -- */

/* CONTC.  DOS aborts whatever runs when we return with CF set: a program,
 * or COMMAND itself (its own parent), which comes back through INT 22h to
 * LODCOM.  The transient asks "Terminate batch job" when it gets control. */
static void contc(struct armregs *f)
{
    if (R.in_init) {
        if (R.init_special) {
            /* ^C at the start-up date/time prompt = an empty answer */
            uint8_t *buf = (uint8_t *)f->r3;
            if (((f->r0 >> 8) & 0xFF) == 0x0A && buf) {
                buf[1] = 0;
                buf[2] = '\r';
            }
            f->r0 = 0x1900;         /* finish the call as a harmless one */
        }
        f->cpsr &= ~ARM_CPSR_C;
        return;
    }
    R.ctrlc_hit = 1;
    f->cpsr |= ARM_CPSR_C;
}

/* ------------------------------------------------------ INT 24h -- */

static uint8_t handle01[2];
static uint8_t *user_jft;

/* SAVHAND: the program's stdin/stdout = our stderr while we talk */
static void savhand(void)
{
    REGS r = {0};
    r.r0 = 0x6200;
    d21(&r);
    struct psp *p = (struct psp *)ARMDOS_SEG2PTR(r.r1 & 0xFFFF);
    user_jft = (uint8_t *)p->jftptr;
    handle01[0] = user_jft[0];
    handle01[1] = user_jft[1];
    uint8_t *mine = (uint8_t *)R.mypsp->jftptr;
    user_jft[0] = mine[2];
    user_jft[1] = mine[2];
}

static void resthand(void)
{
    if (!user_jft) return;
    user_jft[0] = handle01[0];
    user_jft[1] = handle01[1];
    user_jft = 0;
}

static void pipeoff(void)
{
    if (R.pipeflag) {
        R.pipeflag = 0;
        R.echoflag >>= 1;
    }
}

static void dskerr(struct armregs *f)
{
    int ah = (f->r0 >> 8) & 0xFF;
    int al = f->r0 & 0xFF;
    int olderrno = f->r5 & 0xFF;
    const uint8_t *dev = (const uint8_t *)f->r4;
    int cdevat = dev ? dev[5] : 0;
    char devname[9], drvlet[2];
    int res;

    memset(devname, ' ', 8);
    if (dev) memcpy(devname, dev + 16, 8);
    devname[8] = 0;
    savhand();
    rputs(CRLF);
    drvlet[0] = al + 'A';
    drvlet[1] = 0;
    if ((ah & 0x80) && !(cdevat & 0x80)) {
        rprint(220, drvlet, 0, 0);
        res = 2;
        goto eexit;
    }
    const char *rw = rmsg((ah & 1) ? 216 : 215);
    REGS x = {0};
    x.r0 = 0x5900;
    d21(&x);
    int idx = (int)(x.r0 & 0xFFFF) - 19;
    if (idx < 0) idx = 31 - 19;
    int errtype = idx == 35 - 19 || idx == 36 - 19;
    if (idx > 39 - 19) {
        errtype = 0;
        idx = olderrno;
    }
    rputs(crit_t[idx]);
    if (errtype) rputs(CRLF);
    else if (cdevat & 0x80) rprint(218, rw, devname, 0);
    else rprint(217, rw, drvlet, 0);
    if (R.loading) {
        /* reading COMMAND.COM back: the loader prompts for the disk */
        R.load_failed = 1;
        res = 3;
        goto eexit;
    }
    for (;;) {
        if (idx == 15) {
            const uint8_t *vol = (const uint8_t *)x.r5;
            if ((uint32_t)vol > 0x400 && (uint32_t)vol < 0xA0000) {
                char vname[13], s1[5], s2[5];
                memcpy(vname, vol, 11);
                vname[11] = ' ';
                vname[12] = 0;
                uint32_t ser = vol[12] | (vol[13] << 8) | (vol[14] << 16) | ((uint32_t)vol[15] << 24);
                hex4(s1, ser >> 16);
                hex4(s2, ser & 0xFFFF);
                rprint(219, vname, s1, s2);
            }
        }
        rprint(210, 0, 0, 0);
        if (ah & 0x10) rprint(211, 0, 0, 0);
        if (ah & 0x20) rprint(212, 0, 0, 0);
        if (ah & 0x08) rprint(213, 0, 0, 0);
        rprint(214, 0, 0, 0);
        if (R.ffail) {
            res = 3;
            goto eexit;
        }
        int c = getc_echo_flush();
        if (c == 3) rputs("^C");            /* ^C answers Abort */
        rputs(CRLF);
        c = rupper(c);
        if ((ah & 0x20) && c == 'I') { res = 0; goto eexit; }
        if ((ah & 0x10) && c == 'R') { res = 1; goto eexit; }
        if (c == 'A' || c == 3) break;
        if ((ah & 0x08) && c == 'F') { res = 3; goto eexit; }
    }
    /* abort_process */
    if (R.in_init && !R.permcom) {
        rprint(234, 0, 0, 0);
        for (;;) armdos_halt();
    }
    if (R.in_batch) R.batch_abort = 1;
    {
        int dl = R.pipeflag;
        pipeoff();
        if (dl && R.singlecom) R.singlecom = 0xFFFF;
    }
    if (idx == 0 || idx == 2) {
        R.forflag = 0;
        if (R.singlecom) R.singlecom = 0xFFFF;
    }
    res = 2;
eexit:
    resthand();
    f->r0 = (f->r0 & ~0xFFu) | res;
}

/* ------------------------------------------------------ the transient -- */

static uint32_t checksum(const uint8_t *p, uint32_t n)
{
    const uint32_t *w = (const uint32_t *)p;
    uint32_t s = 0;
    for (uint32_t i = 0; i < n / 4; i++) s = (s << 1 | s >> 31) + w[i];
    return s;
}

static uint32_t tbase(void) { return (uint32_t)R.tseg << 4; }

__attribute__((noreturn)) static void enter_transient(int reason)
{
    uint32_t top = (tbase() + R.tsize) & ~7u;
    res_call_on_stack((void *)(tbase() + R.tentry), &R, reason, (void *)top);
}

static uint16_t alloc_top(uint32_t bytes)
{
    REGS r = {0};
    r.r0 = 0x5800;
    d21(&r);
    unsigned strat = r.r0 & 0xFFFF;
    memset(&r, 0, sizeof r);
    r.r0 = 0x5801;
    r.r1 = 2;                                   /* last fit */
    d21(&r);
    memset(&r, 0, sizeof r);
    r.r0 = 0x4800;
    r.r1 = (bytes + 15) >> 4;
    int cf = d21(&r);
    uint16_t seg = cf ? 0 : (r.r0 & 0xFFFF);
    memset(&r, 0, sizeof r);
    r.r0 = 0x5801;
    r.r1 = strat;
    d21(&r);
    return seg;
}

static void free_seg(uint16_t seg)
{
    REGS r = {0};
    r.r0 = 0x4900;
    r.r8 = seg;
    d21(&r);
}

__attribute__((noreturn)) static void res_exit_c(int code)
{
    REGS r = {0};
    R.mypsp->parent = R.parent_psp;
    R.mypsp->int22 = R.old_term;
    r.r0 = 0x4C00 | (code & 0xFF);
    for (;;) d21(&r);
}

static void res_exit(int code) { res_exit_c(code); }

/* FATALC: we cannot get the transient back */
__attribute__((noreturn)) static void fatalc(int n)
{
    rprint(n, 0, 0, 0);
    if (!R.permcom || R.singlecom) {
        rprint(233, 0, 0, 0);
        res_exit_c(0);
    }
    rprint(232, 0, 0, 0);
    for (;;) armdos_halt();
}

/* GETCOMDSK: ask for the disk with COMMAND.COM (removable media only) */
static void getcomdsk(int combad)
{
    const char *cs = R.comspec;
    int drive = (cs[0] && cs[1] == ':') ? rupper(cs[0]) - '@' : 0;
    REGS r = {0};
    r.r0 = 0x4408;
    r.r1 = drive;
    int removable = !d21(&r) && !(r.r0 & 1);
    if (!removable) fatalc(221);
    if (combad) rprint(221, 0, 0, 0);
    char dl[2];
    if (drive) {
        dl[0] = drive + '@';
    } else {
        REGS g = {0};
        g.r0 = 0x1900;
        d21(&g);
        dl[0] = 'A' + (g.r0 & 0xFF);
    }
    dl[1] = 0;
    rprint(222, (cs[0] && cs[1] == ':') ? cs + 2 : cs, dl, 0);
    rprint(223, 0, 0, 0);
    memset(&r, 0, sizeof r);
    r.r0 = 0x0C07;
    d21(&r);
    memset(&r, 0, sizeof r);
    r.r0 = 0x0C00;
    d21(&r);
}

static int fread_at(int h, uint32_t off, void *buf, uint32_t n)
{
    REGS r = {0};
    r.r0 = 0x4200;
    r.r1 = h;
    r.r2 = off >> 16;
    r.r3 = off & 0xFFFF;
    if (d21(&r)) return -1;
    uint8_t *p = buf;
    while (n) {
        uint32_t k = n > 0x8000 ? 0x8000 : n;
        memset(&r, 0, sizeof r);
        r.r0 = 0x3F00;
        r.r1 = h;
        r.r2 = k;
        r.r3 = (uint32_t)p;
        if (d21(&r) || (r.r0 & 0xFFFF) != k) return -1;
        p += k;
        n -= k;
    }
    return 0;
}

static void fclose_(int h)
{
    REGS r = {0};
    r.r0 = 0x3E00;
    r.r1 = h;
    d21(&r);
}

/* LOADCOM: read the transient from COMSPEC into its block and relocate it */
static void loadcom(void)
{
    uint8_t *dst = (uint8_t *)tbase();
    for (;;) {
        R.loading = 1;
        R.load_failed = 0;
        REGS r = {0};
        r.r0 = 0x3D00;
        r.r3 = (uint32_t)R.comspec;
        if (d21(&r)) {
            R.loading = 0;
            if ((r.r0 & 0xFFFF) == 4) fatalc(228);
            getcomdsk(0);
            continue;
        }
        int h = r.r0 & 0xFFFF;
        uint8_t hdr[64];
        struct armexe th;
        uint32_t lfanew, blob;
        int ok = fread_at(h, 0, hdr, 64) == 0 && hdr[0] == 'M' && hdr[1] == 'Z';
        if (ok) {
            lfanew = hdr[0x3C] | (hdr[0x3D] << 8) | (hdr[0x3E] << 16) | ((uint32_t)hdr[0x3F] << 24);
            ok = fread_at(h, lfanew, hdr, 64) == 0 && !memcmp(hdr, "AR1", 4);
        }
        if (ok) {
            uint32_t image_off = hdr[8] | (hdr[9] << 8) | (hdr[10] << 16) | ((uint32_t)hdr[11] << 24);
            blob = image_off + R.tfileoff;
            ok = fread_at(h, blob, &th, sizeof th) == 0 && !memcmp(th.sig, "AR1", 4) &&
                 th.image_size == R.timage && th.entry == R.tentry;
        }
        if (ok) ok = fread_at(h, blob + th.image_off, dst, th.image_size) == 0;
        if (ok) ok = checksum(dst, th.image_size) == R.trawsum;
        if (ok) {
            uint32_t buf[64];
            uint32_t left = th.reloc_count, off = blob + th.reloc_off;
            while (ok && left) {
                uint32_t k = left > 64 ? 64 : left;
                ok = fread_at(h, off, buf, k * 4) == 0;
                for (uint32_t i = 0; ok && i < k; i++) *(uint32_t *)(dst + buf[i]) += (uint32_t)dst;
                off += k * 4;
                left -= k;
            }
        }
        fclose_(h);
        R.loading = 0;
        if (ok && !R.load_failed) break;
        getcomdsk(!R.load_failed);
    }
    R.tsum = checksum(dst, R.timage);
}

static uint8_t t_valid;                 /* the transient is allocated and intact */

/* the transient's block back after a program ran: reload if needed */
static void transient_ensure(void)
{
    uint16_t seg = alloc_top(R.tsize);
    if (!seg) fatalc(231);
    if (seg != R.tseg || checksum((uint8_t *)tbase(), R.timage) != R.tsum) {
        R.tseg = seg;
        loadcom();
    }
    t_valid = 1;
}

/* ------------------------------------------------------------ EXEC -- */

static void exec_err(int err)
{
    int n;
    switch (err) {
    case 2: n = 229; break;
    case 8: n = 227; break;
    case 11: n = 226; break;
    case 5: n = 230; break;
    default:
        rprint(225, R.execpath, 0, 0);
        return;
    }
    rprint(n, 0, 0, 0);
}

/* entered on the resident stack (res_exec_entry) */
__attribute__((noreturn)) void res_exec_c(void)
{
    uint8_t *pb = R.execblock;
    uint32_t v;
    t_valid = 0;
    free_seg(R.tseg);
    pb[0] = R.envseg;
    pb[1] = R.envseg >> 8;
    v = (uint32_t)R.exectail;
    memcpy(pb + 2, &v, 4);
    v = (uint32_t)R.execfcb1;
    memcpy(pb + 6, &v, 4);
    v = (uint32_t)R.execfcb2;
    memcpy(pb + 10, &v, 4);
    REGS r = {0};
    r.r0 = 0x4B00;
    r.r3 = (uint32_t)R.execpath;
    r.r1 = (uint32_t)pb;
    if (d21(&r)) {
        exec_err(r.r0 & 0xFFFF);
    } else {
        memset(&r, 0, sizeof r);
        r.r0 = 0x4D00;
        d21(&r);
        R.retcode = r.r0 & 0xFFFF;
    }
    transient_ensure();
    enter_transient(T_EXEC);
}

/* INT 22h: DOS aborted COMMAND itself (entered on the resident stack) */
__attribute__((noreturn)) void res_lodcom_c(void)
{
    /* (aborted by ^C while we were getting the transient back?) */
    if (!t_valid) transient_ensure();
    enter_transient(T_LODCOM);
}

/* ------------------------------------------------------------ start -- */

__attribute__((noreturn)) void res_start(struct psp *psp, uint8_t *base)
{
    const struct armexe *th = (const struct armexe *)tran_blob;
    REGS r;

    R.magic = RES_MAGIC;
    R.mypsp = psp;
    R.exec = res_exec_entry;
    R.exit = res_exit;
    R.h22 = (void *)lodcom_entry;
    R.h23 = (void *)contc;
    R.h24 = (void *)dskerr;
    R.in_init = 1;
    R.echoflag = 1;
    R.verval = -1;
    R.timage = th->image_size;
    R.tentry = th->entry;
    R.tsize = (th->image_size + th->bss_size + th->stack_size + 15) & ~15u;
    R.tfileoff = (uint32_t)(tran_blob - base);

    /* 1. keep only resident + blob; 2. the transient block at the top */
    uint32_t psp_a = (uint32_t)psp;
    uint32_t blob_end = (uint32_t)tran_blob + th->reloc_off + th->reloc_count * 4;
    memset(&r, 0, sizeof r);
    r.r0 = 0x4A00;
    r.r1 = (blob_end - psp_a + 15) >> 4;
    r.r8 = psp_a >> 4;
    d21(&r);
    R.tseg = alloc_top(R.tsize);
    if (!R.tseg) {
        rprint(231, 0, 0, 0);
        rprint(232, 0, 0, 0);
        for (;;) armdos_halt();
    }
    uint8_t *dst = (uint8_t *)tbase();
    memcpy(dst, tran_blob + th->image_off, th->image_size);
    R.trawsum = checksum(dst, th->image_size);
    const uint32_t *rel = (const uint32_t *)(tran_blob + th->reloc_off);
    for (uint32_t i = 0; i < th->reloc_count; i++) *(uint32_t *)(dst + rel[i]) += (uint32_t)dst;
    R.tsum = checksum(dst, th->image_size);
    t_valid = 1;

    /* 3. the resident part only */
    memset(&r, 0, sizeof r);
    r.r0 = 0x4A00;
    r.r1 = ((uint32_t)__resident_end - psp_a + 15) >> 4;
    r.r8 = psp_a >> 4;
    d21(&r);
    enter_transient(T_INIT);
}
