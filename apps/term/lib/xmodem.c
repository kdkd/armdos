/* xmodem.c - XMODEM, XMODEM-CRC, XMODEM-1K, YMODEM batch, YMODEM-G.
 * Original code for ARM-DOS from Ward Christensen's XMODEM (1977) and Chuck
 * Forsberg's YMODEM (1985-88) descriptions. Returns the ZM_* codes of
 * zmodem.h so callers can share messages. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include "xmodem.h"
#include "zmodem.h"
#ifdef ZM_HOST
#ifndef O_BINARY
#define O_BINARY 0
#endif
extern uint32_t zm_host_ticks(void);
#define XTICKS() zm_host_ticks()
#else
#include "comm.h"
#define XTICKS() TICKS()
#endif

#define SOH 0x01
#define STX 0x02
#define EOT 0x04
#define ACK 0x06
#define NAK 0x15
#define CAN 0x18
#define SUB 0x1A

static uint16_t crc16(const uint8_t *p, int n)
{
    uint16_t c = 0;
    while (n-- > 0) {
        c ^= (uint16_t)(*p++ << 8);
        for (int k = 0; k < 8; k++) c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x1021) : (uint16_t)(c << 1);
    }
    return c;
}

void xm_init(struct xm *x) { memset(x, 0, sizeof *x); x->peek = -1; }
static int rxb(struct xm *x, int to)
{
    if (x->peek >= 0) { int c = x->peek; x->peek = -1; return c; }
    return x->rx(x->ctx, to);
}

static void put1(struct xm *x, int c) { uint8_t b = (uint8_t)c; x->tx(x->ctx, &b, 1); }
static void cancel(struct xm *x)
{
    static const uint8_t c[] = { CAN, CAN, CAN, CAN, CAN, CAN, CAN, CAN, 8, 8, 8, 8, 8, 8, 8, 8 };
    if (x->txpurge) x->txpurge(x->ctx);
    x->tx(x->ctx, c, sizeof c);
    if (x->txflush) x->txflush(x->ctx);
}
static void ev(struct xm *x, int e) { if (x->event) x->event(x, e); }
static int user_abort(struct xm *x) { return x->aborted && x->aborted(x->ctx); }
static void drain(struct xm *x) { while (x->rx(x->ctx, 300) >= 0) ; }

static const char *base_name(const char *p)
{
    const char *b = p;
    for (; *p; p++) if (*p == '/' || *p == '\\' || *p == ':') b = p + 1;
    return b;
}

/* ------------------------------------------------------------ sending */
/* wait for the receiver's start character: 'C' (CRC), NAK (checksum), 'G' */
static int wait_start(struct xm *x, int *crc)
{
    int cans = 0;
    uint32_t t0 = XTICKS();
    while (XTICKS() - t0 < 60 * 18) {
        int c = x->rx(x->ctx, 1000);
        if (c == -2) return ZM_CARRIER;
        if (user_abort(x)) { cancel(x); return ZM_ABORTED; }
        if (c == 'C' || c == 'G') { *crc = 1; if (c == 'G') x->gmode = 1; return 0; }
        if (c == NAK) { *crc = 0; return 0; }
        if (c == CAN) { if (++cans >= 2) return ZM_CANCEL; } else cans = 0;
    }
    return ZM_TIMEOUT;
}

static int send_block(struct xm *x, int num, const uint8_t *data, int len, int crc)
{
    uint8_t *b = x->blk;
    int n = 0;
    b[n++] = len == 1024 ? STX : SOH;
    b[n++] = (uint8_t)num;
    b[n++] = (uint8_t)~num;
    memcpy(b + n, data, len); n += len;
    if (crc) { uint16_t c = crc16(data, len); b[n++] = (uint8_t)(c >> 8); b[n++] = (uint8_t)c; }
    else { uint8_t s = 0; for (int i = 0; i < len; i++) s += data[i]; b[n++] = s; }
    for (int tries = 0; tries < 10; tries++) {
        x->tx(x->ctx, b, n);
        if (x->gmode) return 0;
        int cans = 0;
        for (;;) {
            int c = x->rx(x->ctx, 10000);
            if (c == -2) return ZM_CARRIER;
            if (c == ACK) return 0;
            if (c == CAN) { if (++cans >= 2) return ZM_CANCEL; continue; }
            if (c == NAK || c == -1 || c == 'C') break;
        }
        x->errors++;
        snprintf(x->msg, sizeof x->msg, "Block %d resent", num);
        ev(x, XE_DATA);
        if (user_abort(x)) { cancel(x); return ZM_ABORTED; }
    }
    cancel(x);
    return ZM_ERROR;
}

static int send_eot(struct xm *x, int ymodem)
{
    for (int tries = 0; tries < 10; tries++) {
        put1(x, EOT);
        int c = x->rx(x->ctx, 10000);
        if (c == ACK) return 0;
        if (c == -2) return ZM_CARRIER;
        if (c == NAK && ymodem) continue;       /* YMODEM receivers NAK the first EOT */
    }
    return ZM_TIMEOUT;
}

