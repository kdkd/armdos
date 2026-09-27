/* zmodem.c - ZMODEM send and receive.
 *
 * Original code for ARM-DOS (no lrzsz source is used), written from Chuck
 * Forsberg's public protocol description "The ZMODEM Inter Application File
 * Transfer Protocol" (1988). Interoperates with lrzsz's sz/rz (tested on the
 * host, see tests/zmhost/).
 *
 * Supported: hex, binary (CRC-16) and binary-32 (CRC-32) headers; ZDLE
 * escaping (incl. ESCCTL on request); ZRQINIT/ZRINIT/ZSINIT/ZFILE/ZRPOS/
 * ZDATA/ZEOF/ZFIN/ZSKIP/ZACK/ZNAK/ZCRC/ZCHALLENGE/ZABORT/ZFERR/ZCAN;
 * streaming with ZCRCG and error recovery by ZRPOS; batch transfers; file
 * size and modification time; crash recovery (resume a partial file,
 * ZCRECOV). Not supported: compression, encryption, ZCOMMAND (refused).
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
#include "zmodem.h"

#ifdef ZM_HOST
#include <utime.h>
#ifndef O_BINARY
#define O_BINARY 0
#endif
extern uint32_t zm_host_ticks(void);
#define ZTICKS() zm_host_ticks()
#else
#include <dos.h>
#include "comm.h"
#define ZTICKS() TICKS()
#endif

#define ZPAD   '*'
#define ZDLE   0x18
#define ZDLEE  (ZDLE ^ 0x40)
#define ZBIN   'A'
#define ZHEX   'B'
#define ZBIN32 'C'

enum { ZRQINIT, ZRINIT, ZSINIT, ZACK, ZFILE, ZSKIP, ZNAK, ZABORT, ZFIN, ZRPOS, ZDATA, ZEOF,
       ZFERR, ZCRC, ZCHALLENGE, ZCOMPL, ZCAN, ZFREECNT, ZCOMMAND, ZSTDERR };

#define ZCRCE 'h'
#define ZCRCG 'i'
#define ZCRCQ 'j'
#define ZCRCW 'k'
#define ZRUB0 'l'
#define ZRUB1 'm'

#define GOTOR   0x100
#define GOTCAN  (GOTOR | 0x18)

/* ZRINIT flags (ZF0) */
#define CANFDX  0x01
#define CANOVIO 0x02
#define CANBRK  0x04
#define CANFC32 0x20
#define ESCCTL  0x40
#define ESC8    0x80
/* ZFILE ZF0 conversion options */
#define ZCBIN   1
#define ZCNL    2
#define ZCRECOV 3

/* header byte positions */
#define ZF0 3
#define ZF1 2
#define ZF2 1
#define ZF3 0
#define ZP0 0

#define HDR_TIMEOUT  10000
#define DATA_TIMEOUT 10000

/* ------------------------------------------------------------------ CRCs */
static uint16_t crc16tab[256];
static uint32_t crc32tab[256];
static int crc_ready;

static void crc_init(void)
{
    for (int i = 0; i < 256; i++) {
        uint16_t c = (uint16_t)(i << 8);
        for (int k = 0; k < 8; k++) c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x1021) : (uint16_t)(c << 1);
        crc16tab[i] = c;
        uint32_t d = (uint32_t)i;
        for (int k = 0; k < 8; k++) d = (d & 1) ? (d >> 1) ^ 0xEDB88320u : d >> 1;
        crc32tab[i] = d;
    }
    crc_ready = 1;
}
#define UPD16(crc, c) ((uint16_t)(crc16tab[((crc) >> 8) & 0xFF] ^ ((crc) << 8) ^ (c)))
static inline uint16_t crc16b(uint16_t crc, int c) { return (uint16_t)(crc16tab[(crc >> 8) ^ (c & 0xFF)] ^ (crc << 8)); }
static inline uint32_t crc32b(uint32_t crc, int c) { return crc32tab[(crc ^ (uint32_t)c) & 0xFF] ^ (crc >> 8); }
uint32_t zm_crc32(uint32_t crc, const uint8_t *p, int n)
{
    if (!crc_ready) crc_init();
    while (n-- > 0) crc = crc32b(crc, *p++);
    return crc;
}

/* ---------------------------------------------------------------- output */
static int onum;
static void oflush(struct zm *z) { if (onum) { z->tx(z->ctx, z->obuf, onum); onum = 0; } }
static void oraw(struct zm *z, int c) { if (onum >= (int)sizeof z->obuf) oflush(z); z->obuf[onum++] = (uint8_t)c; }

