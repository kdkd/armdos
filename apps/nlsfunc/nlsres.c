/*
 * nlsres.c - NLSFUNC's resident part: INT 2Fh AH=14h, the kernel's gate to
 * COUNTRY.SYS after CONFIG.SYS time.
 *
 * MS-DOS 4.00's NLSFUNC (CMD/NLSFUNC, Microsoft, MIT licence) re-created for
 * ARM-DOS's kernel interface (kernel/README.md, "National language support"). The kernel calls
 * INT 2Fh with AH=14h and DI (r5) = its struct nls_state:
 *
 *   AL=00h  installed?                          -> AL = FFh
 *   AL=01h  set code page BX for country DX     (INT 21h AX=6602h, CHCP)
 *   AL=02h  extended country info, type BP (r6), code page BX, country DX,
 *           buffer SI (r4), length CX            (INT 21h AH=65h, another one)
 *   AL=03h  set country DX (its code page)      (INT 21h AH=38h, DX=FFFFh)
 *   AL=04h  country info of country DX into SI  (INT 21h AH=38h, another one)
 *
 * and gets AL = 0 or an error: 2 (COUNTRY.SYS not found), 13 (that country /
 * code page is not in it: "Invalid code page"), 65 (a device - CON with
 * DISPLAY.SYS - could not switch: "not prepared for all devices"). For AL=01h
 * NLSFUNC selects the code page on CON (IOCTL 440Ch 4Ah, which DISPLAY.SYS
 * passes on to KEYB) and only then changes the tables.
 *
 * It reads COUNTRY.SYS with ordinary INT 21h calls from inside the kernel's
 * INT 21h, as DOS 4's NLSFUNC did (the ARM-DOS kernel nests calls made from
 * handlers it invoked). Nothing here may call the C library.
 */
#include "nlsfunc.h"

struct nlsres nls_res RES = { .sig = { 'N', 'L', 'S', 'F' } };

static uint8_t t_info[38] RES, t_ucase[130] RES, t_fchar[24] RES, t_collate[258] RES;
static uint8_t t_dbcs[4] RES = { 0, 0, 0, 0 };
static uint16_t t_country RES, t_cp RES;

#define R16(p) ((p)[0] | ((p)[1] << 8))
#define R32(p) ((uint32_t)(p)[0] | ((uint32_t)(p)[1] << 8) | ((uint32_t)(p)[2] << 16) | ((uint32_t)(p)[3] << 24))

RESFN static void zero(void *d, unsigned n) { uint8_t *p = d; while (n--) *p++ = 0; }
RESFN static void copy(void *d, const void *s, unsigned n) { uint8_t *a = d; const uint8_t *b = s; while (n--) *a++ = *b++; }

RESFN static int dos_open(const char *path)
{
    struct armregs r;
    zero(&r, sizeof r);
    r.r0 = 0x3D00; r.r3 = (uint32_t)path;
    if (nls_int21(&r)) return -1;
    return r.r0 & 0xFFFF;
}
RESFN static void dos_close(int h)
{
    struct armregs r;
    zero(&r, sizeof r);
    r.r0 = 0x3E00; r.r1 = h;
    nls_int21(&r);
}
RESFN static int seekread(int h, uint32_t off, void *buf, unsigned n)
{
    struct armregs r;
    zero(&r, sizeof r);
    r.r0 = 0x4200; r.r1 = h; r.r2 = off >> 16; r.r3 = off & 0xFFFF;
    if (nls_int21(&r)) return -1;
    zero(&r, sizeof r);
    r.r0 = 0x3F00; r.r1 = h; r.r2 = n; r.r3 = (uint32_t)buf;
    if (nls_int21(&r) || (r.r0 & 0xFFFF) != n) return -1;
    return 0;
}

RESFN static int table(int h, uint32_t off, uint8_t *dest, unsigned max)
{
    uint8_t th[10];
    if (seekread(h, off, th, 10) || th[0] != 0xFF) return -1;
    unsigned len = R16(th + 8);
    if (len > max) return -1;
    return seekread(h, off + 10, dest, len) ? -1 : (int)len;
}

/* COUNTRY.SYS: NLSFUNC's own path, else the kernel's (COUNTRY=, default
   \COUNTRY.SYS - and, ARM-DOS, \DOS\COUNTRY.SYS on the boot drive) */
RESFN static int open_country(const struct nls_state *k)
{
    if (nls_res.path[0]) return dos_open(nls_res.path);
    int h = dos_open(k->path);
    if (h < 0 && k->path[0] == '\\' && k->path[1] == 'C') {
        static char alt[] RES = "?:\\DOS\\COUNTRY.SYS";
        alt[0] = nls_res.bootdrive;
        h = dos_open(alt);
    }
    return h;
}

/* read the tables of (country, cp) into t_*; cp 0 = the current code page if
   the country has it, else its first. 0, 2 (no file) or 13 (not there). */
