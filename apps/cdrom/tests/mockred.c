/*
 * MOCKRED.COM - a mock network redirector for testing the kernel's INT 2Fh
 * AH=11h callouts (apps/cdrom/README.md).
 *
 *   MOCKRED [d:\path\file]     claim drive D: (TSR); the optional file is
 *                              loaded into memory and served as D:\<name>
 *
 * Drive D: is a tiny read-only file system in memory:
 *   D:\ (label MOCKCD)  HELLO.TXT  README.TXT  SUB\  SUB\INNER.TXT  [file]
 * Every write-type call answers 5 (access denied).
 *
 * Freestanding (no C library), like apps/popup.
 */
#include <stdint.h>
#include <stddef.h>
#include "armdos.h"

#define PACKED __attribute__((packed))

void *memcpy(void *d, const void *s, size_t n) { uint8_t *a = d; const uint8_t *b = s; while (n--) *a++ = *b++; return d; }
void *memset(void *d, int c, size_t n) { uint8_t *a = d; while (n--) *a++ = c; return d; }
static int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }

struct sft {                    /* kernel/inc/kabi.h, DOS 4 SF.INC */
    uint16_t ref_count, mode;
    uint8_t  attr;
    uint16_t flags;
    uint32_t devptr;
    uint16_t first_cluster, time, date;
    uint32_t size, position;
    uint16_t rel_cluster;
    uint32_t dir_sector;
    uint8_t  dir_index;
    char     name[11];
    uint32_t share_prev;
    uint16_t machine, owner_psp, mft, last_cluster;
    uint32_t ifs;
} PACKED;

#define DRIVE 3                 /* D: */
#define DATE ((13 << 9) | (6 << 5) | 1)     /* 06-01-1993 */
#define TIME (12 << 11)                     /* 12:00 */

struct mfile {
    char name[11];
    uint8_t attr;
    int8_t parent;              /* index of the directory, -1 = the root itself */
    const uint8_t *data;
    uint32_t size;
};

static const char hello[] = "Hello from drive D:, served by the mock redirector.\r\n";
static const char readme[] = "This drive is a test fixture.\r\n";
static const char inner[] = "Inside SUB.\r\n";

static struct mfile files[] = {
    { "           ", 0x10, -1, 0, 0 },                  /* 0 root */
    { "MOCKCD     ", 0x08, 0, 0, 0 },                   /* 1 label */
    { "HELLO   TXT", 0x21, 0, (const uint8_t *)hello, sizeof hello - 1 },
    { "README  TXT", 0x21, 0, (const uint8_t *)readme, sizeof readme - 1 },
    { "SUB        ", 0x10, 0, 0, 0 },                   /* 4 */
    { ".          ", 0x10, 4, 0, 0 },
    { "..         ", 0x10, 4, 0, 0 },
    { "INNER   TXT", 0x21, 4, (const uint8_t *)inner, sizeof inner - 1 },
    { "MISSING TXT", 0x21, 0, 0, 0 },                   /* 8: opening it says "not ready" (INT 2Fh 1206h) */
    { "           ", 0x00, 0, 0, 0 },                   /* 9: the loaded file (unused if name blank) */
};
#define NFILES (int)(sizeof files / sizeof files[0])

static armdos_vect_t old2f;
static uint32_t calls, crit_answers;

static uint8_t up(uint8_t c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }

/* "NAME.EXT" (up to a separator or NUL) -> FCB form; returns chars used */
static int to11(const char *s, char *n)
{
    int i = 0, k = 0;
    memset(n, ' ', 11);
    if (s[0] == '.') { n[0] = '.'; k = 1; if (s[1] == '.') { n[1] = '.'; k = 2; } return k; }
    while (s[k] && s[k] != '\\' && s[k] != '.') { if (s[k] == '*') { while (i < 8) n[i++] = '?'; k++; continue; } if (i < 8) n[i++] = up(s[k]); k++; }
    if (s[k] == '.') {
        k++; i = 8;
        while (s[k] && s[k] != '\\') { if (s[k] == '*') { while (i < 11) n[i++] = '?'; k++; continue; } if (i < 11) n[i++] = up(s[k]); k++; }
    }
    return k;
}

