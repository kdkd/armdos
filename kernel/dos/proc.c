/*
 * proc.c - processes (DOS/EXEC.ASM, PROC.ASM, ABORT.ASM, CTRLC.ASM DIVOV):
 * EXEC of .EXE (MZ + AR1) and raw .COM images, the PSP, terminate and
 * TSR, and the handlers for program faults.
 *
 * The frame model (ARCH.md 14.5): EXEC saves the parent's register frame on
 * the parent's own stack (as DOS pushes the user registers there), points
 * the child's PSP at it (+44h, ARM-DOS private) and rewrites the INT 21h
 * frame so that the BIOS "returns" into the child.  Terminate restores the
 * saved frame into the current one, so the BIOS returns to the parent just
 * after its INT 21h AH=4Bh, with CF clear.
 */
#include "dos.h"

#define FRAME_MAGIC 0x45584543u         /* "EXEC" */

struct savedframe {
    uint32_t magic;
    uint32_t r[13];
    uint32_t sp, lr, pc, cpsr;
};

static struct savedframe svc_frames[4];     /* for EXECs made from SVC mode */
static char shell_path[80], shell_tail[128];
static uint8_t root_pending;
static uint8_t in_fault;

struct ar1hdr {
    char sig[4];
    uint16_t hdrsize, flags;
    uint32_t image_off, image_size, bss_size, stack_size, entry, reloc_off, reloc_count, min_extra, max_extra;
    uint32_t reserved[5];
};

void set_shell(const char *path, const char *tail)
{
    strlcpy(shell_path, path, sizeof shell_path);
    strlcpy(shell_tail, tail, sizeof shell_tail);
    root_pending = 1;
}

/* ------------------------------------------------------------- PSPs */

void new_psp(uint16_t seg, uint16_t memtop, int inherit)
{
    struct psp *p = PSP(seg);
    struct psp *parent = cur_psp ? PSP(cur_psp) : 0;
    memset(p, 0, 256);
    p->int20 = 0xDF20;                  /* Thumb: svc #0x20 */
    p->memtop = memtop;
    p->int22 = IVT[0x22];
    p->int23 = IVT[0x23];
    p->int24 = IVT[0x24];
    p->parent = cur_psp;
    memset(p->jft, 0xFF, 20);
    p->jftsize = 20;
    p->jftptr = (uint32_t)p + 0x18;
    p->prevpsp = 0xFFFFFFFFu;
    p->dosver = 0x0004;
    p->call21[0] = 0xEF000021u;         /* svc #0x21 */
    p->call21[1] = 0xE12FFF1Eu;         /* bx lr */
    memset(p->fcb1 + 1, ' ', 11);
    memset(p->fcb2 + 1, ' ', 11);
    p->tail[0] = 0;
    p->tail[1] = '\r';
    if (inherit && parent) {
        unsigned size;
        uint8_t *pj = jft_ptr(parent, &size);
        for (unsigned h = 0; h < size && h < 20; h++) {
            if (pj[h] == 0xFF) continue;
            struct sft *s = sft_get(pj[h]);
            if (!s || !s->ref_count || (s->mode & 0x80)) continue;
            p->jft[h] = pj[h];
            s->ref_count++;
        }
    }
}

/* the program's name for its MCB ("COMMAND"), from its path */
static void mcb_name(unsigned seg, const char *path)
{
    const char *b = path;
    for (const char *q = path; *q; q++) if (*q == '\\' || *q == '/' || *q == ':') b = q + 1;
    struct mcb *m = MCB(seg - 1);
    int i = 0;
    for (; b[i] && b[i] != '.' && i < 8; i++) m->name[i] = b[i];
    if (i < 8) m->name[i] = 0;
}

/* --------------------------------------------------------------- EXEC */

static int read_at(struct sft *s, uint32_t off, void *buf, unsigned n)
{
    s->position = off;
    uint8_t *p = buf;
    while (n) {
        unsigned k = n > 0x8000 ? 0x8000 : n;
        int r = file_read(s, p, k);
        if (r < 0) return r;
        if ((unsigned)r != k) return -E_BADFMT;
        p += k; n -= k;
    }
    return 0;
}