static void osend(struct zm *z, int c)          /* ZDLE-escaped */
{
    static int lastc;
    c &= 0xFF;
    int esc = 0;
    switch (c) {
    case ZDLE: case 0x10: case 0x90: case 0x11: case 0x91: case 0x13: case 0x93: esc = 1; break;
    case 0x0D: case 0x8D: esc = (lastc & 0x7F) == '@'; break;
    default: if (z->escctl && (c & 0x60) == 0) esc = 1;
    }
    if (esc) { oraw(z, ZDLE); c ^= 0x40; }
    oraw(z, c);
    lastc = c;
}

static const char hexd[] = "0123456789abcdef";
static void ohex(struct zm *z, int b) { oraw(z, hexd[(b >> 4) & 15]); oraw(z, hexd[b & 15]); }

static void sethdr(uint8_t *h, long pos)
{ h[0] = (uint8_t)pos; h[1] = (uint8_t)(pos >> 8); h[2] = (uint8_t)(pos >> 16); h[3] = (uint8_t)(pos >> 24); }
static long gethdrpos(const uint8_t *h)
{ return (long)((uint32_t)h[0] | ((uint32_t)h[1] << 8) | ((uint32_t)h[2] << 16) | ((uint32_t)h[3] << 24)); }

static void send_hexhdr(struct zm *z, int type, const uint8_t *h)
{
    uint16_t crc = 0;
    oraw(z, ZPAD); oraw(z, ZPAD); oraw(z, ZDLE); oraw(z, ZHEX);
    ohex(z, type); crc = crc16b(crc, type);
    for (int i = 0; i < 4; i++) { ohex(z, h[i]); crc = crc16b(crc, h[i]); }
    ohex(z, crc >> 8); ohex(z, crc & 0xFF);
    oraw(z, 0x0D); oraw(z, 0x8A);
    if (type != ZFIN && type != ZACK) oraw(z, 0x11);    /* XON */
    oflush(z);
}

static void send_binhdr(struct zm *z, int type, const uint8_t *h)
{
    oraw(z, ZPAD); oraw(z, ZDLE);
    if (z->txcrc32) {
        uint32_t crc = 0xFFFFFFFFu;
        oraw(z, ZBIN32);
        osend(z, type); crc = crc32b(crc, type);
        for (int i = 0; i < 4; i++) { osend(z, h[i]); crc = crc32b(crc, h[i]); }
        crc = ~crc;
        for (int i = 0; i < 4; i++) { osend(z, (int)(crc & 0xFF)); crc >>= 8; }
    } else {
        uint16_t crc = 0;
        oraw(z, ZBIN);
        osend(z, type); crc = crc16b(crc, type);
        for (int i = 0; i < 4; i++) { osend(z, h[i]); crc = crc16b(crc, h[i]); }
        osend(z, crc >> 8); osend(z, crc & 0xFF);
    }
    oflush(z);
}

static void send_data(struct zm *z, const uint8_t *p, int n, int end)
{
    if (z->txcrc32) {
        uint32_t crc = 0xFFFFFFFFu;
        for (int i = 0; i < n; i++) { osend(z, p[i]); crc = crc32b(crc, p[i]); }
        oraw(z, ZDLE); oraw(z, end); crc = crc32b(crc, end);
        crc = ~crc;
        for (int i = 0; i < 4; i++) { osend(z, (int)(crc & 0xFF)); crc >>= 8; }
    } else {
        uint16_t crc = 0;
        for (int i = 0; i < n; i++) { osend(z, p[i]); crc = crc16b(crc, p[i]); }
        oraw(z, ZDLE); oraw(z, end); crc = crc16b(crc, end);
        osend(z, crc >> 8); osend(z, crc & 0xFF);
    }
    if (end == ZCRCW) oraw(z, 0x11);
    oflush(z);
}

static void hexhdr_pos(struct zm *z, int type, long pos) { uint8_t h[4]; sethdr(h, pos); send_hexhdr(z, type, h); }

void zm_cancel(struct zm *z)
{
    static const uint8_t can[] = { 24,24,24,24,24,24,24,24,24,24, 8,8,8,8,8,8,8,8,8,8 };
    if (z->txpurge) z->txpurge(z->ctx);
    z->tx(z->ctx, can, sizeof can);
    if (z->txflush) z->txflush(z->ctx);
}

/* ----------------------------------------------------------------- input */
static int rawin(struct zm *z, int to)
{
    if (z->unget >= 0) { int c = z->unget; z->unget = -1; return c; }
    return z->rx(z->ctx, to);
}

