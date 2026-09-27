/*
 * startup.c - C runtime start-up and memory for ARM-DOS programs.
 *
 * __armdos_start (called from crt0.S):
 *   1. record PSP / load base / block end, DOS version
 *   2. shrink our DOS memory block to image+bss+stack (like the MS C
 *      start-up), so child programs (spawn, system) have room
 *   3. set up the heap: it starts at the stack top and grows by enlarging
 *      the DOS block (INT 21h AH=4Ah); when conventional memory runs out it
 *      continues in one big XMS block (INT 2Fh AX=4300h/4310h, ARCH.md 10)
 *   4. environ from the environment segment, argv[0] from the program path
 *      that follows the environment strings, argc/argv from the command tail
 *   5. exit(main(argc, argv, environ))
 */
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include "armdos.h"
#include "libdos.h"

struct psp *_armdos_psp;
uint8_t    *_armdos_base;
uint8_t    *_armdos_blockend;
uint8_t    *_armdos_heap_start;
const char *_armdos_progpath = "";
unsigned int  _psp;             /* MS C: PSP segment */
unsigned char _osmajor, _osminor;
unsigned int  _osversion;       /* MS C: minor<<8 | major, as in AX from 30h */

__attribute__((weak)) unsigned _armdos_xms_kb = 0;
__attribute__((weak)) unsigned _armdos_heap_grow = 4096;
/* Opt-in (define it = 1 in your program): with no XMS driver, continue the
 * heap in the raw extended memory INT 15h AH=88h reports, from 1 MB up - what
 * pre-XMS DOS extenders did. HIMEM.SYS hooks AH=88h to report 0, so this
 * never takes memory an XMS driver owns. (Added for DOOM.) */
__attribute__((weak)) unsigned _armdos_raw_extmem = 0;

extern char **environ;
extern int main(int, char **, char **);

/* ---------------------------------------------------------------- heap ---- */

static uint8_t *heap_ptr;       /* current break */
static uint8_t *heap_limit;     /* end of the current region */
static uint8_t *heap_base;      /* start of the current region */
static int      heap_in_xms;
static void    *xms_entry;
static uint16_t xms_handle;
static int      xms_tried;
static unsigned long xms_bytes;
static armdos_vect_t old_int23;

static int dos_setblock(unsigned paras, unsigned *maxparas)
{
    struct armregs r = {0};
    r.r0 = 0x4A00;
    r.r1 = paras;
    r.r8 = _psp;                /* ES = block segment (ARCH.md 5.1) */
    if (_armdos_int21(&r)) {
        if (maxparas) *maxparas = r.r1 & 0xFFFF;
        /* DOS 2.1-6 leave a block that failed to grow as large as possible
         * (RBIL); put it back to what we had so children keep their room. */
        memset(&r, 0, sizeof r);
        r.r0 = 0x4A00;
        r.r1 = (uint32_t)(_armdos_blockend - (uint8_t *)_armdos_psp) >> 4;
        r.r8 = _psp;
        _armdos_int21(&r);
        return -1;
    }
    return 0;
}

void *armdos_xms_entry(void)
{
    struct armregs r = {0};
    r.r0 = 0x4300;
    _armdos_int2f(&r);
    if ((r.r0 & 0xFF) != 0x80)
        return 0;
    memset(&r, 0, sizeof r);
    r.r0 = 0x4310;
    _armdos_int2f(&r);
    return (void *)r.r1;        /* ES:BX -> BX is the flat entry (ES = 0) */
}

unsigned long armdos_xms_heap_size(void) { return xms_bytes; }

static uint32_t xms(unsigned ah, unsigned dx, struct armregs *r)
{
    memset(r, 0, sizeof *r);
    r->r0 = ah << 8;
    r->r3 = dx;
    return _armdos_farcall(xms_entry, r) & 0xFFFF;
}

static void xms_release(void)
{
    struct armregs r;
    if (!xms_handle || !xms_entry) return;
    xms(0x0D, xms_handle, &r);          /* unlock */
    xms(0x0A, xms_handle, &r);          /* free */
    xms_handle = 0;
}

/* Ctrl-C while we own an XMS block: give it back (DOS does not know about
 * it), then chain to the previous INT 23h handler. If that returns, ask DOS
 * to abort the program (carry set) - our heap is gone. */
static void int23_handler(struct armregs *f)
{
    xms_release();
    armdos_callold(old_int23, f);
    f->cpsr |= ARM_CPSR_C;
}