static int same11(const char *a, const char *b) { for (int i = 0; i < 11; i++) if (a[i] != b[i]) return 0; return 1; }
static int match11(const char *pat, const char *n) { for (int i = 0; i < 11; i++) if (pat[i] != '?' && pat[i] != n[i]) return 0; return 1; }

static int child(int dir, const char *n11)
{
    for (int i = 1; i < NFILES; i++)
        if (files[i].parent == dir && files[i].attr != 0x08 && files[i].name[0] != ' ' && same11(files[i].name, n11)) return i;
    return -1;
}

/* walk "D:\A\B": returns the entry index, -2 for a missing directory on the way, -1 for a missing last part.
   *lastpat gets the last component in FCB form, *pdir its directory */
static int walk(const char *path, char *lastpat, int *pdir)
{
    const char *p = path + 3;           /* after "D:\" */
    int dir = 0;
    if (!*p) { if (pdir) *pdir = -1; return 0; }
    for (;;) {
        char n[11];
        int k = to11(p, n);
        p += k;
        if (*p == '\\') {
            int c = child(dir, n);
            if (c < 0 || !(files[c].attr & 0x10)) return -2;
            dir = c; p++;
            continue;
        }
        if (lastpat) memcpy(lastpat, n, 11);
        if (pdir) *pdir = dir;
        return child(dir, n);
    }
}

static void n11_to_name(const char *n11, char *out)
{
    int k = 0, nl = 8, el = 11;
    while (nl > 0 && n11[nl - 1] == ' ') nl--;
    while (el > 8 && n11[el - 1] == ' ') el--;
    for (int i = 0; i < nl; i++) out[k++] = n11[i];
    if (el > 8) { out[k++] = '.'; for (int i = 8; i < el; i++) out[k++] = n11[i]; }
    out[k] = 0;
}

static void ok(struct armregs *f) { f->cpsr &= ~ARM_CPSR_C; }
static void err(struct armregs *f, int e) { f->r0 = e; f->cpsr |= ARM_CPSR_C; }

/* the find DTA: +00 drive|80h, +01 pattern (11), +0C sattr, +0D next index (u16), +0F dir (u16) */
static int attr_ok(int fattr, int sattr)
{
    if (sattr == 0x08) return fattr == 0x08;
    if (fattr & 0x08) return 0;
    return (fattr & ~sattr & 0x16) == 0;
}

static void find_step(struct armregs *f, uint8_t *t)
{
    int dir = t[0x0F] | (t[0x10] << 8);
    for (int i = t[0x0D] | (t[0x0E] << 8); i < NFILES; i++) {
        struct mfile *m = &files[i];
        if (m->name[0] == ' ') continue;
        if (m->attr == 0x08 ? dir != 0 : m->parent != dir) continue;
        if (!attr_ok(m->attr, t[0x0C]) || !match11((const char *)t + 1, m->name)) continue;
        t[0x0D] = i + 1; t[0x0E] = 0;
        t[0x15] = m->attr;
        t[0x16] = TIME & 0xFF; t[0x17] = TIME >> 8;
        t[0x18] = DATE & 0xFF; t[0x19] = DATE >> 8;
        t[0x1A] = m->size; t[0x1B] = m->size >> 8; t[0x1C] = m->size >> 16; t[0x1D] = m->size >> 24;
        memset(t + 0x1E, 0, 13);
        if (m->attr == 0x08) { memcpy(t + 0x1E, m->name, 8); int k = 8; while (k > 0 && t[0x1E + k - 1] == ' ') k--; t[0x1E + k] = 0;
            if (m->name[8] != ' ') { t[0x1E + k] = '.'; memcpy(t + 0x1F + k, m->name + 8, 3); } }
        else n11_to_name(m->name, (char *)t + 0x1E);
        ok(f);
        return;
    }
    t[0x0D] = 0xFF; t[0x0E] = 0xFF;
    err(f, 0x12);
}

