/* ucon.c - K_UCON.SYS, a test DEVICE= driver named CON that replaces the
   built-in console (as ANSI.SYS does): output is upper-cased, input is passed
   to the old CON driver. */
#include "klib.h"

static void strategy(struct reqhdr *r);
static void interrupt(void);
__attribute__((section(".devhdr"), used))
struct devhdr ucon_header = {
    DEV_END, DEVA_CHAR | DEVA_STDIN | DEVA_STDOUT | DEVA_SPECIAL, 0, strategy, interrupt, "CON     "
};

static struct devhdr *oldcon;
static struct reqhdr *req;
static int_handler old29;

static void tty(int c)
{
    struct armregs r = { 0 };
    if (c >= 'a' && c <= 'z') c -= 32;
    r.r0 = 0x0E00 | (c & 0xFF);
    r.r1 = 7;
    kint(0x10, &r);
}

static void int29(struct armregs *f) { tty(f->r0 & 0xFF); }

static void strategy(struct reqhdr *r) { req = r; }

extern char __bss_end__[];

static void interrupt(void)
{
    struct reqhdr *r = req;
    if (r->cmd == CMD_INIT) {
        struct req_init *q = (struct req_init *)r;
        struct armregs a = { 0 };
        a.r0 = 0x5200;
        kint(0x21, &a);
        uint8_t *lol = (uint8_t *)a.r1;
        oldcon = (struct devhdr *)(lol[0x0C] | (lol[0x0D] << 8) | (lol[0x0E] << 16) | ((uint32_t)lol[0x0F] << 24));
        old29 = (int_handler)IVT[0x29];
        IVT[0x29] = (uint32_t)int29;
        static const char msg[] = "Uppercase console driver installed\r\n";
        memset(&a, 0, sizeof a);
        a.r0 = 0x4000; a.r1 = 1; a.r2 = sizeof msg - 1; a.r3 = (uint32_t)msg;
        kint(0x21, &a);
        q->brk = (uint32_t)__bss_end__;
        r->status = RS_DONE;
        return;
    }
    if (r->cmd == CMD_WRITE || r->cmd == CMD_WRITEV) {
        struct req_rw *q = (struct req_rw *)r;
        const uint8_t *p = (const uint8_t *)q->addr;
        for (unsigned i = 0; i < q->count; i++) tty(p[i]);
        r->status = RS_DONE;
        return;
    }
    /* everything else: the old console */
    oldcon->strategy(r);
    oldcon->interrupt();
}
