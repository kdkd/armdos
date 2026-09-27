/*
 * int21.c - the INT 21h dispatcher (DOS/MSDISP.ASM, MS_CODE.ASM) and the
 * error-return machinery (SYS_RET_ERR / ETAB_LK / CAL_LK).
 *
 * INT 21h runs on the SVC stack as a C function of the frame the BIOS built
 * (ARCH.md 5, 14.5).  A terminate or a Ctrl-C deep inside the kernel unwinds
 * with k_longjmp to the INT 21h entry of the program's call (the "base"
 * call), as DOS resets its stack from user_SP.
 */
#include "dos.h"

struct ctx *cur_ctx;
uint16_t cur_psp;
uint32_t cur_dta;
uint8_t cur_drive;
uint8_t break_on, verify_on, alloc_strategy, switchar = '/';
uint16_t exit_code;
struct exterr exterr;
uint8_t fail_err;
uint8_t no_i24;

#define JMP_DONE    1
#define JMP_RESTART 2

/* ---------------------------------------------- error code mapping */

/* INT 21h calls and the errors each may return; the last one is what a
 * "foreign" error becomes (MS_TABLE.ASM I21_MAP_E_TAB) */
static const uint8_t errmap[] = {
    0x38, 2, 1, 2,
    0x39, 3, 3, 2, 5,
    0x3A, 4, 16, 3, 2, 5,
    0x3B, 2, 2, 3,
    0x3C, 4, 3, 2, 4, 5,
    0x3D, 6, 3, 2, 12, 4, 26, 5,
    0x3E, 1, 6,
    0x3F, 2, 6, 5,
    0x40, 2, 6, 5,
    0x41, 3, 3, 2, 5,
    0x42, 2, 6, 1,
    0x43, 4, 3, 2, 1, 5,
    0x44, 5, 15, 13, 1, 6, 5,
    0x45, 2, 6, 4,
    0x46, 2, 6, 4,
    0x47, 2, 26, 15,
    0x48, 2, 7, 8,
    0x49, 2, 7, 9,
    0x4A, 3, 7, 9, 8,
    0x4B, 8, 3, 1, 2, 4, 11, 10, 8, 5,
    0x4E, 3, 3, 2, 18,
    0x4F, 1, 18,
    0x56, 5, 17, 3, 2, 16, 5,
    0x57, 4, 6, 8, 13, 1,
    0x58, 1, 1,
    0x5A, 4, 3, 2, 4, 5,
    0x5B, 5, 80, 3, 2, 4, 5,
    0x5C, 4, 6, 1, 36, 33,
    0x65, 2, 1, 2,
    0x66, 2, 1, 2,
    0x68, 1, 6,
    0x67, 3, 4, 8, 1,
    0x6C, 10, 3, 2, 12, 4, 80, 8, 26, 13, 1, 5,
    0x69, 4, 15, 13, 1, 5,
    0xFF
};

/* error -> class, action, locus (0xFF = keep) (ERR_TABLE_21) */
static const uint8_t errclass[] = {
    1, 7, 4, 0xFF,   2, 8, 3, 2,   3, 8, 3, 2,   4, 1, 4, 1,   5, 3, 3, 0xFF,
    6, 7, 4, 1,      7, 7, 5, 5,   8, 1, 4, 5,   9, 7, 4, 5,   10, 7, 4, 5,
    11, 9, 3, 1,     12, 7, 4, 1,  13, 9, 4, 1,  15, 8, 3, 2,  16, 3, 3, 2,
    17, 13, 3, 2,    18, 8, 3, 2,  80, 12, 3, 2, 32, 10, 2, 2, 33, 10, 2, 2,
    84, 1, 4, 0xFF,  86, 3, 3, 1,  82, 1, 4, 2,  50, 9, 3, 3,  85, 12, 3, 3,
    87, 9, 3, 1,     83, 13, 4, 1, 36, 1, 4, 5,  38, 1, 4, 1,  39, 1, 4, 1,
    90, 13, 4, 2,
    0xFF, 0xFF, 0xFF, 0xFF
};

void set_exterr(int err, int class_, int action, int locus)
{
    exterr.code = err;
    exterr.class_ = class_;
    exterr.action = action;
    exterr.locus = locus;
}