RESFN static int load(const struct nls_state *k, unsigned country, unsigned cp)
{
    int h = open_country(k);
    if (h < 0) return 2;
    uint8_t hd[0x17], e[14], cnt[2];
    int found = 0, err = 13;
    if (seekread(h, 0, hd, sizeof hd) || hd[0] != 0xFF || hd[1] != 'C' || hd[2] != 'O') goto out;
    uint32_t list = R32(hd + 0x13);
    if (seekread(h, list, cnt, 2)) goto out;
    uint32_t first = 0;
    unsigned want = cp ? cp : k->cp;
    for (unsigned i = 0; i < (unsigned)R16(cnt); i++) {
        if (seekread(h, list + 2 + 14 * i, e, 14)) goto out;
        if ((unsigned)R16(e + 2) != country) continue;
        if (!first) first = list + 2 + 14 * i;
        if ((unsigned)R16(e + 4) == want) { found = 1; break; }
    }
    if (!found && !cp && first) { if (seekread(h, first, e, 14)) goto out; found = 1; }
    if (!found) goto out;
    uint32_t d = R32(e + 10);
    uint8_t n2[2], it[8];
    if (seekread(h, d, n2, 2)) goto out;
    copy(t_ucase, k->ucase, 130); copy(t_fchar, k->fchar, 24); copy(t_collate, k->collate, 258);
    zero(t_info, sizeof t_info);
    for (unsigned i = 0; i < (unsigned)R16(n2); i++) {
        if (seekread(h, d + 2 + 8 * i, it, 8)) goto out;
        uint32_t t = R32(it + 4);
        int n;
        switch (it[2]) {
        case 1: table(h, t, t_info, sizeof t_info); break;
        case 2: if (table(h, t, t_ucase + 2, 128) == (int)128) { t_ucase[0] = 128; t_ucase[1] = 0; } break;
        case 5: if ((n = table(h, t, t_fchar + 2, sizeof t_fchar - 2)) > 0) { t_fchar[0] = n; t_fchar[1] = 0; } break;
        case 6: if (table(h, t, t_collate + 2, 256) == 256) { t_collate[0] = 0; t_collate[1] = 1; } break;
        }
    }
    t_country = R16(e + 2);
    t_cp = R16(e + 4);
    err = 0;
out:
    dos_close(h);
    return err;
}

RESFN static void commit(struct nls_state *k)
{
    copy(k->info, t_info + 4, 34);
    zero(k->info + 0x12, 4);
    copy(k->ucase, t_ucase, 130);
    copy(k->fchar, t_fchar, 24);
    copy(k->collate, t_collate, 258);
    k->country = t_country;
    k->cp = t_cp;
}

/* select the code page on CON; 0, or 65 if the console could not switch
   (a console without code page support is left alone) */
RESFN static int select_con(unsigned cp)
{
    struct armregs r;
    zero(&r, sizeof r);
    static char con[] RES = "CON";
    r.r0 = 0x3D02; r.r3 = (uint32_t)con;
    if (nls_int21(&r)) return 0;
    int h = r.r0 & 0xFFFF;
    uint16_t pk[2];
    pk[0] = 2; pk[1] = cp;
    zero(&r, sizeof r);
    r.r0 = 0x440C; r.r1 = h; r.r2 = 0x034A; r.r3 = (uint32_t)pk;
    int cf = nls_int21(&r);
    int e = r.r0 & 0xFFFF;
    dos_close(h);
    if (!cf || e == 1 || e == 0x16) return 0;       /* done / no code pages on CON */
    return 65;
}

RESFN void nls_int2f(struct armregs *f)
{
    unsigned ax = f->r0 & 0xFFFF;
    if ((ax >> 8) != 0x14) { nls_res.old2f ? nls_res.old2f(f) : (void)(f->cpsr |= ARM_CPSR_C); return; }
    struct nls_state *k = (struct nls_state *)f->r5;
    int al = ax & 0xFF, e = 0;
    if (al == 0) { f->r0 = (f->r0 & ~0xFFu) | 0xFF; f->cpsr &= ~ARM_CPSR_C; return; }
    if (!k || k->magic != NLS_MAGIC) { f->r0 = 1; f->cpsr |= ARM_CPSR_C; return; }
    unsigned country = f->r3 & 0xFFFF, cp = f->r1 & 0xFFFF;
    switch (al) {
    case 1:                                 /* set the code page */
        if (!(e = load(k, country, cp)) && !(e = select_con(cp))) commit(k);
        break;
    case 2: {                               /* extended info of another country / code page */
        if ((e = load(k, country, cp))) break;
        uint8_t *d = (uint8_t *)f->r4;
        unsigned len = f->r2 & 0xFFFF, type = f->r6 & 0xFF;
        if (type == 1) {
            unsigned n = len - 3 < 38 ? len - 3 : 38;
            uint8_t b[41];
            b[0] = 1; b[1] = n; b[2] = 0;
            copy(b + 3, t_info, 38);
            zero(b + 3 + 4 + 0x12, 4);
            copy(d, b, n + 3);
            f->r2 = n + 3;
        } else {
            const uint8_t *t = type == 2 || type == 4 ? t_ucase : type == 5 ? t_fchar : type == 6 ? t_collate : t_dbcs;
            uint32_t p = (uint32_t)t;
            d[0] = type; d[1] = p; d[2] = p >> 8; d[3] = p >> 16; d[4] = p >> 24;
            f->r2 = 5;
        }
        break;
    }
    case 3:                                 /* set the country */
        if (!(e = load(k, country, 0))) commit(k);
        break;
    case 4:                                 /* country info of another country */
        if (!(e = load(k, country, 0))) { copy((uint8_t *)f->r4, t_info + 4, 34); f->r1 = 0; }
        break;
    default:
        e = 1;
    }
    f->r0 = (f->r0 & ~0xFFu) | e;
    if (e) f->cpsr |= ARM_CPSR_C; else f->cpsr &= ~ARM_CPSR_C;
}