static void int2f(struct armregs *f)
{
    if (((f->r0 >> 8) & 0xFF) != 0x11) { old2f(f); return; }
    int fn = f->r0 & 0xFF;
    const char *p1 = (const char *)f->r4;
    struct sft *s = (struct sft *)f->r5;
    calls++;
    /* path calls for other drives are not ours */
    int pathcall = fn == 0x01 || fn == 0x03 || fn == 0x05 || fn == 0x0E || fn == 0x0F || fn == 0x11 ||
                   fn == 0x13 || fn == 0x16 || fn == 0x17 || fn == 0x1B;
    if (pathcall && (up(p1[0]) != 'A' + DRIVE || p1[1] != ':')) { old2f(f); return; }
    switch (fn) {
    case 0x00: f->r0 = (f->r0 & ~0xFFu) | 0xFF; ok(f); return;
    case 0x05: {
        int i = walk(p1, 0, 0);
        if (i < 0 || !(files[i].attr & 0x10)) { err(f, 3); return; }
        ok(f); return;
    }
    case 0x06: case 0x07: ok(f); return;
    case 0x08: {
        struct mfile *m = &files[s->first_cluster];
        uint32_t n = f->r2 & 0xFFFF, pos = s->position;
        if (pos >= m->size) n = 0; else if (n > m->size - pos) n = m->size - pos;
        memcpy((void *)f->r3, m->data + pos, n);
        s->position = pos + n;
        f->r2 = n;
        ok(f); return;
    }
    case 0x0C: f->r0 = 1; f->r1 = 100; f->r2 = 2048; f->r3 = 0; ok(f); return;
    case 0x0F: {
        int i = walk(p1, 0, 0);
        if (i <= 0) { err(f, i == -2 ? 3 : 2); return; }
        f->r0 = files[i].attr; f->r1 = files[i].size; f->r2 = TIME; f->r3 = DATE;
        ok(f); return;
    }
    case 0x16: {
        int i = walk(p1, 0, 0);
        if (i <= 0) { err(f, i == -2 ? 3 : 2); return; }
        if (files[i].attr & 0x18) { err(f, 5); return; }
        if ((f->r2 & 7) != 0) { err(f, 5); return; }   /* read-only drive */
        if (i == 8) {                                   /* "drive not ready": DOS's INT 24h */
            for (;;) {
                struct armregs q; memset(&q, 0, sizeof q);
                q.r0 = 0x1206; q.r1 = (0x1E << 8) | DRIVE; q.r5 = 0x02; q.r4 = 0;
                _armdos_int2f(&q);
                crit_answers++;
                if ((q.r0 & 0xFF) == 1 && crit_answers < 3) continue;   /* retry */
                break;
            }
            err(f, 0x15); return;
        }
        struct mfile *m = &files[i];
        s->attr = m->attr;
        s->flags = 0x8000 | 0x40 | DRIVE;
        s->devptr = 0;
        s->first_cluster = i;
        s->time = TIME; s->date = DATE;
        s->size = m->size; s->position = 0;
        memcpy(s->name, m->name, 11);
        ok(f); return;
    }
    case 0x1B: {
        uint8_t *t = (uint8_t *)f->r1;
        char pat[11];
        int dir;
        int r = walk(p1, pat, &dir);
        if (r == -2) { err(f, 3); return; }
        if (dir < 0) { err(f, 3); return; }         /* a bare root */
        memset(t, 0, 43);
        t[0] = (DRIVE + 1) | 0x80;
        memcpy(t + 1, pat, 11);
        t[0x0C] = f->r2;
        t[0x0F] = dir;
        find_step(f, t);
        /* no match: 12h (no more files), as DOS reports it */
        return;
    }
    case 0x1C: find_step(f, (uint8_t *)f->r1); return;
    case 0x01: case 0x03: case 0x09: case 0x0E: case 0x11: case 0x13: case 0x17:
        err(f, 5); return;
    case 0x22: ok(f); return;
    }
    old2f(f);
}