static void cal_lk(int err)
{
    const uint8_t *t = errclass;
    while (t[0] != 0xFF && t[0] != err) t += 4;
    if (t[1] != 0xFF) exterr.class_ = t[1];
    if (t[2] != 0xFF) exterr.action = t[2];
    if (t[3] != 0xFF) exterr.locus = t[3];
}

void sys_err(struct armregs *f, int err)
{
    int fn = cur_ctx ? cur_ctx->fn : AH(f);
    exterr.code = err;
    int ret = err;
    const uint8_t *t = errmap;
    while (t[0] != 0xFF) {
        if (t[0] == fn) {
            int n = t[1], ok = 0;
            for (int i = 0; i < n; i++) if (t[2 + i] == err) ok = 1;
            if (!ok) ret = t[1 + n];
            break;
        }
        t += 2 + t[1];
    }
    if (fail_err) exterr.code = E_FAIL24;
    cal_lk(exterr.code);
    f->r0 = ret;
    f->cpsr |= CPSR_C;
}

/* FCB calls return AL=FFh but still set the extended error (FCB_RET_ERR) */
void fcb_err(int err)
{
    exterr.code = fail_err ? E_FAIL24 : err;
    cal_lk(exterr.code);
}

void sys_ok(struct armregs *f)
{
    f->cpsr &= ~CPSR_C;
}

/* -------------------------------------------------------------- idle */

void dos_idle(void)
{
    if (DV.errormode) return;
    struct armregs r = { 0 };
    kint(0x28, &r);
}

/* unwind to the program's INT 21h (after terminate() rewrote its frame) */
__attribute__((noreturn)) void abort_to_base(void)
{
    struct ctx *c = cur_ctx;
    while (c && !c->base && c->prev) c = c->prev;
    k_longjmp(&c->jb, JMP_DONE);
}

/* ------------------------------------------------------ small calls */

static void fn_version(struct armregs *f)
{
    if (AL(f) == 1) f->r1 = 0x0000;     /* 4.00: BH = revision flags? no: OEM */
    f->r0 = 0x0004;                     /* AL = 4, AH = 0 */
    f->r1 = 0xFF00;                     /* BH = OEM (Microsoft), BL:CX = serial 0 */
    f->r2 = 0;
}

static void fn_break(struct armregs *f)
{
    switch (AL(f)) {
    case 0: set_dl(f, break_on); break;
    case 1: break_on = DL(f) ? 1 : 0; break;
    case 2: { int o = break_on; break_on = DL(f) ? 1 : 0; set_dl(f, o); break; }
    case 3: case 4: set_dl(f, 0); break;             /* CPSW: stub */
    case 5: set_dl(f, LOL.bootdrive); break;
    default: set_al(f, 0xFF); break;
    }
}

/* a terminate issued from inside a handler the kernel called (INT 23h/24h)
   ends the process of the call being served: unwind to it */
static void do_terminate(struct armregs *f, int code, int type)
{
    struct ctx *b = base_ctx();
    if (b && b != cur_ctx && b->base) {
        terminate(b->f, code, type);
        abort_to_base();
    }
    terminate(f, code, type);
}

