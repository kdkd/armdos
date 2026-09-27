/*
 * dosutil.c - shared runtime of the ARM-DOS disk utilities (see dosutil.h).
 */
#include "dosutil.h"

const char M_INVSW[]    = "Invalid switch";
const char M_TOOMANY[]  = "Too many parameters";
const char M_REQMISS[]  = "Required parameter missing";
const char M_BADFMT[]   = "Parameter format not correct";
const char M_INVPARM[]  = "Invalid parameter";
const char M_INVDRIVE[] = "Invalid drive specification";
const char M_PRESSKEY[] = "Press any key to continue . . .\r\n";

int dos(R *r) { return _armdos_int21(r); }
int intr(int n, R *r) { return _armdos_intr(n, r); }

/* ------------------------------------------------------------ output */

void wrn(int h, const char *s, unsigned n)
{
    if (!n) return;
    R r = { 0 };
    r.r0 = 0x4000; r.r1 = h; r.r2 = n; r.r3 = (uint32_t)s;
    dos(&r);
}
void wr(int h, const char *s) { wrn(h, s, strlen(s)); }
void outs(const char *s) { wr(STDOUT, s); }
void errs(const char *s) { wr(STDERR, s); }
void crlf(void) { outs("\r\n"); }

char *fmtnum(char *p, uint32_t v, int width, char pad)
{
    char t[12];
    int n = 0;
    do { t[n++] = '0' + v % 10; v /= 10; } while (v);
    while (width-- > n) *p++ = pad;
    while (n) *p++ = t[--n];
    *p = 0;
    return p;
}

char *fmthex4(char *p, unsigned v)
{
    for (int i = 12; i >= 0; i -= 4) *p++ = "0123456789ABCDEF"[(v >> i) & 15];
    *p = 0;
    return p;
}

void outnum(uint32_t v, const char *text)
{
    char b[16];
    fmtnum(b, v, 10, ' ');
    outs(b);
    outs(text);
    crlf();
}

void outfmt(int h, const char *text, const char *a1, const char *a2, const char *a3)
{
    const char *p = text, *s = text;
    for (; *p; p++) {
        if (*p == '%' && p[1] >= '1' && p[1] <= '3') {
            wrn(h, s, p - s);
            const char *a = p[1] == '1' ? a1 : p[1] == '2' ? a2 : a3;
            if (a) wr(h, a);
            s = p + 2;
            p++;
        }
    }
    wrn(h, s, p - s);
}

void serial_line(uint32_t serial)
{
    char b[48], *p;
    p = b;
    memcpy(p, "Volume Serial Number is ", 24); p += 24;
    p = fmthex4(p, serial >> 16);
    *p++ = '-';
    p = fmthex4(p, serial & 0xFFFF);
    *p++ = '\r'; *p++ = '\n'; *p = 0;
    outs(b);
}

/* ------------------------------------------------------------- input */

int getline_flush(uint8_t *buf, int max)
{
    R r = { 0 };
    buf[0] = max;
    buf[1] = 0;
    r.r0 = 0x0C0A; r.r3 = (uint32_t)buf;
    dos(&r);
    memset(&r, 0, sizeof r);
    r.r0 = 0x0C00;
    dos(&r);
    return buf[1];
}

int getkey_flush(void)
{
    R r = { 0 };
    r.r0 = 0x0C08;
    dos(&r);
    return r.r0 & 0xFF;
}

int getche_flush(void)
{
    R r = { 0 };
    r.r0 = 0x0C01;
    dos(&r);
    return r.r0 & 0xFF;
}

int getkey(void)
{
    R r = { 0 };
    r.r0 = 0x0800;
    dos(&r);
    return r.r0 & 0xFF;
}

int yesno(int c)
{
    R r = { 0 };
    r.r0 = 0x6523; r.r3 = c & 0xFF;
    if (dos(&r)) return -1;
    return (r.r0 & 0xFFFF) == 1 ? 1 : (r.r0 & 0xFFFF) == 0 ? 0 : -1;
}

/* -------------------------------------------------------------- misc */

void dos_exit(int code)
{
    R r = { 0 };
    r.r0 = 0x4C00 | (code & 0xFF);
    dos(&r);
    for (;;) ;
}

void check_version(void)
{
    R r = { 0 };
    r.r0 = 0x3000;
    dos(&r);
    if ((r.r0 & 0xFFFF) != 0x0004) {
        outs("Incorrect DOS version\r\n");
        dos_exit(1);
    }
}