static int relocate(struct sft *s, const struct ar1hdr *h, uint32_t base, uint32_t delta, uint32_t limit)
{
    static uint32_t rbuf[128];
    uint32_t left = h->reloc_count, off = h->reloc_off;
    while (left) {
        unsigned n = left > 128 ? 128 : left;
        int e = read_at(s, off, rbuf, n * 4);
        if (e < 0) return e;
        for (unsigned i = 0; i < n; i++) {
            uint32_t o = rbuf[i];
            if (o + 4 > limit) return -E_BADFMT;
            uint8_t *p = (uint8_t *)base + o;
            uint32_t v = (p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24)) + delta;
            p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24;
        }
        off += n * 4;
        left -= n;
    }
    return 0;
}

struct execpb { uint16_t env; uint32_t tail, fcb1, fcb2; uint32_t sp, pc; } PACKED;

static int do_exec(struct armregs *f, const char *upath, int al, uint32_t pbaddr, int root);

/* Is this raw image (a .COM, no MZ) ARM code?  Every ARM-DOS .COM starts
 * with ARM instructions that execute always (condition AL = top nibble E);
 * x86 code practically never has that in 3 of its first 4 words.  (The
 * coprocessor space, which ARM-DOS programs do not start with, counts as
 * x86.)  In an image under 16 bytes the first word decides. */
static int arm_code(const uint8_t *h, int n)
{
    int words = n / 4, al = 0;
    if (words > 4) words = 4;
    if (words == 0) return 1;
    if ((h[3] & 0xF0) != 0xE0 || (h[3] & 0x0C) == 0x0C) return 0;
    for (int i = 0; i < words; i++) if ((h[i * 4 + 3] & 0xF0) == 0xE0) al++;
    return words == 4 ? al >= 3 : 1;          /* (under 16 bytes: the first word decides) */
}

/* An x86 program: EXEC ELBOW.EXE (the 8086 compatibility box, in \DOS on
 * the boot drive) instead, with the program's full path in front of its
 * command tail - as NTVDM starts for a DOS program under Windows NT. */
static int exec_x86(struct armregs *f, const char *path, int al, uint32_t pbaddr, int root)
{
    static char elbow[20];
    static uint8_t tail[128];
    static struct execpb npb;
    if (al == 3) return -E_BADFMT;                       /* (overlays: only for ARM images) */
    ksnprintf(elbow, sizeof elbow, "%c:\\DOS\\ELBOW.EXE", 'A' + LOL.bootdrive - 1);
    if (!strcmp(path, elbow)) return -E_BADFMT;          /* ELBOW itself is not an ARM program? */
    memcpy(&npb, (const void *)pbaddr, al == 1 ? sizeof npb : 14);
    const uint8_t *ot = (const uint8_t *)npb.tail;
    int n = ksnprintf((char *)tail + 1, 126, " %s", path);
    int ol = ot[0] > 126 ? 126 : ot[0];
    if (n + ol > 126) ol = 126 - n;
    memcpy(tail + 1 + n, ot + 1, ol);
    tail[0] = n + ol;
    tail[1 + n + ol] = '\r';
    npb.tail = (uint32_t)tail;
    int e = do_exec(f, elbow, al, (uint32_t)&npb, root);
    if (e == -E_NOFILE || e == -E_NOPATH) return -E_BADFMT;   /* no ELBOW: as before */
    if (e >= 0 && al == 1) memcpy((uint8_t *)pbaddr + 14, (const uint8_t *)&npb + 14, 8);
    return e;
}