static void dispatch(struct armregs *f)
{
    int ah = AH(f);
    switch (ah) {
    case 0x00: do_terminate(f, 0, 0); break;
    case 0x01: case 0x02: case 0x03: case 0x04: case 0x05: case 0x06:
    case 0x07: case 0x08: case 0x09: case 0x0A: case 0x0B: case 0x0C:
        char_functions(f); break;
    case 0x0D: flush_bufs(-1); invalidate_bufs(-1); break;
    case 0x0E:
        if ((int)DL(f) < n_cds && drive_usable(DL(f))) cur_drive = DL(f);
        set_al(f, LOL.lastdrive);
        break;
    case 0x0F: case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: case 0x15:
    case 0x16: case 0x17: case 0x21: case 0x22: case 0x23: case 0x24: case 0x27:
    case 0x28: case 0x29:
        fcb_functions(f); break;
    case 0x18: case 0x1D: case 0x1E: case 0x20: case 0x61: case 0x6B:
        set_al(f, 0); break;
    case 0x19: set_al(f, cur_drive); break;
    case 0x1A: cur_dta = f->r3; break;
    case 0x2F: f->r1 = cur_dta; f->r8 = 0; break;
    case 0x25: IVT[AL(f)] = f->r3; break;
    case 0x35: f->r1 = IVT[AL(f)]; f->r8 = 0; break;
    case 0x30: fn_version(f); break;
    case 0x33: fn_break(f); break;
    case 0x34: f->r1 = (uint32_t)&DV.indos; f->r8 = 0; break;
    case 0x37:
        if (AL(f) == 0) { set_al(f, 0); set_dl(f, switchar); }
        else if (AL(f) == 1) { switchar = DL(f); set_al(f, 0); }
        else if (AL(f) == 2 || AL(f) == 3) { set_dl(f, 0xFF); set_al(f, 0); }   /* \DEV availability */
        else set_al(f, 0xFF);
        break;
    case 0x2E: verify_on = AL(f) ? 1 : 0; break;
    case 0x54: set_al(f, verify_on); break;
    case 0x52: f->r1 = (uint32_t)&LOL.dpb_head; f->r8 = 0; break;
    case 0x59:
        f->r0 = exterr.code;
        f->r1 = (exterr.class_ << 8) | exterr.action;
        f->r2 = (f->r2 & 0xFF) | (exterr.locus << 8);
        f->r5 = exterr.volptr;
        break;
    case 0x44: ioctl_fn(f); break;
    case 0x48: case 0x49: case 0x4A: case 0x58:
        mem_functions(f); break;
    case 0x4B: exec_fn(f); break;
    case 0x4C: do_terminate(f, AL(f), 0); break;
    case 0x31: case 0x4D: case 0x50: case 0x51: case 0x62: case 0x26: case 0x55:
        proc_functions(f); break;
    case 0x4E: find_first(f); break;
    case 0x4F: find_next(f); break;
    case 0x39: case 0x3A: case 0x3B: case 0x3C: case 0x3D: case 0x3E: case 0x3F:
    case 0x40: case 0x41: case 0x42: case 0x43: case 0x45: case 0x46: case 0x47:
    case 0x56: case 0x57: case 0x5A: case 0x5B: case 0x5C: case 0x60: case 0x67:
    case 0x68: case 0x6A: case 0x6C:
        file_functions(f); break;
    default:
        misc_functions(f); break;
    }
}

void int21_handler(struct armregs *f)
{
    struct ctx c;
    c.prev = cur_ctx;
    c.f = f;
    c.base = (f->cpsr & MODE_MASK) != MODE_SVC;
    memcpy(&c.orig, f, sizeof *f);
    memset(c.checked, 0, sizeof c.checked);
    uint8_t saved_indos = DV.indos, saved_err = DV.errormode;
    cur_ctx = &c;
    irq_on();
    if (c.base && cur_psp) PSP(cur_psp)->savedsp = f->sp;

    int r = k_setjmp(&c.jb);
    if (r != JMP_DONE) {
        c.fn = AH(f);
        /* 33h 50h 51h 62h are served "outside" DOS (MSDISP.ASM): no InDOS, no
           ^C check - TSRs call them from interrupt handlers */
        int outside = c.fn == 0x33 || c.fn == 0x50 || c.fn == 0x51 || c.fn == 0x62;
        DV.indos = saved_indos + (outside ? 0 : 1);
        fail_err = 0;
        no_i24 = 0;
        f->cpsr &= ~CPSR_C;
        if (break_on && !outside && (c.fn < 0x01 || c.fn > 0x0C)) check_ctrl_c();
        dispatch(f);
    }
    DV.indos = c.base ? 0 : saved_indos;
    DV.errormode = c.base ? 0 : saved_err;
    cur_ctx = c.prev;
}

/* redo the base call with the registers the INT 23h handler left */
__attribute__((noreturn)) void restart_base(const struct armregs *regs)
{
    struct ctx *c = cur_ctx;
    while (c && !c->base && c->prev) c = c->prev;
    struct armregs *f = c->f;
    memcpy(f, regs, 13 * 4);            /* r0-r12; pc/sp/cpsr stay */
    f->cpsr = c->orig.cpsr & ~CPSR_C;
    cur_ctx = c;
    k_longjmp(&c->jb, JMP_RESTART);
}

struct ctx *base_ctx(void)
{
    struct ctx *c = cur_ctx;
    while (c && !c->base && c->prev) c = c->prev;
    return c;
}