static int send_data(struct xm *x, int fd, int crc, int onek)
{
    uint8_t data[1024];
    int num = 1;
    x->pos = 0;
    x->t0 = XTICKS();
    for (;;) {
        int want = onek && (x->fsize < 0 || x->fsize - x->pos > 896) ? 1024 : 128;
        int n = (int)read(fd, data, want);
        if (n <= 0) break;
        if (n < want) { memset(data + n, SUB, want - n); if (want == 1024 && n <= 128) want = 128; }
        int r = send_block(x, num & 0xFF, data, want, crc);
        if (r) return r;
        num++;
        x->pos += n;
        x->msg[0] = 0;
        ev(x, XE_DATA);
        if (user_abort(x)) { cancel(x); return ZM_ABORTED; }
    }
    return 0;
}

int xm_send(struct xm *x, const char *path)
{
    int crc, r;
    int fd = open(path, O_RDONLY | O_BINARY);
    if (fd < 0) return ZM_FILEERR;
    snprintf(x->fname, sizeof x->fname, "%s", base_name(path));
    x->fsize = lseek(fd, 0, SEEK_END); lseek(fd, 0, SEEK_SET);
    strcpy(x->msg, "Waiting for receiver");
    ev(x, XE_FILE);
    if ((r = wait_start(x, &crc)) != 0) { close(fd); return r; }
    r = send_data(x, fd, crc, x->onek);
    close(fd);
    if (r) return r;
    r = send_eot(x, 0);
    if (!r) { x->files++; ev(x, XE_DONE); }
    return r;
}

int xm_send_batch(struct xm *x, char *const *paths, int n)
{
    int crc, r;
    uint8_t hdr[128];
    for (int i = 0; i <= n; i++) {
        strcpy(x->msg, "Waiting for receiver");
        if ((r = wait_start(x, &crc)) != 0) return r;
        memset(hdr, 0, sizeof hdr);
        int fd = -1;
        if (i < n) {
            struct stat st;
            fd = open(paths[i], O_RDONLY | O_BINARY);
            if (fd < 0) { cancel(x); return ZM_FILEERR; }
            fstat(fd, &st);
            x->fsize = (long)st.st_size;
            snprintf(x->fname, sizeof x->fname, "%s", base_name(paths[i]));
            int l = 0;
            for (const char *p = x->fname; *p && l < 60; p++) hdr[l++] = (uint8_t)((*p >= 'A' && *p <= 'Z') ? *p + 32 : *p);
            hdr[l++] = 0;
            snprintf((char *)hdr + l, sizeof hdr - l, "%ld %lo 0", x->fsize, (long)st.st_mtime);
            ev(x, XE_FILE);
        }
        int g = x->gmode; x->gmode = 0;         /* block 0 is always acknowledged */
        r = send_block(x, 0, hdr, 128, 1);
        x->gmode = g;
        if (r) { if (fd >= 0) close(fd); return r; }
        if (i == n) break;
        if ((r = wait_start(x, &crc)) != 0) { close(fd); return r; }
        r = send_data(x, fd, 1, 1);
        close(fd);
        if (r) return r;
        if ((r = send_eot(x, 1)) != 0) return r;
        x->files++;
        ev(x, XE_DONE);
    }
    return 0;
}

/* ------------------------------------------------------------ receiving */
/* read one block: returns block number (0-255), -10 for EOT, or <0 */
static int get_block(struct xm *x, int crc, uint8_t *data, int *len, int to)
{
    int c = rxb(x, to);
    if (c < 0) return c;
    if (c == EOT) return -10;
    if (c == CAN) { c = rxb(x, 1000); if (c == CAN) return ZM_CANCEL; return ZM_ERROR; }
    if (c != SOH && c != STX) return ZM_ERROR;
    int n = c == STX ? 1024 : 128;
    int num = rxb(x, 1000), inv = rxb(x, 1000);
    if (num < 0 || inv < 0) return ZM_ERROR;
    for (int i = 0; i < n; i++) { c = rxb(x, 1000); if (c < 0) return c == -2 ? c : ZM_ERROR; data[i] = (uint8_t)c; }
    if (crc) {
        int h = rxb(x, 1000), l = rxb(x, 1000);
        if (h < 0 || l < 0) return ZM_ERROR;
        if (crc16(data, n) != (uint16_t)((h << 8) | l)) { strcpy(x->msg, "CRC error"); return ZM_ERROR; }
    } else {
        int s = rxb(x, 1000); uint8_t sum = 0;
        for (int i = 0; i < n; i++) sum += data[i];
        if (s < 0 || sum != (uint8_t)s) { strcpy(x->msg, "Checksum error"); return ZM_ERROR; }
    }
    if ((uint8_t)num != (uint8_t)~inv) return ZM_ERROR;
    *len = n;
    return num;
}