static int do_exec(struct armregs *f, const char *upath, int al, uint32_t pbaddr, int root)
{
    struct pathinfo pi;
    char path[80];
    int e;
    if (strlen(upath) >= sizeof path) return -E_NOPATH;
    e = canon_path(upath, path, 0, 0);
    if (e < 0) return e;

    struct sft s;
    memset(&s, 0, sizeof s);
    e = file_open_sft(upath, 0, &s, 0, 0, 0);
    if (e < 0) return e;
    if (s.flags & SF_DEVICE) return -E_ACCESS;
    (void)pi;

    /* what kind of image */
    uint8_t hdr[64];
    struct ar1hdr ah;
    int exe = 0;
    memset(hdr, 0, sizeof hdr);
    s.position = 0;
    int got = file_read(&s, hdr, 64);
    if (got < 0) return got;
    if (got >= 2 && ((hdr[0] == 'M' && hdr[1] == 'Z') || (hdr[0] == 'Z' && hdr[1] == 'M'))) {
        if (got < 64) return -E_BADFMT;
        uint32_t lfa = hdr[0x3C] | (hdr[0x3D] << 8) | (hdr[0x3E] << 16) | ((uint32_t)hdr[0x3F] << 24);
        if (lfa + 64 > s.size || read_at(&s, lfa, &ah, 64) < 0 || memcmp(ah.sig, "AR1", 4))
            return exec_x86(f, path, al, pbaddr, root);          /* an x86 .EXE: ELBOW runs it */
        if (ah.image_off + ah.image_size > s.size || ah.reloc_off + ah.reloc_count * 4 > s.size) return -E_BADFMT;
        exe = 1;
    } else if (!arm_code(hdr, got)) {
        return exec_x86(f, path, al, pbaddr, root);              /* an x86 .COM */
    } else {
        memset(&ah, 0, sizeof ah);
        ah.image_off = 0;
        ah.image_size = s.size;
        ah.stack_size = 0;
        ah.max_extra = 0xFFFFFFFFu;
        ah.min_extra = 256;
    }

    if (al == 3) {
        /* overlay: load at the segment given, relocate by the factor */
        const uint16_t *ov = (const uint16_t *)pbaddr;
        uint32_t base = (uint32_t)ov[0] << 4;
        uint32_t delta = (uint32_t)ov[1] << 4;
        e = read_at(&s, ah.image_off, (void *)base, ah.image_size);
        if (e < 0) return e;
        if (exe && ah.reloc_count && (e = relocate(&s, &ah, base, delta, ah.image_size)) < 0) return e;
        if (exe) memset((void *)(base + ah.image_size), 0, ah.bss_size);
        return 0;
    }

    /* copy what the parent passed before memory is handed out */
    struct execpb pb;
    memcpy(&pb, (const void *)pbaddr, al == 1 ? sizeof pb : 14);
    uint8_t tail[128], fcb1[16], fcb2[16];
    memcpy(tail, (const void *)pb.tail, 128);
    if (tail[0] > 126) tail[0] = 126;
    tail[tail[0] + 1] = '\r';
    if (pb.fcb1) memcpy(fcb1, (const void *)pb.fcb1, 16); else { memset(fcb1, 0, 16); memset(fcb1 + 1, ' ', 11); }
    if (pb.fcb2) memcpy(fcb2, (const void *)pb.fcb2, 16); else { memset(fcb2, 0, 16); memset(fcb2 + 1, ' ', 11); }

    /* the environment: the one given, or a copy of the parent's; if the
       parent has none either, the child gets none (EXEC.ASM) */
    uint16_t srcenv = pb.env;
    if (!srcenv && cur_psp) srcenv = PSP(cur_psp)->envseg;
    const char *env = srcenv ? (const char *)((uint32_t)srcenv << 4) : "\0";
    unsigned envseg = 0, largest;
    if (srcenv) {
    /* elen = bytes of strings, each NUL-terminated (0 if there are none) */
    unsigned elen = 0;
    while (env[elen]) {
        while (env[elen]) { elen++; if (elen >= 32768) return -E_BADENV; }
        elen++;
    }
    unsigned plen = strlen(path);
    unsigned envbytes = elen + 1 + 2 + plen + 1;
    e = mem_alloc((envbytes + 15) >> 4, &envseg, &largest);
    if (e < 0) return e;
    char *ne = (char *)((uint32_t)envseg << 4);
    memmove(ne, env, elen);
    ne[elen] = 0;                       /* the empty string that ends the list */
    unsigned k = elen + 1;
    ne[k] = 1; ne[k + 1] = 0;
    memcpy(ne + k + 2, path, plen + 1);
    }

    /* the program's block */
    uint32_t need = 0x100 + ah.image_size + ah.bss_size + ah.stack_size + ah.min_extra;
    uint32_t want = ah.max_extra == 0xFFFFFFFFu ? 0xFFFFFFFFu
                  : 0x100 + ah.image_size + ah.bss_size + ah.stack_size + ah.max_extra;
    if (want < need) want = need;
    unsigned big = mem_largest();
    if ((uint32_t)big * 16 < need) { if (envseg) mem_free(envseg); return -E_NOMEM; }
    unsigned paras = want >= (uint32_t)big * 16 ? big : (want + 15) >> 4;
    unsigned pspseg;
    e = mem_alloc(paras, &pspseg, &largest);
    if (e < 0) { if (envseg) mem_free(envseg); return e; }

    uint32_t pspaddr = (uint32_t)pspseg << 4;
    uint32_t base = pspaddr + 0x100;
    uint32_t end = pspaddr + (uint32_t)paras * 16;

    /* load */
    e = read_at(&s, ah.image_off, (void *)base, ah.image_size);
    if (e == 0 && exe && ah.reloc_count) e = relocate(&s, &ah, base, base, ah.image_size);
    if (e < 0) { mem_free(pspseg); if (envseg) mem_free(envseg); return e == -E_BADFMT ? e : -E_BADFMT; }
    if (exe) memset((void *)(base + ah.image_size), 0, ah.bss_size);

    /* the PSP */
    uint16_t parent = cur_psp;
    new_psp(pspseg, pspseg + paras, 1);
    struct psp *p = PSP(pspseg);
    p->envseg = envseg;
    p->parent = root ? pspseg : parent;
    memcpy(p->fcb1, fcb1, 16);
    memcpy(p->fcb2, fcb2, 16);
    memcpy(p->tail, tail, 128);
    if (envseg) MCB(envseg - 1)->owner = pspseg;
    MCB(pspseg - 1)->owner = pspseg;
    mcb_name(pspseg, path);

    uint32_t sp = exe ? base + ah.image_size + ah.bss_size + ah.stack_size : end;
    if (sp > end) sp = end;
    sp &= ~7u;
    uint32_t pc = base + ah.entry;
    int thumb = exe && ((ah.flags & 1) || (ah.entry & 1));
    pc &= ~1u;

    if (al == 1) {
        struct execpb *up = (struct execpb *)pbaddr;
        up->sp = sp;
        up->pc = pc | (thumb ? 1 : 0);
        cur_psp = pspseg;
        cur_dta = pspaddr + 0x80;
        p->execframe = 0;
        return 0;
    }

    /* save the parent's frame on its stack and enter the child */
    struct savedframe *sf;
    if (root) sf = 0;
    else if ((f->cpsr & MODE_MASK) == MODE_SVC) {
        sf = 0;
        for (int i = 0; i < 4; i++) if (svc_frames[i].magic != FRAME_MAGIC) { sf = &svc_frames[i]; break; }
        if (!sf) sf = &svc_frames[3];
    } else sf = (struct savedframe *)((f->sp - sizeof *sf) & ~7u);
    if (sf) {
        sf->magic = FRAME_MAGIC;
        memcpy(sf->r, &f->r0, 13 * 4);
        sf->sp = f->sp;
        sf->lr = f->lr;
        sf->pc = f->pc;
        sf->cpsr = f->cpsr & ~CPSR_C;
    }
    p->execframe = (uint32_t)sf;
    uint32_t ret22 = f->pc | ((f->cpsr & CPSR_T) ? 1 : 0);
    p->int22 = root ? 0 : ret22;
    IVT[0x22] = p->int22;

    cur_psp = pspseg;
    cur_dta = pspaddr + 0x80;
    memset(&f->r0, 0, 13 * 4);
    f->r0 = pspaddr;
    f->r1 = base;
    f->r2 = end;
    f->sp = sp;
    f->lr = pspaddr | 1;                /* bx lr lands on the Thumb "INT 20h" at PSP:0 */
    f->pc = pc;
    f->cpsr = MODE_SYS | CPSR_F | (thumb ? CPSR_T : 0);
    return 0;
}

