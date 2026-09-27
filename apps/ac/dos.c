/*
 * dos.c - INT 21h wrappers and small helpers for ACMAIN.
 */
#include "acm.h"

int doserr;
int crit_err;

static int call21(struct armregs *r)
{
    if (_armdos_int21(r)) { doserr = r->r0 & 0xFFFF; return -1; }
    return 0;
}

int d_open(const char *p, int mode)
{
    struct armregs r = {0};
    r.r0 = 0x3D00 | (mode & 0xFF);
    r.r3 = (uint32_t)p;
    return call21(&r) ? -1 : (int)(r.r0 & 0xFFFF);
}

int d_creat(const char *p, int attr)
{
    struct armregs r = {0};
    r.r0 = 0x3C00;
    r.r2 = attr;
    r.r3 = (uint32_t)p;
    return call21(&r) ? -1 : (int)(r.r0 & 0xFFFF);
}

int d_read(int h, void *b, int n)
{
    struct armregs r = {0};
    r.r0 = 0x3F00; r.r1 = h; r.r2 = n; r.r3 = (uint32_t)b;
    return call21(&r) ? -1 : (int)(r.r0 & 0xFFFF);
}

int d_write(int h, const void *b, int n)
{
    struct armregs r = {0};
    r.r0 = 0x4000; r.r1 = h; r.r2 = n; r.r3 = (uint32_t)b;
    if (call21(&r)) return -1;
    return (int)(r.r0 & 0xFFFF);
}

void d_close(int h)
{
    struct armregs r = {0};
    r.r0 = 0x3E00; r.r1 = h;
    call21(&r);
}

uint32_t d_filesize(int h)
{
    struct armregs r = {0};
    r.r0 = 0x4202; r.r1 = h;
    if (call21(&r)) return 0;
    uint32_t sz = ((r.r3 & 0xFFFF) << 16) | (r.r0 & 0xFFFF);
    memset(&r, 0, sizeof r);
    r.r0 = 0x4200; r.r1 = h;
    call21(&r);
    return sz;
}

int d_seek(int h, uint32_t pos)
{
    struct armregs r = {0};
    r.r0 = 0x4200; r.r1 = h; r.r2 = pos >> 16; r.r3 = pos & 0xFFFF;
    return call21(&r);
}

int d_getattr(const char *p)
{
    struct armregs r = {0};
    r.r0 = 0x4300; r.r3 = (uint32_t)p;
    return call21(&r) ? -1 : (int)(r.r2 & 0xFFFF);
}

int d_setattr(const char *p, int a)
{
    struct armregs r = {0};
    r.r0 = 0x4301; r.r2 = a; r.r3 = (uint32_t)p;
    return call21(&r);
}

static int path1(int ah, const char *p)
{
    struct armregs r = {0};
    r.r0 = ah << 8; r.r3 = (uint32_t)p;
    return call21(&r);
}

int d_unlink(const char *p) { return path1(0x41, p); }
int d_rmdir(const char *p)  { return path1(0x3A, p); }
int d_mkdir(const char *p)  { return path1(0x39, p); }
int d_chdir(const char *p)  { return path1(0x3B, p); }

int d_rename(const char *a, const char *b)
{
    struct armregs r = {0};
    r.r0 = 0x5600; r.r3 = (uint32_t)a; r.r5 = (uint32_t)b;
    return call21(&r);
}

int d_getdrive(void)
{
    struct armregs r = {0};
    r.r0 = 0x1900;
    call21(&r);
    return r.r0 & 0xFF;
}

void d_setdrive(int d)
{
    struct armregs r = {0};
    r.r0 = 0x0E00; r.r3 = d;
    call21(&r);
}

int d_getcwd(int drive, char *out)
{
    struct armregs r = {0};
    out[0] = 'A' + drive; out[1] = ':'; out[2] = '\\';
    r.r0 = 0x4700; r.r3 = drive + 1; r.r4 = (uint32_t)(out + 3);
    crit_err = 0;
    if (call21(&r) || crit_err) { out[3] = 0; return -1; }
    if (out[3] == 0) out[2] = '\\';
    return 0;
}