/* one byte of a ZDLE-encoded stream: data 0-255, GOTOR|frame-end, GOTCAN, or <0 */
static int zdlread(struct zm *z, int to)
{
    int c;
    for (;;) {
        c = rawin(z, to);
        if (c < 0) return c;
        if (c == ZDLE) break;
        if (c == 0x11 || c == 0x13 || c == 0x91 || c == 0x93) continue;   /* flow control noise */
        return c;
    }
    int cans = 1;
    for (;;) {
        c = rawin(z, to);
        if (c < 0) return c;
        if (c == ZDLE) { if (++cans >= 5) return GOTCAN; continue; }
        switch (c) {
        case ZCRCE: case ZCRCG: case ZCRCQ: case ZCRCW: return c | GOTOR;
        case ZRUB0: return 0x7F;
        case ZRUB1: return 0xFF;
        case 0x11: case 0x13: case 0x91: case 0x93: continue;
        default:
            if ((c & 0x60) == 0x40) return c ^ 0x40;
            return ZM_ERROR;
        }
    }
}

static int hexval(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}
static int hexbyte(struct zm *z, int to)
{
    int a = rawin(z, to); if (a < 0) return a;
    int b = rawin(z, to); if (b < 0) return b;
    a = hexval(a & 0x7F); b = hexval(b & 0x7F);
    if (a < 0 || b < 0) return ZM_ERROR;
    return (a << 4) | b;
}

/* Wait for a header. Returns the frame type (>= 0) with z->hdr filled, or
 * <0. Garbage before the header is skipped (up to 'garbage' bytes). */
static int zgethdr(struct zm *z, int to, int garbage)
{
    int c, v = 0, cans = 0, n = 0;
    uint8_t hb[4];
    for (;;) {
        c = rawin(z, to);
        if (c < 0) return c;
        if (z->aborted && z->aborted(z->ctx)) return ZM_ABORTED;
        if (c == 24) { if (++cans >= 5) return ZM_CANCEL; continue; }
        cans = 0;
        if ((c & 0x7F) != ZPAD) { if (++n > garbage) return ZM_ERROR; continue; }
    gotpad:
        c = rawin(z, to); if (c < 0) return c;
        if ((c & 0x7F) == ZPAD) goto gotpad;
        if (c != ZDLE) { z->unget = c; continue; }
        c = rawin(z, to); if (c < 0) return c;
        int type;
        if (c == ZHEX) {
            uint16_t crc = 0;
            if ((v = hexbyte(z, to)) < 0) goto bad;
            type = v; crc = crc16b(crc, v);
            for (int i = 0; i < 4; i++) { if ((v = hexbyte(z, to)) < 0) goto bad; hb[i] = (uint8_t)v; crc = crc16b(crc, v); }
            for (int i = 0; i < 2; i++) { if ((v = hexbyte(z, to)) < 0) goto bad; crc = crc16b(crc, v); }
            if (crc != 0) { strcpy(z->msg, "Bad CRC (header)"); goto bad; }
            c = rawin(z, 500);                /* swallow CR LF */
            if (c == 0x0D || c == 0x8D) (void)rawin(z, 500);
            else if (c >= 0) z->unget = c;
            z->crc32 = 0;
        } else if (c == ZBIN) {
            uint16_t crc = 0;
            if ((v = zdlread(z, to)) < 0 || (v & GOTOR)) goto bad;
            type = v; crc = crc16b(crc, v);
            for (int i = 0; i < 4; i++) { if ((v = zdlread(z, to)) < 0 || (v & GOTOR)) goto bad; hb[i] = (uint8_t)v; crc = crc16b(crc, v); }
            for (int i = 0; i < 2; i++) { if ((v = zdlread(z, to)) < 0 || (v & GOTOR)) goto bad; crc = crc16b(crc, v); }
            if (crc != 0) { strcpy(z->msg, "Bad CRC (header)"); goto bad; }
            z->crc32 = 0;
        } else if (c == ZBIN32) {
            uint32_t crc = 0xFFFFFFFFu;
            if ((v = zdlread(z, to)) < 0 || (v & GOTOR)) goto bad;
            type = v; crc = crc32b(crc, v);
            for (int i = 0; i < 4; i++) { if ((v = zdlread(z, to)) < 0 || (v & GOTOR)) goto bad; hb[i] = (uint8_t)v; crc = crc32b(crc, v); }
            for (int i = 0; i < 4; i++) { if ((v = zdlread(z, to)) < 0 || (v & GOTOR)) goto bad; crc = crc32b(crc, v); }
            if (crc != 0xDEBB20E3u) { strcpy(z->msg, "Bad CRC (header)"); goto bad; }
            z->crc32 = 1;
        } else {
            if (c == 24) cans = 1;
            continue;
        }
        memcpy(z->hdr, hb, 4);
        z->last_rxhdr_type = type;
#ifdef ZM_HOST
        if (getenv("ZDEBUG")) fprintf(stderr, "[%d] got hdr %d pos %ld\n", (int)getpid(), type, gethdrpos(hb));
#endif
        return type;
    bad:
        if (v == ZM_CARRIER || v == ZM_TIMEOUT) return v;
        if (v == GOTCAN) return ZM_CANCEL;
        z->errors++;
        n += 8;
        if (n > garbage) return ZM_ERROR;
        return ZM_ERROR - 100;               /* bad header: caller decides */
    }
}