void exec_fn(struct armregs *f)
{
    int al = AL(f);
    if (al != 0 && al != 1 && al != 3) { sys_err(f, E_INVFN); return; }
    int root = root_pending && cur_psp == init_psp_seg && al == 0;
    int e = do_exec(f, (const char *)f->r3, al, f->r1, root);
    if (e < 0) { sys_err(f, -e); return; }
    if (root) root_pending = 0;
    if (al != 0) f->cpsr &= ~CPSR_C;
}

/* the root process died without an INT 22h: start the shell again */
static void restart_shell(struct armregs *f)
{
    static uint8_t tail[128], fcb1[16];
    static struct execpb pb;
    char full[80];
    int drive = LOL.bootdrive - 1;
    if (shell_path[1] == ':') strcpy(full, shell_path);
    else ksnprintf(full, sizeof full, "%c:%s%s", 'A' + drive, shell_path[0] == '\\' ? "" : "\\", shell_path);
    int t = strlen(shell_tail);
    tail[0] = t;
    memcpy(tail + 1, shell_tail, t);
    tail[t + 1] = '\r';
    /* as SYSINIT: no environment (the shell builds PATH= and COMSPEC=),
       FCB1's drive = the boot drive */
    memset(fcb1, 0, sizeof fcb1);
    memset(fcb1 + 1, ' ', 11);
    fcb1[0] = drive + 1;
    pb.env = 0;
    pb.tail = (uint32_t)tail;
    pb.fcb1 = (uint32_t)fcb1;
    pb.fcb2 = 0;
    cur_psp = init_psp_seg;
    if (do_exec(f, full, 0, (uint32_t)&pb, 1) < 0) {
        bcon_write("\r\nBad or missing Command Interpreter\r\n", 38);
        irq_on();
        for (;;) cpu_wfi();
    }
}