int cur_drive(void)
{
    R r = { 0 };
    r.r0 = 0x1900;
    dos(&r);
    return r.r0 & 0xFF;
}

int boot_drive(void)
{
    R r = { 0 };
    r.r0 = 0x3305;
    dos(&r);
    return (r.r3 & 0xFF) ? (r.r3 & 0xFF) - 1 : 2;
}

void get_date(unsigned *year, unsigned *mon, unsigned *day)
{
    R r = { 0 };
    r.r0 = 0x2A00;
    dos(&r);
    *year = r.r2 & 0xFFFF; *mon = (r.r3 >> 8) & 0xFF; *day = r.r3 & 0xFF;
}

void get_time(unsigned *h, unsigned *m, unsigned *s, unsigned *hs)
{
    R r = { 0 };
    r.r0 = 0x2C00;
    dos(&r);
    *h = (r.r2 >> 8) & 0xFF; *m = r.r2 & 0xFF; *s = (r.r3 >> 8) & 0xFF; *hs = r.r3 & 0xFF;
}

uint16_t dos_date(void)
{
    unsigned y, m, d;
    get_date(&y, &m, &d);
    return ((y - 1980) << 9) | (m << 5) | d;
}

uint16_t dos_time(void)
{
    unsigned h, m, s, hs;
    get_time(&h, &m, &s, &hs);
    return (h << 11) | (m << 5) | (s / 2);
}

void disk_reset(void)
{
    R r = { 0 };
    r.r0 = 0x0D00;
    dos(&r);
}

struct kdpb *get_dpb(int drive)
{
    R r = { 0 };
    r.r0 = 0x3200; r.r3 = drive + 1;
    dos(&r);
    if ((r.r0 & 0xFF) == 0xFF) return 0;
    return (struct kdpb *)r.r1;
}

void forget_free(int drive, int next_free)
{
    struct kdpb *d = get_dpb(drive);
    if (!d) return;
    d->free_count = 0xFFFF;
    if (next_free >= 0) d->next_free = next_free;
}

/* ------------------------------------------------------------ drives */

int ioctl_removable(int drive)
{
    R r = { 0 };
    r.r0 = 0x4408; r.r1 = drive + 1;
    if (dos(&r)) return -1;
    return (r.r0 & 0xFFFF) == 0 ? 1 : 0;
}

int ioctl_remote(int drive, unsigned *attr)
{
    R r = { 0 };
    r.r0 = 0x4409; r.r1 = drive + 1;
    if (dos(&r)) return -1;
    *attr = r.r3 & 0xFFFF;
    return 0;
}

int drive_valid(int drive)
{
    unsigned a;
    return ioctl_remote(drive, &a) == 0;
}

int truename_letter(int drive)
{
    char in[4] = { 'A' + drive, ':', '\\', 0 }, out[130];
    R r = { 0 };
    r.r0 = 0x6000; r.r4 = (uint32_t)in; r.r5 = (uint32_t)out;
    if (dos(&r)) return 'A' + drive;
    return out[0];
}

int get_logical(int drive)
{
    R r = { 0 };
    r.r0 = 0x440E; r.r1 = drive + 1;
    if (dos(&r)) return 0;
    return r.r0 & 0xFF;
}

void set_logical(int drive)
{
    R r = { 0 };
    r.r0 = 0x440F; r.r1 = drive + 1;
    dos(&r);
}

int gen_ioctl(int drive, int minor, void *packet)
{
    R r = { 0 };
    r.r0 = 0x440D; r.r1 = drive + 1; r.r2 = 0x0800 | minor; r.r3 = (uint32_t)packet;
    if (!dos(&r)) return 0;
    /* AX only says "access denied" (DOS maps 44h's errors); the real one, as 4.00's utilities do: */
    memset(&r, 0, sizeof r);
    r.r0 = 0x5900;
    dos(&r);
    return (r.r0 & 0xFFFF) ? (r.r0 & 0xFFFF) : 5;
}