/* read a data subpacket into z->buf; returns the frame end (ZCRCx) or <0 */
static int zrdata(struct zm *z, int *len)
{
    int n = 0, c;
    if (z->crc32) {
        uint32_t crc = 0xFFFFFFFFu;
        for (;;) {
            c = zdlread(z, DATA_TIMEOUT);
            if (c < 0) return c;
            if (c == GOTCAN) return ZM_CANCEL;
            if (c & GOTOR) {
                int end = c & 0xFF;
                crc = crc32b(crc, end);
                for (int i = 0; i < 4; i++) { c = zdlread(z, DATA_TIMEOUT); if (c < 0) return c; if (c & GOTOR) return ZM_ERROR; crc = crc32b(crc, c); }
                if (crc != 0xDEBB20E3u) { strcpy(z->msg, "CRC error"); return ZM_ERROR; }
                *len = n;
                return end;
            }
            if (n >= 8192) { strcpy(z->msg, "Subpacket too long"); return ZM_ERROR; }
            z->buf[n++] = (uint8_t)c;
            crc = crc32b(crc, c);
        }
    } else {
        uint16_t crc = 0;
        for (;;) {
            c = zdlread(z, DATA_TIMEOUT);
            if (c < 0) return c;
            if (c == GOTCAN) return ZM_CANCEL;
            if (c & GOTOR) {
                int end = c & 0xFF;
                crc = crc16b(crc, end);
                for (int i = 0; i < 2; i++) { c = zdlread(z, DATA_TIMEOUT); if (c < 0) return c; if (c & GOTOR) return ZM_ERROR; crc = crc16b(crc, c); }
                if (crc != 0) { strcpy(z->msg, "CRC error"); return ZM_ERROR; }
                *len = n;
                return end;
            }
            if (n >= 8192) { strcpy(z->msg, "Subpacket too long"); return ZM_ERROR; }
            z->buf[n++] = (uint8_t)c;
            crc = crc16b(crc, c);
        }
    }
}

static void ev(struct zm *z, int e) { if (z->event) z->event(z, e); }

void zm_init(struct zm *z)
{
    memset(z, 0, sizeof *z);
    z->unget = -1;
    z->resume = 1;
    z->blksize = 1024;
    z->fsize = -1;
    if (!crc_ready) crc_init();
}

const char *zm_errstr(int rc)
{
    switch (rc) {
    case ZM_OK: return "Transfer complete";
    case ZM_TIMEOUT: return "Timed out";
    case ZM_CARRIER: return "Carrier lost";
    case ZM_CANCEL: return "Cancelled by remote";
    case ZM_ABORTED: return "Aborted by user";
    case ZM_FILEERR: return "File error";
    default: return "Protocol error";
    }
}

static const char *base_name(const char *p)
{
    const char *b = p;
    for (; *p; p++) if (*p == '/' || *p == '\\' || *p == ':') b = p + 1;
    return b;
}

/* ================================================================ SEND */
static int get_rinit(struct zm *z)
{
    for (int tries = 0; tries < 10; tries++) {
        int c = zgethdr(z, HDR_TIMEOUT, 4096);
        switch (c) {
        case ZRINIT:
            z->rxflags = z->hdr[ZF0];
            z->txcrc32 = (z->rxflags & CANFC32) != 0;
            z->escctl = (z->rxflags & ESCCTL) != 0;
            /* a receiver that started first answers our ZRQINIT with a
             * second ZRINIT: eat the duplicates, or they would make us send
             * ZFILE twice */
            for (int k = 0; k < 4; k++) {
                int d = zgethdr(z, 1200, 256);
                if (d == ZM_CARRIER || d == ZM_CANCEL) return d;
                if (d != ZRINIT) break;
            }
            return ZM_OK;
        case ZCHALLENGE: send_hexhdr(z, ZACK, z->hdr); tries--; break;
        case ZCAN: case ZABORT: case ZFERR: case ZM_CANCEL: return ZM_CANCEL;
        case ZM_CARRIER: case ZM_ABORTED: return c;
        case ZRQINIT: break;              /* our own echo, or a confused peer */
        default: {
            uint8_t h[4] = { 0, 0, 0, 0 };
            send_hexhdr(z, ZRQINIT, h);
        }
        }
    }
    return ZM_TIMEOUT;
}

