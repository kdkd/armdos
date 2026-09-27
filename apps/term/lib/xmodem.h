/* xmodem.h - XMODEM (checksum/CRC, 128/1K), YMODEM batch and YMODEM-G. */
#ifndef XMODEM_H
#define XMODEM_H
#include <stdint.h>

enum { XE_FILE = 1, XE_DATA, XE_DONE };

struct xm {
    void *ctx;
    int  (*rx)(void *ctx, int timeout_ms);       /* byte or <0 (-1 timeout, -2 carrier) */
    void (*tx)(void *ctx, const uint8_t *b, int n);
    void (*txflush)(void *ctx);
    void (*txpurge)(void *ctx);
    int  (*aborted)(void *ctx);
    void (*event)(struct xm *x, int ev);
    const char *dir;          /* batch receive directory */
    int  onek;                /* send 1K blocks (XMODEM-1K / YMODEM) */
    int  gmode;               /* YMODEM-G: no per-block acknowledgements */
    char fname[80];
    long fsize, pos;
    uint32_t t0;
    int  errors, files;
    char msg[48];
    int  peek;
    uint8_t blk[1030];
};

void xm_init(struct xm *x);
int  xm_send(struct xm *x, const char *path);                   /* XMODEM */
int  xm_recv(struct xm *x, const char *path);
int  xm_send_batch(struct xm *x, char *const *paths, int n);    /* YMODEM */
int  xm_recv_batch(struct xm *x);
#endif