int drive_geometry(int drive, struct geom *g)
{
    int rem = ioctl_removable(drive);
    if (rem < 0) return -1;
    R r = { 0 };
    r.r0 = 0x0800; r.r3 = rem ? 0x00 : 0x80;
    intr(0x13, &r);
    g->spt = r.r2 & 0x3F;
    g->heads = ((r.r3 >> 8) & 0xFF) + 1;
    g->cyls = (((r.r2 >> 8) & 0xFF) | ((r.r2 & 0xC0) << 2)) + 1;
    if (rem) {
        if (CF(&r) || !g->spt) { g->spt = 18; g->cyls = 80; g->heads = 2; }
        g->total = (uint32_t)g->spt * g->heads * g->cyls;
    } else {
        struct devparams dp;
        memset(&dp, 0, sizeof dp);
        dp.special = 1;
        if (gen_ioctl(drive, 0x60, &dp)) return -1;
        if (!g->spt || CF(&r)) { g->spt = dp.bpb.spt; g->heads = dp.bpb.heads; }
        g->total = dp.bpb.total16 ? dp.bpb.total16 : dp.bpb.total32;
        g->cyls = (g->total + g->spt * g->heads - 1) / (g->spt * g->heads);
    }
    return 0;
}

struct trkpkt {
    uint8_t  special;
    uint16_t head, cyl, first, count;
    uint32_t buf;
} __attribute__((packed));

int trk_rw(int drive, int write, const struct geom *g, uint32_t lba, unsigned count, void *buf)
{
    uint8_t *p = buf;
    while (count) {
        struct trkpkt t;
        unsigned per_cyl = g->spt * g->heads;
        unsigned first = lba % g->spt, n = g->spt - first;
        if (n > count) n = count;
        t.special = 0;
        t.cyl = lba / per_cyl;
        t.head = (lba / g->spt) % g->heads;
        t.first = first;
        t.count = n;
        t.buf = (uint32_t)p;
        int e = gen_ioctl(drive, write ? 0x41 : 0x61, &t);
        if (e) return e;
        lba += n; count -= n; p += n * 512;
    }
    return 0;
}

int trk_format(int drive, unsigned cyl, unsigned head, int verify_only)
{
    struct trkpkt t;
    memset(&t, 0, sizeof t);
    t.head = head;
    t.cyl = cyl;
    return gen_ioctl(drive, verify_only ? 0x62 : 0x42, &t);
}

int abs_rw(int drive, int write, uint32_t sector, unsigned count, void *buf)
{
    static struct { uint32_t start; uint16_t count; uint32_t buf; } __attribute__((packed)) pk;
    pk.start = sector; pk.count = count; pk.buf = (uint32_t)buf;
    R r = { 0 };
    r.r0 = drive; r.r1 = (uint32_t)&pk; r.r2 = 0xFFFF;
    if (intr(write ? 0x26 : 0x25, &r)) return (r.r0 & 0xFFFF) ? (r.r0 & 0xFFFF) : 0xFFFF;
    return 0;
}

/* ----------------------------------------------------------- parsing */

void upcase(char *s)
{
    for (; *s; s++) if (*s >= 'a' && *s <= 'z') *s -= 32;
}

int is_drive_spec(const char *s)
{
    return ((s[0] >= 'A' && s[0] <= 'Z') || (s[0] >= 'a' && s[0] <= 'z')) && s[1] == ':' && !s[2];
}

static int delim(int c) { return c == ' ' || c == '\t' || c == ',' || c == ';' || c == '='; }

int parse_tail(struct arg *a, int max)
{
    const uint8_t *t = _armdos_psp->cmdtail;
    int len = t[0] > 126 ? 126 : t[0];
    char line[128];
    memcpy(line, t + 1, len);
    line[len] = 0;
    for (int i = 0; i < len; i++) if (line[i] == '\r') { line[i] = 0; break; }
    int n = 0;
    char *p = line;
    while (*p && n < max) {
        int blank = 0;
        while (*p && delim(*p)) { if (*p == ' ' || *p == '\t') blank = 1; p++; }
        if (!*p) break;
        struct arg *x = &a[n++];
        memset(x, 0, sizeof *x);
        char *q = x->text;
        if (*p == '/') {
            x->sw = 1;
            *q++ = *p++;
            int quote = 0;
            while (*p && (quote || (!delim(*p) && *p != '/')) && q < x->text + 78) {
                if (*p == '"') quote = !quote;
                if (*p == ':' && !x->val) { *q++ = 0; x->val = q; p++; continue; }
                *q++ = *p++;
            }
        } else {
            while (*p && !delim(*p) && *p != '/' && q < x->text + 78) *q++ = *p++;
        }
        *q = 0;
        /* what the retriever shows: switches keep the blank before them */
        char *s = x->shown;
        if (x->sw && blank) *s++ = ' ';
        strcpy(s, x->text);
        if (x->val) { strcat(s, ":"); strcat(s, x->val); }
    }
    return n;
}

void parse_err(const char *msg, const char *shown)
{
    errs(msg);
    if (shown) { errs(" - "); errs(shown); }
    errs("\r\n");
}