static long file_crc(int fd, long len)
{
    uint32_t crc = 0xFFFFFFFFu;
    uint8_t b[512];
    lseek(fd, 0, SEEK_SET);
    while (len > 0) {
        int n = (int)read(fd, b, len > 512 ? 512 : (int)len);
        if (n <= 0) break;
        crc = zm_crc32(crc, b, n);
        len -= n;
    }
    return (long)~crc;
}

/* stream the file from z->pos; returns ZM_OK when the receiver accepted
 * ZEOF, ZSKIP when it skipped, else an error */
static int send_filedata(struct zm *z, int fd)
{
    uint8_t h[4];
    int maxblk = z->blksize > 0 && z->blksize <= 8192 ? z->blksize : 1024;
    static int blk;
    static uint8_t data[8192];
    int eofretries = 0, good = 0;
    if (z->pos == z->startpos) blk = maxblk;
    goto start;
restart:
    /* line trouble: smaller subpackets lose less on each hit */
    if (blk > 256) blk /= 2;
    good = 0;
start:
    if (lseek(fd, z->pos, SEEK_SET) < 0) return ZM_FILEERR;
    sethdr(h, z->pos);
    send_binhdr(z, ZDATA, h);
    for (;;) {
        int n = (int)read(fd, data, blk);
        if (n < 0) return ZM_FILEERR;
        int end = n < blk ? ZCRCE : ZCRCG;
        if (n == blk && z->fsize >= 0 && z->pos + n >= z->fsize) end = ZCRCE;
        send_data(z, data, n, end);
        z->pos += n;
        if (++good >= 8 && blk < maxblk) { blk *= 2; good = 0; }
        ev(z, ZE_DATA);
        if (end == ZCRCE) break;
        if (z->aborted && z->aborted(z->ctx)) { zm_cancel(z); return ZM_ABORTED; }
        /* the reverse channel: anything from the receiver means trouble */
        while (z->rxready(z->ctx) > 0) {
            int c = rawin(z, 0);
            if (c < 0) break;
            if (c == ZPAD || c == 24) {
                z->unget = c;
                int t = zgethdr(z, 3000, 64);
                if (t == ZRPOS) {
                    if (z->txpurge) z->txpurge(z->ctx);
                    send_data(z, data, 0, ZCRCE);
                    z->pos = gethdrpos(z->hdr);
                    z->errors++;
                    snprintf(z->msg, sizeof z->msg, "Resend from %ld", z->pos);
                    ev(z, ZE_MSG);
                    goto restart;
                }
                if (t == ZSKIP) return ZSKIP;
                if (t == ZM_CANCEL || t == ZCAN || t == ZABORT || t == ZFERR) return ZM_CANCEL;
                if (t == ZM_CARRIER) return t;
            }
        }
    }
    /* end of file */
    for (;;) {
        sethdr(h, z->pos);
        send_binhdr(z, ZEOF, h);
        if (z->txflush) z->txflush(z->ctx);
        int t = zgethdr(z, HDR_TIMEOUT, 16384);
        switch (t) {
        case ZRINIT: return ZM_OK;
        case ZSKIP: return ZSKIP;
        case ZACK: continue;
        case ZRPOS:
            if (z->txpurge) z->txpurge(z->ctx);
            z->pos = gethdrpos(z->hdr);
            z->errors++;
            snprintf(z->msg, sizeof z->msg, "Resend from %ld", z->pos);
            ev(z, ZE_MSG);
            goto restart;
        case ZM_CANCEL: case ZCAN: case ZABORT: case ZFERR: return ZM_CANCEL;
        case ZM_CARRIER: case ZM_ABORTED: return t;
        default:
            if (++eofretries > 10) return ZM_TIMEOUT;
        }
    }
}