/* receive the data blocks of one file into fd; first block number 1 */
static int recv_data(struct xm *x, int fd, int crc, int startch, int ymodem)
{
    uint8_t data[1024];
    int expect = 1, errors = 0, len, eots = 0;
    x->pos = 0;
    x->t0 = XTICKS();
    if (startch) put1(x, startch);
    for (;;) {
        int b = get_block(x, crc, data, &len, 10000);
        if (b == -2 || b == ZM_CANCEL) return b;
        if (b == -10) {
            if (ymodem && !x->gmode && eots++ == 0) { put1(x, NAK); continue; }
            put1(x, ACK);
            return 0;
        }
        if (b < 0) {
            if (x->gmode) { cancel(x); return ZM_ERROR; }
            if (++errors > 10) { cancel(x); return ZM_ERROR; }
            x->errors++;
            drain(x);
            ev(x, XE_DATA);
            put1(x, NAK);
            continue;
        }
        if (b == ((expect - 1) & 0xFF)) { put1(x, ACK); continue; }  /* duplicate */
        if (b != (expect & 0xFF)) { cancel(x); strcpy(x->msg, "Out of sequence"); return ZM_ERROR; }
        long keep = len;
        if (x->fsize >= 0 && x->pos + keep > x->fsize) keep = x->fsize - x->pos;
        if (keep > 0 && write(fd, data, keep) != keep) { cancel(x); return ZM_FILEERR; }
        x->pos += keep;
        expect++;
        errors = 0;
        if (!x->gmode) put1(x, ACK);
        x->msg[0] = 0;
        ev(x, XE_DATA);
        if (user_abort(x)) { cancel(x); return ZM_ABORTED; }
    }
}

int xm_recv(struct xm *x, const char *path)
{
    snprintf(x->fname, sizeof x->fname, "%s", base_name(path));
    x->fsize = -1;
    ev(x, XE_FILE);
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0644);
    if (fd < 0) return ZM_FILEERR;
    /* try CRC first ('C' three times), then checksum (NAK) */
    int r = ZM_TIMEOUT;
    for (int t = 0; t < 8; t++) {
        int crc = t < 3;
        put1(x, crc ? 'C' : NAK);
        int c = x->rx(x->ctx, 3000);
        if (c == -2) { r = c; break; }
        if (user_abort(x)) { r = ZM_ABORTED; cancel(x); break; }
        if (c == SOH || c == STX) {
            x->peek = c;
            r = recv_data(x, fd, crc, 0, 0);
            break;
        }
    }
    close(fd);
    if (!r) { x->files++; ev(x, XE_DONE); }
    return r;
}

int xm_recv_batch(struct xm *x)
{
    uint8_t data[1024];
    int len;
    int startch = x->gmode ? 'G' : 'C';
    for (;;) {
        int b = -1, tries;
        for (tries = 0; tries < 10; tries++) {
            put1(x, startch);
            b = get_block(x, 1, data, &len, 5000);
            if (b == 0 || b == -2 || b == ZM_CANCEL) break;
            if (user_abort(x)) { cancel(x); return ZM_ABORTED; }
            if (b != -1) drain(x);
        }
        if (b == -2 || b == ZM_CANCEL) return b;
        if (b != 0) { cancel(x); return ZM_TIMEOUT; }
        put1(x, ACK);
        if (!data[0]) return 0;                 /* empty header: end of batch */
        char *name = (char *)data;
        name[len - 1] = 0;
        const char *info = name + strlen(name) + 1;
        x->fsize = -1;
        if (*info) x->fsize = strtol(info, NULL, 10);
        /* DOS name */
        char dn[16]; int o = 0, ext = 0, eo = 0;
        const char *bn = base_name(name), *dot = strrchr(bn, '.');
        for (const char *p = bn; *p; p++) {
            char c = *p;
            if (c >= 'a' && c <= 'z') c -= 32;
            if (p == dot) { if (o) { dn[o++] = '.'; ext = 1; } continue; }
            if (c == '.' || c == ' ') continue;
            if (!ext && o < 8) dn[o++] = c;
            else if (ext && eo < 3) { dn[o++] = c; eo++; }
        }
        dn[o] = 0;
        if (!o) strcpy(dn, "NONAME");
        snprintf(x->fname, sizeof x->fname, "%s", dn);
        char path[160];
        snprintf(path, sizeof path, "%s%s%s", x->dir ? x->dir : "", x->dir && *x->dir && x->dir[strlen(x->dir) - 1] != '\\' ? "\\" : "", dn);
#ifdef ZM_HOST
        for (char *p = path; *p; p++) if (*p == '\\') *p = '/';
#endif
        ev(x, XE_FILE);
        int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0644);
        if (fd < 0) { cancel(x); return ZM_FILEERR; }
        int r = recv_data(x, fd, 1, startch, 1);
        close(fd);
        if (r) return r;
        x->files++;
        ev(x, XE_DONE);
    }
}
