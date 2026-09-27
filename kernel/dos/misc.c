/*
 * misc.c - the rest of INT 21h (date/time through CLOCK$, disk information,
 * country information, DPBs, extended country / code pages, media ID,
 * server calls) and INT 25h/26h/28h/2Fh plus the default 22h/23h/24h.
 */
#include "dos.h"

/* --------------------------------------------------------- CLOCK$ */

int clock_read(uint8_t *six)
{
    struct req_rw q;
    memset(&q, 0, sizeof q);
    q.h.len = sizeof q;
    q.h.cmd = CMD_READ;
    q.addr = (uint32_t)six;
    q.count = 6;
    return devcall(LOL.clock, &q) & RS_ERROR ? -1 : 0;
}

int clock_write(const uint8_t *six)
{
    struct req_rw q;
    memset(&q, 0, sizeof q);
    q.h.len = sizeof q;
    q.h.cmd = CMD_WRITE;
    q.addr = (uint32_t)six;
    q.count = 6;
    return devcall(LOL.clock, &q) & RS_ERROR ? -1 : 0;
}

static const uint8_t mdays[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
static int leap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

unsigned ymd_to_days(int y, int m, int d)
{
    unsigned n = 0;
    for (int yy = 1980; yy < y; yy++) n += leap(yy) ? 366 : 365;
    for (int mm = 1; mm < m; mm++) n += mdays[mm - 1] + (mm == 2 && leap(y));
    return n + d - 1;
}

int days_to_ymd(unsigned n, int *y, int *m, int *d)
{
    int yy = 1980;
    for (;;) {
        unsigned len = leap(yy) ? 366 : 365;
        if (n < len) break;
        n -= len; yy++;
    }
    int mm = 1;
    for (;;) {
        unsigned len = mdays[mm - 1] + (mm == 2 && leap(yy));
        if (n < len) break;
        n -= len; mm++;
    }
    *y = yy; *m = mm; *d = n + 1;
    return 0;
}

static void get_date(struct armregs *f)
{
    uint8_t t[6];
    clock_read(t);
    unsigned days = t[0] | (t[1] << 8);
    int y, m, d;
    days_to_ymd(days, &y, &m, &d);
    f->r2 = y;
    f->r3 = (m << 8) | d;
    set_al(f, (days + 2) % 7);          /* 1-1-1980 was a Tuesday */
}

static void set_date(struct armregs *f)
{
    int y = CX(f), m = DH(f), d = DL(f);
    if (y < 1980 || y > 2099 || m < 1 || m > 12 || d < 1 || d > mdays[m - 1] + (m == 2 && leap(y))) {
        set_al(f, 0xFF);
        return;
    }
    uint8_t t[6];
    clock_read(t);
    unsigned days = ymd_to_days(y, m, d);
    t[0] = days; t[1] = days >> 8;
    clock_write(t);
    set_al(f, 0);
}

static void get_time(struct armregs *f)
{
    uint8_t t[6];
    clock_read(t);
    f->r2 = (t[3] << 8) | t[2];
    f->r3 = (t[5] << 8) | t[4];
}

static void set_time(struct armregs *f)
{
    int h = CH(f), mi = CL(f), s = DH(f), hs = DL(f);
    if (h > 23 || mi > 59 || s > 59 || hs > 99) { set_al(f, 0xFF); return; }
    uint8_t t[6];
    clock_read(t);
    t[2] = mi; t[3] = h; t[4] = hs; t[5] = s;
    clock_write(t);
    set_al(f, 0);
}

/* ------------------------------------------------------- country */

/* The national language data (kabi.h struct nls_state):
   country 001, code page 437 until COUNTRY= or NLSFUNC changes it. The tables
   are DOS 4's built-in ones (DOSMES.ASM). */
struct nls_state nls = {
    NLS_MAGIC, "\\COUNTRY.SYS", 1, 437, 437, 0,
    /* date MDY, "$", ",", ".", "-", ":", currency 0, 2 digits, 12-hour, (case map), "," */
    { 0, 0, '$', 0, 0, 0, 0, ',', 0, '.', 0, '-', 0, ':', 0, 0, 2, 0, 0, 0, 0, 0, ',', 0 },
    { 128, 0,
    128,154, 69, 65,142, 65,143,128, 69, 69, 69, 73, 73, 73,142,143,
    144,146,146, 79,153, 79, 85, 85, 89,153,154,155,156,157,158,159,
     65, 73, 79, 85,165,165,166,167,168,169,170,171,172,173,174,175,
    176,177,178,179,180,181,182,183,184,185,186,187,188,189,190,191,
    192,193,194,195,196,197,198,199,200,201,202,203,204,205,206,207,
    208,209,210,211,212,213,214,215,216,217,218,219,220,221,222,223,
    224,225,226,227,228,229,230,231,232,233,234,235,236,237,238,239,
    240,241,242,243,244,245,246,247,248,249,250,251,252,253,254,255 },
    { 22, 0, 1, 0, 255, 0, 0, 0x20, 2, 14, '.', '"', '/', '\\', '[', ']', ':', '|', '<', '>', '+', '=', ';', ',' },
    /* DOS 4's built-in US / code page 437 collating sequence (DOSMES.ASM COLLATE_TAB) */
    { 0, 1,   /* 256 entries */
    0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
    16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,
    32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,
    48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,
    64,65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,
    80,81,82,83,84,85,86,87,88,89,90,91,92,93,94,95,
    96,65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,
    80,81,82,83,84,85,86,87,88,89,90,123,124,125,126,127,
    67,85,69,65,65,65,65,67,69,69,69,73,73,73,65,65,
    69,65,65,79,79,79,85,85,89,79,85,36,36,36,36,36,
    65,73,79,85,78,78,166,167,63,169,170,171,172,33,34,34,
    176,177,178,179,180,181,182,183,184,185,186,187,188,189,190,191,
    192,193,194,195,196,197,198,199,200,201,202,203,204,205,206,207,
    208,209,210,211,212,213,214,215,216,217,218,219,220,221,222,223,
    224,83,226,227,228,229,230,231,232,233,234,235,236,237,238,239,
    240,241,242,243,244,245,246,247,248,249,250,251,252,253,254,255 },
};
static const uint8_t dbcs_tab[4] = { 0, 0, 0, 0 };

/* the case-map routine the country buffer points at (a FAR routine in DOS:
   here a function called with AL, returning AL; ARM-DOS 12h, 22h) */
static uint32_t map_case(uint32_t c) { return dos_upcase(c & 0xFF); }

static void country_info(uint8_t *b)
{
    memcpy(b, nls.info, 34);
    uint32_t mc = (uint32_t)map_case;
    b[0x12] = mc; b[0x13] = mc >> 8; b[0x14] = mc >> 16; b[0x15] = mc >> 24;
}

/* INT 2Fh AH=14h: NLSFUNC. 0 = not installed; else what it returns in AL */
static int nlsfunc(int fn, struct armregs *r)
{
    struct armregs q = { 0 };
    q.r0 = 0x1400;
    kint(0x2F, &q);
    if ((q.r0 & 0xFF) != 0xFF) return -1;
    r->r0 = 0x1400 | fn;
    r->r5 = (uint32_t)&nls;
    kint(0x2F, r);
    return r->r0 & 0xFF;
}

static void fn_country(struct armregs *f)
{
    int code = AL(f) == 0xFF ? BX(f) : AL(f);
    if (DX(f) == 0xFFFF) {              /* set the current country (NLSFUNC) */
        if (code != nls.country) {
            struct armregs r = { 0 };
            r.r3 = code; r.r1 = 0;
            int e = nlsfunc(3, &r);
            if (e < 0) { sys_err(f, E_INVFN); return; }
            if (e) { sys_err(f, e); return; }
        }
        f->r0 = code; f->r1 = code;
        return;
    }
    if (code && code != nls.country) {  /* another country: NLSFUNC has COUNTRY.SYS */
        struct armregs r = { 0 };
        r.r3 = code; r.r1 = 0; r.r4 = f->r3;
        int e = nlsfunc(4, &r);
        if (e < 0) { sys_err(f, E_NOFILE); return; }
        if (e) { sys_err(f, e); return; }
        uint8_t *b = (uint8_t *)f->r3;
        uint32_t mc = (uint32_t)map_case;
        b[0x12] = mc; b[0x13] = mc >> 8; b[0x14] = mc >> 16; b[0x15] = mc >> 24;
        f->r0 = code; f->r1 = code;
        return;
    }
    country_info((uint8_t *)f->r3);
    f->r1 = nls.country;
    f->r0 = nls.country;
}

static void fn_extcountry(struct armregs *f)
{
    int al = AL(f);
    uint8_t *d = (uint8_t *)f->r5;
    unsigned len = CX(f);
    switch (al) {
    case 0x01: case 0x02: case 0x04: case 0x05: case 0x06: case 0x07: {
        if (len < 5) { sys_err(f, E_INVFN); return; }
        unsigned country = DX(f) == 0xFFFF ? nls.country : DX(f);
        unsigned cp = BX(f) == 0xFFFF ? nls.cp : BX(f);
        if (country != nls.country || cp != nls.cp) {  /* another country or code page: NLSFUNC */
            struct armregs r = { 0 };
            r.r1 = cp; r.r2 = len; r.r3 = country; r.r4 = (uint32_t)d; r.r6 = al;
            int e = nlsfunc(2, &r);
            if (e < 0) { sys_err(f, E_INVFN); return; }
            if (e) { sys_err(f, e); return; }
            f->r2 = r.r2 & 0xFFFF;
            f->r0 = nls.syscp;
            return;
        }
        if (al == 1) {
            uint8_t b[41];
            b[0] = 1; b[1] = 38; b[2] = 0;
            b[3] = nls.country; b[4] = nls.country >> 8;
            b[5] = nls.cp; b[6] = nls.cp >> 8;
            country_info(b + 7);
            unsigned n = len - 3 < 38 ? len - 3 : 38;   /* the data after the length word */
            b[1] = n; b[2] = 0;
            memcpy(d, b, n + 3);
            f->r2 = n + 3;
        } else {
            const void *t = al == 2 || al == 4 ? (const void *)nls.ucase : al == 5 ? (const void *)nls.fchar
                           : al == 6 ? (const void *)nls.collate : (const void *)dbcs_tab;
            uint32_t p = (uint32_t)t;
            d[0] = al; d[1] = p; d[2] = p >> 8; d[3] = p >> 16; d[4] = p >> 24;
            f->r2 = 5;
        }
        f->r0 = nls.syscp;
        break;
    }
    case 0x20: case 0xA0:
        set_dl(f, dos_upcase(DL(f)));
        break;
    case 0x21: case 0xA1: {
        uint8_t *s = (uint8_t *)f->r3;
        for (unsigned i = 0; i < CX(f); i++) s[i] = dos_upcase(s[i]);
        break;
    }
    case 0x22: case 0xA2: {
        uint8_t *s = (uint8_t *)f->r3;
        for (; *s; s++) *s = dos_upcase(*s);
        break;
    }
    case 0x23: {
        int c = dos_upcase(DL(f));
        f->r0 = c == 'Y' ? 1 : c == 'N' ? 0 : 2;
        break;
    }
    default:
        sys_err(f, E_INVFN);
    }
}

/* INT 21h AH=66h: get / set the global code page ($GetSetCdPg). Setting it is
   NLSFUNC's job: the tables of COUNTRY.SYS for the code page, and the code
   page selected on the devices that switch (CON with DISPLAY.SYS). */
static void fn_codepage(struct armregs *f)
{
    if (AL(f) == 1) { f->r1 = nls.cp; f->r3 = nls.syscp; return; }
    if (AL(f) != 2) { sys_err(f, E_INVFN); return; }
    struct armregs r = { 0 };
    r.r1 = BX(f); r.r3 = nls.country;
    int e = nlsfunc(1, &r);
    if (e < 0) { sys_err(f, E_INVFN); return; }
    if (e == 65) {                      /* a device could not switch */
        set_exterr(65, 4, 6, 4);        /* hardware failure, ignore, serial device */
        f->r0 = 65;
        f->cpsr |= CPSR_C;
        return;
    }
    if (e) sys_err(f, e);
}

/* ---------------------------------------------------------- disks */

static void alloc_info(struct armregs *f, int drive)
{
    int err;
    if (!drive_usable(drive)) { set_al(f, 0xFF); f->cpsr |= CPSR_C; return; }
    if (drive_remote(drive)) {          /* from 110Ch */
        static uint8_t remote_media = 0xF8;
        unsigned spc, total, bps, avail;
        if (redir_space(drive, &spc, &total, &bps, &avail) < 0) { set_al(f, 0xFF); f->cpsr |= CPSR_C; return; }
        set_al(f, spc);
        f->r2 = bps;
        f->r3 = total;
        f->r1 = (uint32_t)&remote_media;
        f->r7 = 0;
        return;
    }
    struct dpb *d = drive_dpb(drive, &err);
    if (!d) { set_al(f, 0xFF); f->cpsr |= CPSR_C; return; }
    set_al(f, d->cluster_mask + 1);
    f->r2 = d->sector_size;
    f->r3 = d->max_cluster - 1;
    f->r1 = (uint32_t)&d->media;
    f->r7 = 0;
}

static void free_space(struct armregs *f)
{
    int drive = DL(f) ? DL(f) - 1 : cur_drive, err;
    if (!drive_usable(drive)) { f->r0 = 0xFFFF; return; }
    if (drive_remote(drive)) {          /* 110Ch */
        unsigned spc, total, bps, avail;
        if (redir_space(drive, &spc, &total, &bps, &avail) < 0) { f->r0 = 0xFFFF; return; }
        f->r0 = spc; f->r1 = avail; f->r2 = bps; f->r3 = total;
        return;
    }
    struct dpb *d = drive_dpb(drive, &err);
    if (!d) { f->r0 = 0xFFFF; return; }
    int n = fat_count_free(d);
    if (n < 0) { f->r0 = 0xFFFF; return; }
    f->r0 = d->cluster_mask + 1;
    f->r1 = n;
    f->r2 = d->sector_size;
    f->r3 = d->max_cluster - 1;
}

static void get_dpb(struct armregs *f, int drive)
{
    int err;
    if (!drive_usable(drive) || drive_remote(drive)) { set_al(f, 0xFF); return; }
    struct dpb *d = drive_dpb(drive, &err);
    if (!d) { set_al(f, 0xFF); return; }
    f->r1 = (uint32_t)d;
    set_al(f, 0);
}

static void media_id(struct armregs *f)
{
    no_i24 = 1;             /* 69h fails quietly on a drive that is not ready (4.00) */
    int drive = BL(f) ? BL(f) - 1 : cur_drive, err;
    if (drive_remote(drive)) { sys_err(f, E_INVFN); return; }   /* no media ID over the network */
    struct dpb *d = drive_dpb(drive, &err);
    if (!d) { sys_err(f, err); return; }
    if (AL(f) > 1) { sys_err(f, E_INVFN); return; }
    struct req_gioctl q;
    memset(&q, 0, sizeof q);
    q.h.len = sizeof q;
    q.h.unit = d->unit;
    q.h.cmd = CMD_GENIOCTL;
    q.category = 8;
    q.minor = AL(f) ? 0x46 : 0x66;
    q.data = f->r3;
    int st = devcall(d->driver, &q);
    if (st & RS_ERROR) sys_err(f, (st & 0xFF) == DE_MEDIA ? E_BADDATA : E_ACCESS);
}

/* -------------------------------------------------------- INT 21h */

void misc_functions(struct armregs *f)
{
    switch (AH(f)) {
    case 0x1B: alloc_info(f, cur_drive); break;
    case 0x1C: alloc_info(f, DL(f) ? DL(f) - 1 : cur_drive); break;
    case 0x1F: get_dpb(f, cur_drive); break;
    case 0x32: get_dpb(f, DL(f) ? DL(f) - 1 : cur_drive); break;
    case 0x2A: get_date(f); break;
    case 0x2B: set_date(f); break;
    case 0x2C: get_time(f); break;
    case 0x2D: set_time(f); break;
    case 0x36: free_space(f); break;
    case 0x38: fn_country(f); break;
    case 0x53: {
        struct dpb *d = (struct dpb *)f->r6;
        build_dpb(d, (const struct bpb *)f->r4);
        break;
    }
    case 0x5D:
        if (AL(f) == 0x06) {
            f->r4 = (uint32_t)&DV;
            f->r2 = sizeof DV;
            f->r3 = sizeof DV;
        } else if (AL(f) == 0x0A) {
            const uint16_t *e = (const uint16_t *)f->r3;
            exterr.code = e[0];
            exterr.class_ = e[1] >> 8;
            exterr.action = e[1];
            exterr.locus = e[2] >> 8;
        } else sys_err(f, E_INVFN);
        break;
    case 0x5E: case 0x5F:
        sys_err(f, E_INVFN);
        break;
    case 0x63:
        if (AL(f) == 0) { f->r4 = (uint32_t)dbcs_tab; set_al(f, 0); }
        else set_al(f, 0xFF);
        break;
    case 0x65: fn_extcountry(f); break;
    case 0x66: fn_codepage(f); break;
    case 0x69: media_id(f); break;
    default:
        set_al(f, 0);
        break;
    }
}

/* ------------------------------------------------ INT 25h / 26h */

static void absdisk(struct armregs *f, int write)
{
    int drive = AL(f), err;
    uint32_t start, count, buf;
    if (CX(f) == 0xFFFF) {
        const uint8_t *p = (const uint8_t *)f->r1;
        start = p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
        count = p[4] | (p[5] << 8);
        buf = p[6] | (p[7] << 8) | (p[8] << 16) | ((uint32_t)p[9] << 24);
    } else {
        start = DX(f); count = CX(f); buf = f->r1;
    }
    struct cds *c = get_cds(drive);
    if (!c || !(c->flags & CDS_VALID) || !c->dpb) { f->r0 = 0x8001; f->cpsr |= CPSR_C; return; }
    struct dpb *d = drive_dpb(drive, &err);
    if (!d) { f->r0 = 0x8002; f->cpsr |= CPSR_C; return; }
    if (CX(f) != 0xFFFF && (uint32_t)(d->max_cluster - 1) * (d->cluster_mask + 1) + d->first_data > 0xFFFF) {
        f->r0 = 0x0207; f->cpsr |= CPSR_C; return;      /* a big partition needs the packet form */
    }
    /* a write makes buffered copies stale; a read goes to the disk as it is,
       dirty buffers and all (as DOS 4's ABSDRD) */
    if (write) buf_sync_range(d, start, count, 1);
    struct req_rw q;
    memset(&q, 0, sizeof q);
    q.h.len = sizeof q;
    q.h.unit = d->unit;
    q.h.cmd = write ? CMD_WRITE : CMD_READ;
    q.media = d->media;
    q.addr = buf;
    q.count = count;
    q.start = 0xFFFF;
    q.start32 = start;
    int st = devcall(d->driver, &q);
    if (st & RS_ERROR) {
        static const uint8_t ah_of[16] = { 0x03, 0x02, 0x80, 0x02, 0x10, 0x02, 0x40, 0x02, 0x04, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02 };
        f->r0 = (ah_of[st & 0x0F] << 8) | (st & 0xFF);
        f->cpsr |= CPSR_C;
        return;
    }
    f->cpsr &= ~CPSR_C;
}

void int25_handler(struct armregs *f) { absdisk(f, 0); }
void int26_handler(struct armregs *f) { absdisk(f, 1); }

/* ---------------------------------------------- default vectors */

void int28_handler(struct armregs *f) { (void)f; }
void int22_default(struct armregs *f) { (void)f; }
void int23_default(struct armregs *f) { f->cpsr |= CPSR_C; }       /* abort */
void int24_default(struct armregs *f) { set_al(f, 3); }             /* fail */

static uint32_t msg_tables[4];

void int2f_handler(struct armregs *f)
{
    switch (AH(f)) {
    case 0x12:
        switch (AL(f)) {
        case 0x00: set_al(f, 0xFF); break;
        case 0x16: {                    /* SFT entry BX -> ES:DI */
            struct sft *s = sft_get(BX(f));
            if (!s) { f->cpsr |= CPSR_C; break; }
            f->r5 = (uint32_t)s; f->r8 = 0;
            f->cpsr &= ~CPSR_C;
            break;
        }
        case 0x20: {                    /* JFT entry of handle BX -> ES:DI */
            unsigned size;
            uint8_t *j = jft_ptr(PSP(cur_psp), &size);
            if (BX(f) >= size) { f->r0 = E_BADHANDLE; f->cpsr |= CPSR_C; break; }
            f->r5 = (uint32_t)&j[BX(f)]; f->r8 = 0;
            f->cpsr &= ~CPSR_C;
            break;
        }
        case 0x2E: {                    /* message retriever tables */
            int i = (DL(f) >> 1) & 3;
            if (DL(f) & 1) msg_tables[i] = f->r5;
            else { f->r5 = msg_tables[i]; f->r8 = 0; }
            break;
        }
        case 0x06: {                    /* invoke critical error (redirectors): BH = INT 24h AH,
                                           BL = drive, DI = error code, SI = device header */
            int r = crit_error(BH(f), BL(f), f->r5 & 0xFF, (struct devhdr *)f->r4);
            set_al(f, r);
            f->cpsr &= ~CPSR_C;
            break;
        }
        case 0x2F:                      /* fake version: not supported here */
            break;
        }
        break;
    case 0x11:
        /* no redirector took the call: 1100h says "not installed", the rest fail */
        if (AL(f)) { f->r0 = E_NOTSUP; f->cpsr |= CPSR_C; }
        break;
    default:
        /* nobody installed: AL stays 00h ("not installed, OK to install") */
        break;
    }
}