int d_diskfree(int drive, uint32_t *total, uint32_t *freeb)
{
    struct armregs r = {0};
    r.r0 = 0x3600; r.r3 = drive + 1;
    crit_err = 0;
    _armdos_int21(&r);
    if ((r.r0 & 0xFFFF) == 0xFFFF || crit_err) return -1;
    uint32_t bpc = (r.r0 & 0xFFFF) * (r.r2 & 0xFFFF);
    *total = bpc * (r.r3 & 0xFFFF);
    *freeb = bpc * (r.r1 & 0xFFFF);
    return 0;
}

int d_getftime(int h, uint16_t *t, uint16_t *d)
{
    struct armregs r = {0};
    r.r0 = 0x5700; r.r1 = h;
    if (call21(&r)) return -1;
    *t = r.r2; *d = r.r3;
    return 0;
}

int d_setftime(int h, uint16_t t, uint16_t d)
{
    struct armregs r = {0};
    r.r0 = 0x5701; r.r1 = h; r.r2 = t; r.r3 = d;
    return call21(&r);
}

int d_findfirst(const char *spec, int attr, void *dta)
{
    struct armregs r = {0};
    r.r0 = 0x1A00; r.r3 = (uint32_t)dta;
    call21(&r);
    memset(&r, 0, sizeof r);
    r.r0 = 0x4E00; r.r2 = attr; r.r3 = (uint32_t)spec;
    crit_err = 0;
    return call21(&r) || crit_err ? -1 : 0;
}

int d_findnext(void *dta)
{
    struct armregs r = {0};
    r.r0 = 0x1A00; r.r3 = (uint32_t)dta;
    call21(&r);
    memset(&r, 0, sizeof r);
    r.r0 = 0x4F00;
    return call21(&r);
}

/* a drive letter that exists (IOCTL 4408h: removable?), B: only with 2 floppies */
int d_drive_valid(int d)
{
    struct armregs r = {0};
    if (d == 1) {
        uint16_t eq = *(volatile uint16_t *)0x410;
        if (!(eq & 1) || ((eq >> 6) & 3) == 0) return 0;
    }
    r.r0 = 0x4408; r.r1 = d + 1;
    if (_armdos_int21(&r)) return (r.r0 & 0xFFFF) != 0x0F;
    return 1;
}

const char *dos_errmsg(int e)
{
    switch (e) {
    case 2: return "File not found";
    case 3: return "Path not found";
    case 4: return "Too many open files";
    case 5: return "Access denied";
    case 8: return "Not enough memory";
    case 15: return "Invalid drive";
    case 16: return "Cannot remove the current directory";
    case 17: return "Not the same device";
    case 18: return "No more files";
    case 19: return "Disk is write protected";
    case 21: return "Drive not ready";
    case 29: return "Write fault";
    case 30: return "Read fault";
    case 39: return "Disk full";
    case 80: return "File exists";
    case 82: return "Cannot make directory entry";
    }
    return "DOS error";
}

/* ---- INT 23h / 24h ---- */
static void h23(struct armregs *f) { f->cpsr &= ~ARM_CPSR_C; }  /* ignore ^C */

static void h24(struct armregs *f)
{
    unsigned ah = AH(f);
    crit_err = (f->r5 & 0xFF) + 19;        /* DOS extended error = code + 19 */
    if (ah & 0x08) f->r0 = (f->r0 & ~0xFFu) | 3;        /* fail */
    else if (ah & 0x20) f->r0 = (f->r0 & ~0xFFu) | 0;   /* ignore */
    else f->r0 = (f->r0 & ~0xFFu) | 3;
}

void dos_hooks(void)
{
    armdos_setvect(0x23, h23);
    armdos_setvect(0x24, h24);
}

/* The largest block a program started from the command line will get:
   our own blocks (program, environment) count as free, since ACMAIN leaves
   memory before the loader runs the command. */
