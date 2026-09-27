/*
 * cdrom.h - shared by ARMCD.SYS, ARMCDEX.EXE and CDPLAY.EXE: the CD-ROM device
 * driver interface of the 1990s CD-ROM extensions (the "MSCDEX device driver
 * specification" request set), in ARM-DOS form: DOS's byte layouts with every
 * far pointer a 4-byte flat pointer (ARCH.md 14.4), so every offset below is
 * the DOS offset.
 *
 * Copyright (C) 1993 Europa Micro Systems (ARM-DOS project).
 */
#ifndef CDROM_H
#define CDROM_H

#include <stdint.h>
#include <stddef.h>

#define PACKED __attribute__((packed))

/* the device header: ARM-DOS's 24 bytes, then the CD-ROM extension fields */
struct cdreq;
struct cddevhdr {
    struct cddevhdr *next;          /* 00 */
    uint16_t attr;                  /* 04 C800h: char, IOCTL, open/close */
    uint16_t pad;                   /* 06 */
    void (*strategy)(struct cdreq *);   /* 08 */
    void (*interrupt)(void);        /* 0C */
    char     name[8];               /* 10 "ARMCD001" */
    uint16_t reserved;              /* 18 must be 0 */
    uint8_t  drive;                 /* 1A drive letter (1 = A:), set by the extensions */
    uint8_t  units;                 /* 1B number of units */
};

/* request header (13 bytes, DOS layout) */
struct cdreq {
    uint8_t  len;                   /* 00 */
    uint8_t  unit;                  /* 01 subunit */
    uint8_t  cmd;                   /* 02 */
    uint16_t status;                /* 03 */
    uint8_t  reserved[8];           /* 05 */
} PACKED;

#define ST_ERROR    0x8000
#define ST_BUSY     0x0200          /* audio is playing */
#define ST_DONE     0x0100

/* driver error codes (status low byte, INT 24h DI) */
#define E_WRPROT    0x00
#define E_UNIT      0x01
#define E_NOTREADY  0x02
#define E_BADCMD    0x03
#define E_CRC       0x04
#define E_BADLEN    0x05
#define E_SEEK      0x06
#define E_MEDIA     0x07
#define E_NOTFOUND  0x08
#define E_READ      0x0B
#define E_GENERAL   0x0C
#define E_DISKCHG   0x0F

/* commands */
#define C_INIT          0
#define C_IOCTL_IN      3
#define C_INFLUSH       7
#define C_OUTFLUSH      11
#define C_IOCTL_OUT     12
#define C_OPEN          13
#define C_CLOSE         14
#define C_READ_LONG     128
#define C_READ_PREFETCH 130
#define C_SEEK          131
#define C_PLAY          132
#define C_STOP          133
#define C_WRITE_LONG    134
#define C_WRITE_VERIFY  135
#define C_RESUME        136

struct cdreq_init {                 /* 0 */
    struct cdreq h;
    uint8_t  units;                 /* 0D */
    uint32_t brk;                   /* 0E */
    uint32_t arg;                   /* 12 the DEVICE= line */
    uint8_t  drive;                 /* 16 */
    uint16_t cfgerr;                /* 17 */
} PACKED;

struct cdreq_ioctl {                /* 3, 12 */
    struct cdreq h;
    uint8_t  media;                 /* 0D */
    uint32_t buf;                   /* 0E the control block */
    uint16_t count;                 /* 12 */
    uint16_t start;                 /* 14 */
    uint32_t volid;                 /* 16 */
} PACKED;

struct cdreq_read {                 /* 128, 130, 131 */
    struct cdreq h;
    uint8_t  addrmode;              /* 0D 0 = HSG (LBA), 1 = Red Book (MSF) */
    uint32_t buf;                   /* 0E */
    uint16_t count;                 /* 12 sectors */
    uint32_t start;                 /* 14 */
    uint8_t  mode;                  /* 18 0 cooked (2048), 1 raw (2352) */
    uint8_t  isize;                 /* 19 interleave size */
    uint8_t  iskip;                 /* 1A interleave skip */
} PACKED;

struct cdreq_play {                 /* 132 */
    struct cdreq h;
    uint8_t  addrmode;              /* 0D */
    uint32_t start;                 /* 0E */
    uint32_t count;                 /* 12 sectors */
} PACKED;

/* IOCTL input control block codes */
#define IOI_DEVHDR      0
#define IOI_HEAD        1
#define IOI_ERRSTAT     3
#define IOI_AUDIOCHAN   4
#define IOI_DRVBYTES    5
#define IOI_DEVSTAT     6
#define IOI_SECTSIZE    7
#define IOI_VOLSIZE     8
#define IOI_MEDIACHG    9
#define IOI_DISKINFO    10
#define IOI_TRACKINFO   11
#define IOI_QCHAN       12
#define IOI_SUBCHAN     13
#define IOI_UPC         14
#define IOI_AUDIOSTAT   15
/* IOCTL output */
#define IOO_EJECT       0
#define IOO_LOCK        1
#define IOO_RESET       2
#define IOO_AUDIOCHAN   3
#define IOO_CTLSTRING   4
#define IOO_CLOSETRAY   5

/* device status (IOCTL input 6) */
#define DS_DOOROPEN     0x0001
#define DS_UNLOCKED     0x0002
#define DS_RAW          0x0004
#define DS_WRITE        0x0008
#define DS_AUDIO        0x0010
#define DS_INTERLEAVE   0x0020
#define DS_PREFETCH     0x0080
#define DS_CHANMANIP    0x0100
#define DS_REDBOOK      0x0200
#define DS_NODISC       0x0800
#define DS_RWSUB        0x1000

/* Red Book address as the driver interface packs it: frame | sec << 8 | min << 16 */
static inline uint32_t rb_pack(unsigned m, unsigned s, unsigned f) { return f | (s << 8) | (m << 16); }
static inline uint32_t rb_to_lba(uint32_t rb) { return ((rb >> 16) & 0xFF) * 4500 + ((rb >> 8) & 0xFF) * 75 + (rb & 0xFF) - 150; }
static inline uint32_t lba_to_rb(uint32_t lba) { uint32_t a = lba + 150; return rb_pack(a / 4500, (a / 75) % 60, a % 75); }

static inline uint32_t rd32le(const void *p) { const uint8_t *b = p; return b[0] | (b[1] << 8) | (b[2] << 16) | ((uint32_t)b[3] << 24); }
static inline void wr32le(void *p, uint32_t v) { uint8_t *b = p; b[0] = v; b[1] = v >> 8; b[2] = v >> 16; b[3] = v >> 24; }
static inline uint16_t rd16le(const void *p) { const uint8_t *b = p; return b[0] | (b[1] << 8); }
static inline void wr16le(void *p, uint16_t v) { uint8_t *b = p; b[0] = v; b[1] = v >> 8; }

/* the extensions' version (INT 2Fh AX=150Ch: BH major, BL minor) */
#define CDEX_VERSION    0x0215      /* 2.21 */

#endif