static int send_one(struct zm *z, const char *path)
{
    struct stat st;
    int fd = open(path, O_RDONLY | O_BINARY);
    if (fd < 0) { snprintf(z->msg, sizeof z->msg, "Can't open %.40s", path); ev(z, ZE_MSG); return ZSKIP; }
    if (fstat(fd, &st) == 0) z->fsize = (long)st.st_size; else z->fsize = lseek(fd, 0, SEEK_END);
    long mtime = (long)st.st_mtime;
    strncpy(z->path, path, sizeof z->path - 1);
    strncpy(z->fname, base_name(path), sizeof z->fname - 1);
    z->fname[sizeof z->fname - 1] = 0;
    z->pos = z->startpos = 0;
    z->errors = 0;
    z->msg[0] = 0;
    ev(z, ZE_FILE);

    /* ZFILE: name NUL "size mtime mode serial filesleft bytesleft" NUL */
    uint8_t info[160];
    int nl = 0;
    for (const char *p = z->fname; *p && nl < 64; p++) {
        char c = *p;
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');   /* DOS names: send lower case, as DSZ did */
        info[nl++] = (uint8_t)c;
    }
    info[nl++] = 0;
    nl += snprintf((char *)info + nl, sizeof info - nl, "%ld %lo 100644 0 %d %ld", z->fsize, mtime, z->filesleft, z->bytesleft);
    info[nl++] = 0;

    for (int tries = 0; tries < 10; tries++) {
        uint8_t h[4] = { 0, 0, 0, 0 };
        h[ZF0] = z->recover ? ZCRECOV : ZCBIN;
        send_binhdr(z, ZFILE, h);
        send_data(z, info, nl, ZCRCW);
    again:;
        int t = zgethdr(z, HDR_TIMEOUT, 4096);
        switch (t) {
        case ZRINIT: continue;                         /* didn't get it: resend */
        case ZRPOS: {
            z->pos = z->startpos = gethdrpos(z->hdr);
            if (z->pos > 0) { snprintf(z->msg, sizeof z->msg, "Resuming at %ld", z->pos); ev(z, ZE_MSG); }
            z->t0 = ZTICKS();
            int r = send_filedata(z, fd);
            close(fd);
            if (r == ZSKIP) { strcpy(z->msg, "Skipped by receiver"); ev(z, ZE_SKIP); return ZSKIP; }
            if (r == ZM_OK) { z->files++; ev(z, ZE_DONEFILE); }
            return r;
        }
        case ZSKIP:
            close(fd);
            strcpy(z->msg, "Skipped by receiver");
            ev(z, ZE_SKIP);
            return ZSKIP;
        case ZCRC: {
            uint8_t h2[4];
            sethdr(h2, file_crc(fd, z->fsize));
            send_hexhdr(z, ZCRC, h2);
            goto again;
        }
        case ZM_CANCEL: case ZCAN: case ZABORT: case ZFERR: close(fd); return ZM_CANCEL;
        case ZM_CARRIER: case ZM_ABORTED: close(fd); return t;
        default: break;                               /* ZNAK, timeout, garbage: resend */
        }
    }
    close(fd);
    return ZM_TIMEOUT;
}

int zm_send(struct zm *z, char *const *paths, int n)
{
    uint8_t h[4] = { 0, 0, 0, 0 };
    int rc;
    z->unget = -1;
    z->files = 0;
    z->txcrc32 = 0; z->escctl = 0;
    /* "rz\r" starts rz on a Unix host; then ZRQINIT */
    static const uint8_t rz[] = { 'r', 'z', '\r' };
    z->tx(z->ctx, rz, 3);
    send_hexhdr(z, ZRQINIT, h);
    rc = get_rinit(z);
    if (rc != ZM_OK) { if (rc != ZM_CARRIER) zm_cancel(z); return rc; }

    z->bytesleft = 0;
    for (int i = 0; i < n; i++) {
        struct stat st;
        if (stat(paths[i], &st) == 0) z->bytesleft += (long)st.st_size;
    }
    for (int i = 0; i < n; i++) {
        z->filesleft = n - i;
        rc = send_one(z, paths[i]);
        if (rc == ZSKIP) rc = ZM_OK;
        if (rc != ZM_OK) {
            if (rc == ZM_ABORTED || rc == ZM_TIMEOUT || rc == ZM_ERROR || rc == ZM_FILEERR) zm_cancel(z);
            return rc;
        }
        z->bytesleft -= z->fsize;
    }
    /* ZFIN / ZFIN / OO */
    for (int tries = 0; tries < 5; tries++) {
        send_hexhdr(z, ZFIN, h);
        if (z->txflush) z->txflush(z->ctx);
        int t = zgethdr(z, 5000, 1024);
        if (t == ZFIN) { static const uint8_t oo[] = { 'O', 'O' }; z->tx(z->ctx, oo, 2); if (z->txflush) z->txflush(z->ctx); return ZM_OK; }
        if (t == ZM_CARRIER || t == ZM_CANCEL) return ZM_OK;   /* files are across anyway */
    }
    return ZM_OK;
}

/* ============================================================= RECEIVE */
static void send_rinit(struct zm *z)
{
    uint8_t h[4] = { 0, 0, 0, 0 };
    h[ZF0] = CANFDX | CANOVIO | (z->no32 ? 0 : CANFC32);
    send_hexhdr(z, ZRINIT, h);
}