static void halt_memory(void)
{
    static const char m[] = "\r\nMemory allocation error \r\nCannot load COMMAND, system halted\r\n";
    bcon_write(m, sizeof m - 1);
    irq_on();
    for (;;) cpu_wfi();
}

/* end the current process: type 0 normal, 1 ^C, 2 critical error, 3 TSR */
void terminate(struct armregs *f, int code, int type)
{
    uint16_t me = cur_psp;
    struct psp *p = PSP(me);
    exit_code = ((type & 0xFF) << 8) | (code & 0xFF);
    IVT[0x22] = p->int22;
    IVT[0x23] = p->int23;
    IVT[0x24] = p->int24;
    uint16_t parent = p->parent;
    int root = parent == me || me == init_psp_seg;
    if (!root && type != 3) {
        close_process_files(p);
        mem_free_owner(me);
    }
    flush_bufs(-1);
    if (LOL.first_mcb && mem_check() < 0) halt_memory();
    printer_echo = 0;

    if (root) {
        uint32_t t22 = IVT[0x22];
        if (!t22) {
            /* nothing to go back to: a new shell */
            close_process_files(p);
            mem_free_owner(me);
            restart_shell(f);
            return;
        }
        f->pc = t22 & ~1u;
        f->cpsr = (f->cpsr & ~(CPSR_T | CPSR_C | MODE_MASK)) | MODE_SYS | ((t22 & 1) ? CPSR_T : 0);
        f->cpsr &= ~CPSR_I;
        return;
    }

    cur_psp = parent;
    cur_dta = ((uint32_t)parent << 4) + 0x80;
    struct savedframe *sf = (struct savedframe *)p->execframe;
    uint32_t t22 = IVT[0x22];
    if (sf && sf->magic == FRAME_MAGIC) {
        sf->magic = 0;
        memcpy(&f->r0, sf->r, 13 * 4);
        f->sp = sf->sp;
        f->lr = sf->lr;
        f->pc = sf->pc;
        f->cpsr = sf->cpsr & ~CPSR_C;
        uint32_t orig = sf->pc | ((sf->cpsr & CPSR_T) ? 1 : 0);
        if (t22 && t22 != orig) {
            f->pc = t22 & ~1u;
            f->cpsr = (f->cpsr & ~CPSR_T) | ((t22 & 1) ? CPSR_T : 0);
        }
    } else {
        /* loaded with AL=01h (a debugger): go to its INT 22h with its stack */
        memset(&f->r0, 0, 13 * 4);
        f->sp = PSP(parent)->savedsp;
        f->pc = t22 & ~1u;
        f->cpsr = MODE_SYS | CPSR_F | ((t22 & 1) ? CPSR_T : 0);
    }
}

/* ---------------------------------------------------- INT 20h / 27h */

void int20_handler(struct armregs *f)
{
    /* INT 20h is an INT 21h AH=00h */
    f->r0 = 0;
    int21_handler(f);
}

void int27_handler(struct armregs *f)
{
    /* DX = first byte beyond the resident part (a flat pointer, or an offset from the PSP) */
    uint32_t psp = (uint32_t)cur_psp << 4;
    uint32_t e = f->r3;
    uint32_t keep = e >= psp ? e - psp : e;
    f->r3 = (keep + 15) >> 4;
    f->r0 = 0x3100;
    int21_handler(f);
}

/* ----------------------------------------------------- INT 21h calls */

