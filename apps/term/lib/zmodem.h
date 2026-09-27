/* zmodem.h - ZMODEM file transfer (Chuck Forsberg's protocol, 1986-88),
 * written from the protocol description for ARM-DOS; see zmodem.c. */
#ifndef ZMODEM_H
#define ZMODEM_H
#include <stdint.h>

#define ZM_OK        0
#define ZM_TIMEOUT  -1
#define ZM_CARRIER  -2      /* carrier lost */
#define ZM_ERROR    -3
#define ZM_CANCEL   -4      /* the other side cancelled (CAN CAN CAN...) */
#define ZM_ABORTED  -5      /* the local user cancelled */
#define ZM_FILEERR  -6

/* progress events */
enum { ZE_FILE = 1, ZE_DATA, ZE_DONEFILE, ZE_MSG, ZE_SKIP };

struct zm {
    /* ---- supplied by the caller ---- */
    void *ctx;
    int  (*rx)(void *ctx, int timeout_ms);        /* byte, ZM_TIMEOUT or ZM_CARRIER */
    int  (*rxready)(void *ctx);                    /* bytes waiting */
    void (*tx)(void *ctx, const uint8_t *b, int n);
    void (*txflush)(void *ctx);                    /* wait until sent */
    void (*txpurge)(void *ctx);                    /* drop what is still queued */
    int  (*aborted)(void *ctx);                    /* local user wants out (Esc) */
    void (*event)(struct zm *z, int ev);
    const char *dir;                               /* receive: directory ("" = current) */
    int  resume;                                   /* receive: crash recovery (continue a shorter file) */
    int  recover;                                  /* send: ask for crash recovery (ZCRECOV) */
    int  blksize;                                  /* send: subpacket size (default 1024) */
    int  no32;                                     /* receive: don't offer CRC-32 */

    /* ---- state, readable by the event callback ---- */
    char fname[80];         /* current file name (as sent / as stored) */
    char path[128];         /* local path */
    long fsize;             /* -1 unknown */
    long pos, startpos;     /* bytes done; where this file started (resume) */
    uint32_t t0;            /* BIOS ticks at the start of this file's data */
    int  files, filesleft;
    long bytesleft;
    int  errors;
    int  crc32;             /* current frame's CRC type */
    char msg[64];           /* last status message ("CRC error", "Resuming at 1024") */
    int  last_rxhdr_type;

    /* ---- internal ---- */
    int  unget;
    int  txcrc32, escctl;
    uint8_t rxflags;
    uint8_t hdr[4];
    uint8_t buf[8192 + 16];
    uint8_t obuf[2600];
    int  on;
};

void zm_init(struct zm *z);
int  zm_send(struct zm *z, char *const *paths, int n);
int  zm_receive(struct zm *z);
void zm_cancel(struct zm *z);                  /* send the abort sequence */
const char *zm_errstr(int rc);
uint32_t zm_crc32(uint32_t crc, const uint8_t *p, int n);

/* Recognising "**\x18B00" (a hex ZRQINIT) in a terminal stream: feed each
 * received byte; returns 1 when a ZMODEM sender just started. */
int zm_autodetect(int *state, int c);
#endif