/* make a DOS 8.3 name out of whatever the sender called it */
static void dosname(char *out, const char *in)
{
    char base[9] = "", ext[4] = "";
    int nb = 0, ne = 0, inext = 0;
    const char *dot = strrchr(in, '.');
    for (const char *p = in; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (p == dot) { inext = 1; continue; }
        if (c >= 'a' && c <= 'z') c = (unsigned char)(c - 32);
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || strchr("!#$%&'()-@^_`{}~", c))) {
            if (c == '.' || c == ' ') continue;
            c = '_';
        }
        if (!inext) { if (nb < 8) base[nb++] = (char)c; }
        else if (ne < 3) ext[ne++] = (char)c;
    }
    base[nb] = 0; ext[ne] = 0;
    if (!nb) strcpy(base, "NONAME");
    strcpy(out, base);
    if (ne) { strcat(out, "."); strcat(out, ext); }
}

#ifndef ZM_HOST
static void set_ftime(int fd, long t)
{
    time_t tt = (time_t)t;
    struct tm *tm = gmtime(&tt);
    if (!tm || tm->tm_year < 80) return;
    unsigned d = (unsigned)(((tm->tm_year - 80) << 9) | ((tm->tm_mon + 1) << 5) | tm->tm_mday);
    unsigned tv = (unsigned)((tm->tm_hour << 11) | (tm->tm_min << 5) | (tm->tm_sec / 2));
    _dos_setftime(fd, d, tv);
}
#endif