void proc_functions(struct armregs *f)
{
    switch (AH(f)) {
    case 0x31: {
        unsigned paras = DX(f), maxp;
        if (paras < 6) paras = 6;
        mem_resize(cur_psp, paras, &maxp);
        terminate(f, AL(f), 3);
        break;
    }
    case 0x4D:
        f->r0 = exit_code;
        exit_code = 0;
        break;
    case 0x50: cur_psp = BX(f); break;
    case 0x51: case 0x62: f->r1 = cur_psp; break;
    case 0x26: {
        uint16_t seg = DX(f);
        struct psp *src = PSP(cur_psp), *p = PSP(seg);
        memcpy(p, src, 256);
        p->int22 = IVT[0x22];
        p->int23 = IVT[0x23];
        p->int24 = IVT[0x24];
        p->jftptr = (uint32_t)p + 0x18;
        break;
    }
    case 0x55: {
        new_psp(DX(f), f->r4 & 0xFFFF, 1);
        PSP(DX(f))->envseg = PSP(cur_psp)->envseg;
        cur_psp = DX(f);
        break;
    }
    }
}

/* ------------------------------------------------------------- faults */

static int_handler bios_fault[4];
static const uint8_t fault_ints[4] = { 0x00, 0x06, 0x0D, 0x0E };

static int fault_slot(int n)
{
    for (int i = 0; i < 4; i++) if (fault_ints[i] == n) return i;
    return 0;
}

static uint32_t kernel_top(void)
{
    if (!LOL.first_mcb) return dos_image_end + 0x10000;
    struct mcb *m = MCB(LOL.first_mcb);
    return ((uint32_t)LOL.first_mcb + 1 + m->size) << 4;
}

static const char *prog_name(uint16_t psp, char *out)
{
    /* from the path after the environment, else the MCB name */
    struct psp *p = PSP(psp);
    out[0] = 0;
    if (p->envseg) {
        const char *e = (const char *)((uint32_t)p->envseg << 4);
        int guard = 0;
        while (*e && guard++ < 1000) e += strlen(e) + 1;
        e++;
        if (e[0] == 1 && e[1] == 0) {
            const char *path = e + 2, *b = path;
            for (const char *q = path; *q; q++) if (*q == '\\' || *q == ':') b = q + 1;
            strlcpy(out, b, 13);
        }
    }
    if (!out[0]) {
        struct mcb *m = MCB(psp - 1);
        int i = 0;
        for (; i < 8 && m->name[i]; i++) out[i] = m->name[i];
        out[i] = 0;
    }
    return out;
}

static void fault_handler(struct armregs *f)
{
    int n = f->intno;
    uint32_t mode = f->cpsr & MODE_MASK;
    uint32_t pc = f->pc;
    int_handler old = bios_fault[fault_slot(n)];
    int kernel = mode == MODE_SVC || pc < kernel_top() || pc >= 0xFFF00000u || in_fault ||
                 !LOL.first_mcb || cur_psp == init_psp_seg;
    if (kernel) { if (old) old(f); return; }
    in_fault = 1;
    irq_on();
    char msg[240];
    int len;
    if (n == 0) {
        len = ksnprintf(msg, sizeof msg, "\r\nDivide overflow\r\n");
    } else {
        char name[16];
        const char *what = n == 6 ? "undefined instruction" : n == 0x0D ? "data abort" : "prefetch abort";
        len = ksnprintf(msg, sizeof msg, "\r\nException %02Xh: %s at %08X in %s", n, what, pc, prog_name(cur_psp, name));
        if (n == 0x0D) {
            uint32_t far = read_far();
            len += ksnprintf(msg + len, sizeof msg - len, " (address %08X)", far);
        }
        len += ksnprintf(msg + len, sizeof msg - len, "\r\n");
        bcon_write(msg, len);
        const uint32_t *r = &f->r0;
        len = ksnprintf(msg, sizeof msg, "R0=%08X R1=%08X R2=%08X R3=%08X R4=%08X R5=%08X\r\n",
                        r[0], r[1], r[2], r[3], r[4], r[5]);
        bcon_write(msg, len);
        len = ksnprintf(msg, sizeof msg, "R6=%08X R7=%08X R8=%08X R9=%08X R10=%08X R11=%08X\r\n",
                        r[6], r[7], r[8], r[9], r[10], r[11]);
        bcon_write(msg, len);
        len = ksnprintf(msg, sizeof msg, "R12=%08X SP=%08X LR=%08X CPSR=%08X\r\n", r[12], f->sp, f->lr, f->cpsr);
    }
    bcon_write(msg, len);
    DV.indos = 0;
    DV.errormode = 0;
    cur_ctx = 0;
    terminate(f, 0, 1);
    in_fault = 0;
}

void fault_install(void)
{
    for (int i = 0; i < 4; i++) {
        bios_fault[i] = (int_handler)IVT[fault_ints[i]];
        IVT[fault_ints[i]] = (uint32_t)fault_handler;
    }
}