/* Get one big XMS block to continue the heap in. need = bytes required. */
static int xms_heap(size_t need)
{
    struct armregs r;
    unsigned kb, largest;
    uint32_t addr;

    if (xms_tried) return -1;
    xms_tried = 1;
    if (_armdos_xms_kb == ~0u) return -1;
    xms_entry = armdos_xms_entry();
    if (!xms_entry) {
        if (!_armdos_raw_extmem) return -1;
        memset(&r, 0, sizeof r);
        r.r0 = 0x8800;                                   /* INT 15h AH=88h */
        if (_armdos_intr(0x15, &r)) return -1;
        /* Never the first 64 KB (the HMA: the ARM-PC BIOS keeps its data
         * and stacks there): [1 MB + 64 KB, 1 MB + AH=88h KB). */
        kb = r.r0 & 0xFFFF;
        if (kb <= 64) return -1;
        kb -= 64;
        if (_armdos_xms_kb && _armdos_xms_kb < kb) kb = _armdos_xms_kb;
        if ((unsigned long)kb * 1024 < need + 16) return -1;
        xms_bytes = (unsigned long)kb * 1024;
        heap_base = heap_ptr = (uint8_t *)0x110000;
        heap_limit = heap_ptr + xms_bytes;
        heap_in_xms = 1;
        return 0;
    }

    if (!xms(0x08, 0, &r) && (r.r1 & 0xFF)) return -1;   /* query free */
    largest = r.r0 & 0xFFFF;
    kb = _armdos_xms_kb ? _armdos_xms_kb : largest;
    if (kb > largest) kb = largest;
    /* too big a request: a smaller one may still fit later (DOOM tries 8 MiB, then 7, ...) */
    if ((unsigned long)kb * 1024 < need + 16) { xms_tried = 0; return -1; }

    if (xms(0x09, kb, &r) != 1) return -1;               /* allocate */
    xms_handle = r.r3 & 0xFFFF;
    if (xms(0x0C, xms_handle, &r) != 1) {                /* lock */
        xms(0x0A, xms_handle, &r);
        xms_handle = 0;
        return -1;
    }
    addr = ((r.r3 & 0xFFFF) << 16) | (r.r1 & 0xFFFF);    /* DX:BX */

    old_int23 = armdos_getvect(0x23);
    armdos_setvect(0x23, int23_handler);

    xms_bytes = (unsigned long)kb * 1024;
    heap_base = heap_ptr = (uint8_t *)((addr + 7) & ~7u);
    heap_limit = (uint8_t *)(addr + xms_bytes);
    heap_in_xms = 1;
    return 0;
}

void *_sbrk(ptrdiff_t incr)
{
    uint8_t *old = heap_ptr;

    if (incr <= 0) {
        if (heap_ptr + incr < heap_base) {
            errno = EINVAL;
            return (void *)-1;
        }
        heap_ptr += incr;
        return old;
    }
    if ((size_t)incr <= (size_t)(heap_limit - heap_ptr)) {
        heap_ptr += incr;
        return old;
    }
    if (!heap_in_xms) {
        /* grow the DOS block */
        uint32_t want = (uint32_t)(heap_ptr + incr - (uint8_t *)_armdos_psp);
        uint32_t g = _armdos_heap_grow ? _armdos_heap_grow : 16;
        uint32_t round = (want + g - 1) / g * g;
        unsigned maxp;
        if (round < 0x100000 && dos_setblock((round + 15) >> 4, &maxp) == 0) {
            heap_limit = (uint8_t *)_armdos_psp + ((round + 15) & ~15u);
            _armdos_blockend = heap_limit;
            heap_ptr += incr;
            return old;
        }
        if (want < 0x100000 && dos_setblock((want + 15) >> 4, &maxp) == 0) {
            heap_limit = (uint8_t *)_armdos_psp + ((want + 15) & ~15u);
            _armdos_blockend = heap_limit;
            heap_ptr += incr;
            return old;
        }
        /* conventional memory exhausted: continue in extended memory */
        if (xms_heap((size_t)incr) == 0) {
            old = heap_ptr;
            heap_ptr += incr;
            return old;
        }
    }
    errno = ENOMEM;
    return (void *)-1;
}

unsigned long _memavl(void)
{
    unsigned maxp = 0;
    if (heap_in_xms) return (unsigned long)(heap_limit - heap_ptr);
    dos_setblock(0xFFFF, &maxp);            /* fails; BX = max for our block */
    uint8_t *top = (uint8_t *)_armdos_psp + ((uint32_t)maxp << 4);
    if (top < heap_limit) top = heap_limit;
    return (unsigned long)(top - heap_ptr);
}

unsigned long _memmax(void)
{
    struct armregs r = {0};
    r.r0 = 0x4800;
    r.r1 = 0xFFFF;
    _armdos_int21(&r);                      /* fails; BX = largest free block */
    return (unsigned long)(r.r1 & 0xFFFF) << 4;
}