/* ============================================================ install */

static void say(const char *s)
{
    struct armregs r; memset(&r, 0, sizeof r);
    r.r0 = 0x4000; r.r1 = 1; r.r2 = slen(s); r.r3 = (uint32_t)s;
    _armdos_int21(&r);
}

__attribute__((noreturn)) static void leave(int code)
{
    struct armregs r; memset(&r, 0, sizeof r);
    r.r0 = 0x4C00 | code;
    _armdos_int21(&r);
    for (;;) ;
}

void __armdos_start(struct psp *psp, uint8_t *base, uint8_t *blockend, uint8_t *stacktop)
{
    (void)base; (void)blockend;
    struct armregs r;
    uint8_t *end = (uint8_t *)(((uint32_t)stacktop + 15) & ~15u);

    /* optional file to serve */
    const uint8_t *tail = psp->cmdtail;
    char path[80]; int n = 0;
    for (int i = 1; i <= tail[0] && tail[i] != 0x0D; i++) if (tail[i] != ' ' || n) { if (tail[i] == ' ') break; if (n < 79) path[n++] = tail[i]; }
    path[n] = 0;
    if (n) {
        memset(&r, 0, sizeof r);
        r.r0 = 0x3D00; r.r3 = (uint32_t)path;
        if (_armdos_int21(&r)) { say("MOCKRED: cannot open the file\r\n"); leave(1); }
        int h = r.r0 & 0xFFFF;
        memset(&r, 0, sizeof r);
        r.r0 = 0x3F00; r.r1 = h; r.r2 = 0xFFF0; r.r3 = (uint32_t)end;
        _armdos_int21(&r);
        uint32_t got = r.r0 & 0xFFFF;
        memset(&r, 0, sizeof r); r.r0 = 0x3E00; r.r1 = h; _armdos_int21(&r);
        const char *b = path;
        for (const char *q = path; *q; q++) if (*q == '\\' || *q == ':') b = q + 1;
        to11(b, files[9].name);
        files[9].attr = 0x21; files[9].data = end; files[9].size = got;
        end += (got + 15) & ~15u;
    }

    /* claim D: through the List of Lists: +16h CDS array, +21h LASTDRIVE (unaligned: bytes) */
    memset(&r, 0, sizeof r);
    r.r0 = 0x5200; _armdos_int21(&r);
    uint8_t *lol = (uint8_t *)r.r1;
    uint8_t *cds = (uint8_t *)(lol[0x16] | (lol[0x17] << 8) | (lol[0x18] << 16) | ((uint32_t)lol[0x19] << 24));
    if (lol[0x21] <= DRIVE) { say("MOCKRED: LASTDRIVE too small\r\n"); leave(1); }
    uint8_t *c = cds + DRIVE * 0x58;
    if ((c[0x44] & 0x40)) { say("MOCKRED: drive D: is in use\r\n"); leave(1); }

    old2f = (armdos_vect_t)*(volatile uint32_t *)(0x2F * 4);
    armdos_disable();
    *(volatile uint32_t *)(0x2F * 4) = (uint32_t)int2f;
    memset(c, 0, 0x58);
    c[0] = 'A' + DRIVE; c[1] = ':'; c[2] = '\\';
    c[0x43] = 0x00; c[0x44] = 0xC0;             /* CDS_NET | CDS_VALID */
    c[0x49] = 0xFF; c[0x4A] = 0xFF;             /* cluster: none */
    c[0x4F] = 2;                                /* root backslash offset */
    armdos_enable();
    say("MOCKRED installed: drive D: = mock redirector\r\n");

    memset(&r, 0, sizeof r);
    r.r0 = 0x3100; r.r3 = ((uint32_t)end - (uint32_t)psp + 15) >> 4;
    _armdos_int21(&r);
    for (;;) ;
}