int zm_receive(struct zm *z)
{
    int fd = -1, tries = 0, t, bad = 0;
    long mtime = 0;
    z->unget = -1;
    z->files = 0;
    z->fname[0] = 0;
    send_rinit(z);
    for (;;) {
        t = zgethdr(z, HDR_TIMEOUT, fd >= 0 ? 65536 : 8192);
        if (t == ZM_CARRIER || t == ZM_ABORTED) {
            if (fd >= 0) close(fd);
            if (t == ZM_ABORTED) zm_cancel(z);
            return t;
        }
        if (t == ZM_CANCEL || t == ZCAN || t == ZABORT) { if (fd >= 0) close(fd); return ZM_CANCEL; }
        if (t < 0) {
            if (++tries > 12) { if (fd >= 0) close(fd); zm_cancel(z); return ZM_TIMEOUT; }
            if (fd >= 0) { hexhdr_pos(z, ZRPOS, z->pos); strcpy(z->msg, t == ZM_TIMEOUT ? "Timeout" : "Bad header"); z->errors++; ev(z, ZE_MSG); }
            else send_rinit(z);
            continue;
        }
        switch (t) {
        case ZRQINIT:
            if (fd < 0) send_rinit(z);
            break;
        case ZSINIT: {
            int len, e = zrdata(z, &len);
            if (e == ZCRCW || e == ZCRCE || e == ZCRCQ || e == ZCRCG) hexhdr_pos(z, ZACK, 1);
            else { uint8_t h[4] = { 0 }; send_hexhdr(z, ZNAK, h); }
            break;
        }
        case ZFILE: {
            int len, e = zrdata(z, &len);
            if (e < 0) { uint8_t h[4] = { 0 }; if (e == ZM_CANCEL) return e; send_hexhdr(z, ZNAK, h); break; }
            z->buf[len] = 0;
            z->buf[len + 1] = 0;
            const char *name = (const char *)z->buf;
            const char *rest = name + strlen(name) + 1;
            char dn[16];
            dosname(dn, base_name(name));
            if (fd >= 0 && strcmp(dn, z->fname) == 0) { hexhdr_pos(z, ZRPOS, z->pos); break; }   /* resent */
            if (fd >= 0) { close(fd); fd = -1; }
            long size = -1; mtime = 0;
            if ((const uint8_t *)rest < z->buf + len) {
                char *ep;
                size = strtol(rest, &ep, 10);
                if (ep != rest) mtime = strtol(ep, NULL, 8);
                else size = -1;
            }
            strcpy(z->fname, dn);
            snprintf(z->path, sizeof z->path, "%s%s%s", z->dir ? z->dir : "",
                     (z->dir && *z->dir && z->dir[strlen(z->dir) - 1] != '\\' && z->dir[strlen(z->dir) - 1] != '/') ? "\\" : "", dn);
#ifdef ZM_HOST
            for (char *p = z->path; *p; p++) if (*p == '\\') *p = '/';
#endif
            z->fsize = size;
            z->pos = 0;
            z->errors = 0;
            z->msg[0] = 0;
            struct stat st;
            int exists = stat(z->path, &st) == 0;
            int recov = z->hdr[ZF0] == ZCRECOV;
            if (exists && (z->resume || recov) && size > 0 && (long)st.st_size < size && st.st_size > 0)
                z->pos = (long)st.st_size;
            else if (exists && recov && size >= 0 && (long)st.st_size == size) {
                strcpy(z->msg, "Already have it - skipped");
                ev(z, ZE_SKIP);
                uint8_t h[4] = { 0 };
                send_hexhdr(z, ZSKIP, h);
                break;
            }
            fd = open(z->path, O_WRONLY | O_CREAT | O_BINARY | (z->pos ? 0 : O_TRUNC), 0644);
            if (fd < 0) {
                snprintf(z->msg, sizeof z->msg, "Can't create %.40s", z->path);
                ev(z, ZE_SKIP);
                uint8_t h[4] = { 0 };
                send_hexhdr(z, ZSKIP, h);
                break;
            }
            if (z->pos) {
                lseek(fd, z->pos, SEEK_SET);
                snprintf(z->msg, sizeof z->msg, "Resuming at %ld", z->pos);
            }
            z->startpos = z->pos;
            z->t0 = ZTICKS();
            tries = 0;
            ev(z, ZE_FILE);
            hexhdr_pos(z, ZRPOS, z->pos);
            break;
        }
        case ZDATA: {
            if (fd < 0) { send_rinit(z); break; }
            if (gethdrpos(z->hdr) != z->pos) { hexhdr_pos(z, ZRPOS, z->pos); break; }
            for (;;) {
                int len, e = zrdata(z, &len);
                if (e == ZCRCG || e == ZCRCQ || e == ZCRCW || e == ZCRCE) {
                    if (len && write(fd, z->buf, len) != len) {
                        close(fd); fd = -1;
                        uint8_t h[4] = { 0 };
                        send_hexhdr(z, ZFERR, h);
                        strcpy(z->msg, "Disk full");
                        ev(z, ZE_MSG);
                        zm_cancel(z);
                        return ZM_FILEERR;
                    }
                    z->pos += len;
                    tries = 0; bad = 0;
                    ev(z, ZE_DATA);
                    if (e == ZCRCW || e == ZCRCQ) hexhdr_pos(z, ZACK, z->pos);
                    if (z->aborted && z->aborted(z->ctx)) { close(fd); zm_cancel(z); return ZM_ABORTED; }
                    if (e == ZCRCW || e == ZCRCE) break;
                    continue;
                }
                if (e == ZM_CANCEL) { close(fd); return ZM_CANCEL; }
                if (e == ZM_CARRIER) { close(fd); return ZM_CARRIER; }
                if (z->aborted && z->aborted(z->ctx)) { close(fd); zm_cancel(z); return ZM_ABORTED; }
                /* CRC error, timeout, garbage: ask for a resend */
                z->errors++;
                if (e == ZM_TIMEOUT) strcpy(z->msg, "Timeout");
                ev(z, ZE_MSG);
                if (++bad > 25) { close(fd); zm_cancel(z); return ZM_ERROR; }
                hexhdr_pos(z, ZRPOS, z->pos);
                break;
            }
            break;
        }
        case ZEOF:
            if (fd < 0) { send_rinit(z); break; }
            if (gethdrpos(z->hdr) != z->pos) break;           /* stale: data is still coming */
#ifndef ZM_HOST
            if (mtime) set_ftime(fd, mtime);
#endif
            close(fd); fd = -1;
#ifdef ZM_HOST
            if (mtime) { struct utimbuf u = { (time_t)mtime, (time_t)mtime }; utime(z->path, &u); }
#endif
            z->files++;
            ev(z, ZE_DONEFILE);
            send_rinit(z);
            break;
        case ZFIN: {
            uint8_t h[4] = { 0 };
            if (fd >= 0) { close(fd); fd = -1; }
            send_hexhdr(z, ZFIN, h);
            if (z->txflush) z->txflush(z->ctx);
            /* "OO" (over and out); a terminal must not show it */
            for (int i = 0; i < 2; i++) { int c = rawin(z, 1000); if (c != 'O') { if (c >= 0) z->unget = c; break; } }
            return ZM_OK;
        }
        case ZCOMMAND: {
            uint8_t h[4] = { 0 };
            int len; (void)zrdata(z, &len);
            sethdr(h, 0);
            send_hexhdr(z, ZCOMPL, h);                      /* "done" - but we ran nothing */
            break;
        }
        case ZFREECNT: hexhdr_pos(z, ZACK, 0x7FFFFFFF); break;
        default: break;
        }
    }
}

int zm_autodetect(int *st, int c)
{
    /* "**" ZDLE "B00" */
    static const char pat[] = { '*', '*', 0x18, 'B', '0', '0' };
    if (c == pat[*st]) { if (++*st == 6) { *st = 0; return 1; } }
    else if (c == '*') *st = (*st == 1 || *st == 2) ? 2 : 1;
    else *st = 0;
    return 0;
}