/* ----------------------------------------------------------- exit ------- */

void _exit(int code)
{
    struct armregs r = {0};
    xms_release();
    r.r0 = 0x4C00 | (code & 0xFF);
    for (;;)
        _armdos_int21(&r);
}

/* ------------------------------------------------------ argv / environ -- */

static int is_space(int c) { return c == ' ' || c == '\t'; }

/* Microsoft C rules: whitespace separates; "..." groups; \" is a literal
 * quote; 2n backslashes before a quote -> n backslashes. Returns argc; if
 * argv is non-null, fills it and writes the strings into buf. */
static int parse_tail(const char *s, int n, char **argv, char *buf)
{
    int argc = 0, i = 0;
    for (;;) {
        while (i < n && is_space(s[i])) i++;
        if (i >= n) break;
        if (argv) argv[argc] = buf;
        argc++;
        int inq = 0;
        while (i < n && (inq || !is_space(s[i]))) {
            if (s[i] == '\\') {
                int bs = 0;
                while (i < n && s[i] == '\\') { bs++; i++; }
                if (i < n && s[i] == '"') {
                    for (int k = 0; k < bs / 2; k++) if (buf) *buf++ = '\\';
                    if (bs & 1) { if (buf) *buf++ = '"'; i++; }
                } else {
                    for (int k = 0; k < bs; k++) if (buf) *buf++ = '\\';
                }
            } else if (s[i] == '"') {
                if (inq && i + 1 < n && s[i + 1] == '"') { if (buf) *buf++ = '"'; i += 2; }
                else { inq = !inq; i++; }
            } else {
                if (buf) *buf++ = s[i];
                i++;
            }
        }
        if (buf) *buf++ = 0;
    }
    return argc;
}

static char *empty_env[1];

static char **setup_environ(void)
{
    const char *env;
    int count = 0;
    char **ev;

    if (!_armdos_psp->envseg)
        return empty_env;
    env = (const char *)ARMDOS_SEG2PTR(_armdos_psp->envseg);
    for (const char *p = env; *p; p += strlen(p) + 1)
        count++;
    ev = malloc((count + 1) * sizeof(char *));
    if (!ev) return empty_env;
    count = 0;
    const char *p = env;
    for (; *p; p += strlen(p) + 1)
        ev[count++] = (char *)p;
    ev[count] = 0;
    /* DOS 3+: after the double NUL, a word count, then the program path */
    p++;
    if ((p[0] | (p[1] << 8)) >= 1)
        _armdos_progpath = p + 2;
    return ev;
}

extern void _armdos_io_init(void);
extern void __libc_init_array(void);
extern uint32_t _armdos_start_ticks;

void __armdos_start(struct psp *psp, uint8_t *base, uint8_t *blockend, uint8_t *stacktop)
{
    struct armregs r = {0};
    int argc;
    char **argv;
    const char *tail;
    int n;

    _armdos_psp = psp;
    _armdos_base = base;
    _armdos_blockend = blockend;
    _psp = (uint32_t)psp >> 4;
    _armdos_start_ticks = ARMDOS_BIOS_TICKS;

    r.r0 = 0x3000;
    _armdos_int21(&r);
    _osmajor = r.r0 & 0xFF;
    _osminor = (r.r0 >> 8) & 0xFF;
    _osversion = r.r0 & 0xFFFF;

    /* the heap starts at the top of the stack area */
    _armdos_heap_start = heap_base = heap_ptr = (uint8_t *)(((uint32_t)stacktop + 7) & ~7u);
    heap_limit = blockend;
    {
        uint32_t keep = (uint32_t)(heap_ptr - (uint8_t *)psp);
        keep = (keep + 15) & ~15u;
        if ((uint8_t *)psp + keep < blockend && dos_setblock(keep >> 4, 0) == 0)
            heap_limit = _armdos_blockend = (uint8_t *)psp + keep;
    }

    _armdos_io_init();

    environ = setup_environ();

    tail = (const char *)psp->cmdtail + 1;
    n = psp->cmdtail[0];
    if (n > 126) n = 126;
    for (int i = 0; i < n; i++)
        if (tail[i] == '\r') { n = i; break; }
    argc = parse_tail(tail, n, 0, 0) + 1;
    argv = malloc((argc + 1) * sizeof(char *) + n + argc + 1);
    if (!argv) {
        static char *noargv[2];
        argc = 1;
        argv = noargv;
    } else {
        parse_tail(tail, n, argv + 1, (char *)(argv + argc + 1));
    }
    argv[0] = (char *)_armdos_progpath;
    argv[argc] = 0;

    __libc_init_array();     /* constructors; .fini_array is not run (saves ~800 bytes of atexit) */
    exit(main(argc, argv, environ));
}