uint32_t mem_free_for_child(void)
{
    struct armregs r = {0};
    r.r0 = 0x5200;
    _armdos_int21(&r);
    uint16_t seg = *(uint16_t *)(r.r1 - 2);
    uint16_t me = (uint32_t)_armdos_psp >> 4;
    uint32_t best = 0, run = 0;
    int guard = 0;
    for (;;) {
        struct mcb *m = (struct mcb *)ARMDOS_SEG2PTR(seg);
        if ((m->type != 'M' && m->type != 'Z') || ++guard > 2000) break;
        int freeb = m->owner == 0 || (!standalone && m->owner == me);
        if (freeb) run += (run ? 16 : 0) + ((uint32_t)m->size << 4);
        else run = 0;
        if (run > best) best = run;
        if (m->type == 'Z') break;
        seg += m->size + 1;
    }
    return best;
}

/* ---- helpers ---- */
void u2s(char *o, uint32_t v)
{
    char t[12];
    int n = 0;
    do { t[n++] = '0' + v % 10; v /= 10; } while (v);
    while (n) *o++ = t[--n];
    *o = 0;
}

void u2s_pad(char *o, uint32_t v, int w)
{
    char t[12];
    u2s(t, v);
    int l = strlen(t);
    while (l < w) { *o++ = ' '; w--; }
    strcpy(o, t);
}

void commas(char *out, uint32_t v)
{
    char t[12];
    u2s(t, v);
    int l = strlen(t), k = 0;
    for (int i = 0; i < l; i++) {
        out[k++] = t[i];
        if ((l - i - 1) % 3 == 0 && i != l - 1) out[k++] = ',';
    }
    out[k] = 0;
}

static int upc(int c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }
void str_upper(char *s) { for (; *s; s++) *s = upc(*s); }
void str_lower(char *s) { for (; *s; s++) if (*s >= 'A' && *s <= 'Z') *s += 32; }

static int match_part(const char *p, int pl, const char *s, int sl)
{
    int i = 0, j = 0;
    while (i < pl) {
        if (p[i] == '*') return 1;
        if (j >= sl) { if (p[i] != '?') return 0; i++; continue; }
        if (p[i] != '?' && upc(p[i]) != upc(s[j])) return 0;
        i++; j++;
    }
    return j >= sl;
}

/* DOS-style 8.3 wildcard match ("*.*", "a?c.*", "*.exe") */
int wildmatch(const char *pat, const char *name)
{
    const char *pd = strchr(pat, '.'), *nd = strchr(name, '.');
    int pl = pd ? pd - pat : (int)strlen(pat);
    int nl = nd ? nd - name : (int)strlen(name);
    if (!match_part(pat, pl, name, nl)) return 0;
    const char *pe = pd ? pd + 1 : "", *ne = nd ? nd + 1 : "";
    if (!pd) return (pl && pat[pl - 1] == '*') || !*ne;
    return match_part(pe, strlen(pe), ne, strlen(ne));
}

void path_join(char *out, const char *dir, const char *name)
{
    int l = strlen(dir);
    if (out != dir) memmove(out, dir, l + 1);
    if (l && out[l - 1] != '\\') out[l++] = '\\';
    strcpy(out + l, name);
}

const char *basename_(const char *p)
{
    const char *b = strrchr(p, '\\');
    if (!b) b = strchr(p, ':');
    return b ? b + 1 : p;
}

int is_exec(const char *name)
{
    const char *d = strrchr(name, '.');
    if (!d) return 0;
    return !strcmp(d, ".COM") || !strcmp(d, ".EXE") || !strcmp(d, ".BAT");
}

void fmt_date(char *o, uint16_t d)
{
    int m = (d >> 5) & 15, dd = d & 31, y = ((d >> 9) + 80) % 100;
    o[0] = m >= 10 ? '0' + m / 10 : ' ';
    o[1] = '0' + m % 10; o[2] = '-';
    o[3] = '0' + dd / 10; o[4] = '0' + dd % 10; o[5] = '-';
    o[6] = '0' + y / 10; o[7] = '0' + y % 10; o[8] = 0;
}

void fmt_time(char *o, uint16_t t)
{
    int h = t >> 11, m = (t >> 5) & 63;
    char ap = h >= 12 ? 'p' : 'a';
    h %= 12;
    if (!h) h = 12;
    o[0] = h >= 10 ? '1' : ' ';
    o[1] = '0' + h % 10; o[2] = ':';
    o[3] = '0' + m / 10; o[4] = '0' + m % 10; o[5] = ap; o[6] = 0;
}
