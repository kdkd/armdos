/*
 * kabi.h - ARM-DOS kernel ABI: the structures shared by the boot code,
 * IO.SYS (resident drivers + SYSINIT), ARMDOS.SYS (the DOS proper) and
 * installable drivers (HIMEM.SYS).  Everything here follows MS-DOS 4.00's
 * layouts (the INC directory) with every seg:off far pointer kept at 4
 * bytes and holding a flat 32-bit address (ARCH.md 5, 14.4).
 *
 * Freestanding: do not include the SDK's dos.h here (it #defines interrupt).
 */
#ifndef KABI_H
#define KABI_H

#include <stdint.h>
#include <stddef.h>

#define PACKED __attribute__((packed))

/* ------------------------------------------------------------ the frame */

struct armregs {                /* built by the BIOS SVC/IRQ/abort stubs */
    uint32_t r0, r1, r2, r3, r4, r5, r6, r7, r8, r9, r10, r11, r12;
    uint32_t sp, lr, pc, cpsr, intno;
    uint32_t _ksp, _pad;        /* BIOS private: SVC sp to restore */
};
typedef void (*int_handler)(struct armregs *);

#define CPSR_C      0x20000000u
#define CPSR_Z      0x40000000u
#define CPSR_I      0x00000080u
#define CPSR_F      0x00000040u
#define CPSR_T      0x00000020u
#define MODE_MASK   0x1Fu
#define MODE_USR    0x10u
#define MODE_SVC    0x13u
#define MODE_SYS    0x1Fu

#define AX(f)  ((f)->r0 & 0xFFFFu)
#define AH(f)  (((f)->r0 >> 8) & 0xFFu)
#define AL(f)  ((f)->r0 & 0xFFu)
#define BX(f)  ((f)->r1 & 0xFFFFu)
#define BH(f)  (((f)->r1 >> 8) & 0xFFu)
#define BL(f)  ((f)->r1 & 0xFFu)
#define CX(f)  ((f)->r2 & 0xFFFFu)
#define CH(f)  (((f)->r2 >> 8) & 0xFFu)
#define CL(f)  ((f)->r2 & 0xFFu)
#define DX(f)  ((f)->r3 & 0xFFFFu)
#define DH(f)  (((f)->r3 >> 8) & 0xFFu)
#define DL(f)  ((f)->r3 & 0xFFu)

static inline void set_ax(struct armregs *f, uint32_t v) { f->r0 = v & 0xFFFF; }
static inline void set_al(struct armregs *f, uint32_t v) { f->r0 = (f->r0 & ~0xFFu) | (v & 0xFF); }
static inline void set_ah(struct armregs *f, uint32_t v) { f->r0 = (f->r0 & ~0xFF00u) | ((v & 0xFF) << 8); }
static inline void set_bx(struct armregs *f, uint32_t v) { f->r1 = v & 0xFFFF; }
static inline void set_cx(struct armregs *f, uint32_t v) { f->r2 = v & 0xFFFF; }
static inline void set_dx(struct armregs *f, uint32_t v) { f->r3 = v & 0xFFFF; }
static inline void set_dl(struct armregs *f, uint32_t v) { f->r3 = (f->r3 & ~0xFFu) | (v & 0xFF); }
static inline void set_dh(struct armregs *f, uint32_t v) { f->r3 = (f->r3 & ~0xFF00u) | ((v & 0xFF) << 8); }
static inline void set_cf(struct armregs *f, int on) { if (on) f->cpsr |= CPSR_C; else f->cpsr &= ~CPSR_C; }
static inline void set_zf(struct armregs *f, int on) { if (on) f->cpsr |= CPSR_Z; else f->cpsr &= ~CPSR_Z; }

#define IVT ((volatile uint32_t *)0)
#define BDA8(o)  (*(volatile uint8_t  *)(0x400 + (o)))
#define BDA16(o) (*(volatile uint16_t *)(0x400 + (o)))
#define BDA32(o) (*(volatile uint32_t *)(0x400 + (o)))

#define IOPORT(p) (*(volatile uint8_t *)(0x10000000u + (p)))

/* ------------------------------------------------ device drivers (14.4) */

struct reqhdr;
struct devhdr {
    struct devhdr *next;        /* (struct devhdr *)-1 = end of chain */
    uint16_t attr;
    uint16_t pad;
    void (*strategy)(struct reqhdr *);
    void (*interrupt)(void);
    char     name[8];           /* block devices: name[0] = unit count */
};
#define DEV_END         ((struct devhdr *)0xFFFFFFFFu)

#define DEVA_CHAR       0x8000
#define DEVA_IOCTL      0x4000
#define DEVA_NONIBM     0x2000  /* block: media by FAT id; char: output until busy */
#define DEVA_OPENCLOSE  0x0800  /* open/close/removable media */
#define DEVA_GENIOCTL   0x0040  /* generic IOCTL + get/set logical drive */
#define DEVA_SPECIAL    0x0010  /* char: INT 29h fast output */
#define DEVA_CLOCK      0x0008
#define DEVA_NUL        0x0004
#define DEVA_STDOUT     0x0002
#define DEVA_STDIN      0x0001
#define DEVA_32BIT      0x0002  /* block: 32-bit sector numbers (4.0 EXTDRVR) */

/* request header (DOS layout, 13 bytes) */
struct reqhdr {
    uint8_t  len;
    uint8_t  unit;
    uint8_t  cmd;
    uint16_t status;
    uint8_t  reserved[8];
} PACKED;

#define RS_ERROR        0x8000
#define RS_BUSY         0x0200
#define RS_DONE         0x0100

/* driver error codes (INT 24h DI) */
#define DE_WRPROT       0x00
#define DE_UNIT         0x01
#define DE_NOTREADY     0x02
#define DE_BADCMD       0x03
#define DE_CRC          0x04
#define DE_BADLEN       0x05
#define DE_SEEK         0x06
#define DE_MEDIA        0x07
#define DE_NOTFOUND     0x08
#define DE_PAPER        0x09
#define DE_WRITE        0x0A
#define DE_READ         0x0B
#define DE_GENERAL      0x0C
#define DE_DISKCHANGE   0x0F

#define CMD_INIT        0
#define CMD_MEDIA       1
#define CMD_BPB         2
#define CMD_IOCTL_IN    3
#define CMD_READ        4
#define CMD_NDREAD      5
#define CMD_INSTAT      6
#define CMD_INFLUSH     7
#define CMD_WRITE       8
#define CMD_WRITEV      9
#define CMD_OUTSTAT     10
#define CMD_OUTFLUSH    11
#define CMD_IOCTL_OUT   12
#define CMD_OPEN        13
#define CMD_CLOSE       14
#define CMD_REMOVABLE   15
#define CMD_OUTBUSY     16
#define CMD_GENIOCTL    19
#define CMD_GETLOG      23
#define CMD_SETLOG      24

struct req_init {               /* cmd 0 */
    struct reqhdr h;
    uint8_t  units;             /* +0D out: block units */
    uint32_t brk;               /* +0E in: memory limit; out: break address */
    uint32_t arg;               /* +12 in: command line (after DEVICE=); out: BPB array */
    uint8_t  drive;             /* +16 in: first drive number (0 = A) */
    uint16_t cfgerr;            /* +17 out: nonzero -> "Error in CONFIG.SYS line n" */
} PACKED;

struct req_media {              /* cmd 1 */
    struct reqhdr h;
    uint8_t  media;             /* +0D */
    int8_t   changed;           /* +0E out: 1 no, 0 don't know, -1 yes */
    uint32_t volid;             /* +0F out: previous volume label */
} PACKED;

struct req_bpb {                /* cmd 2 */
    struct reqhdr h;
    uint8_t  media;             /* +0D */
    uint32_t buf;               /* +0E sector buffer (first FAT sector) */
    uint32_t bpb;               /* +12 out: BPB pointer */
} PACKED;

struct req_rw {                 /* cmd 3 4 8 9 12 */
    struct reqhdr h;
    uint8_t  media;             /* +0D */
    uint32_t addr;              /* +0E transfer address */
    uint16_t count;             /* +12 sectors / bytes; out: done */
    uint16_t start;             /* +14 start sector (FFFFh: use start32) */
    uint32_t volid;             /* +16 out on error 0Fh: volume label wanted */
    uint32_t start32;           /* +1A 32-bit start sector (4.0) */
} PACKED;

struct req_ndread {             /* cmd 5 */
    struct reqhdr h;
    uint8_t  ch;                /* +0D */
} PACKED;

struct req_gioctl {             /* cmd 19 */
    struct reqhdr h;
    uint8_t  category;          /* +0D */
    uint8_t  minor;             /* +0E */
    uint16_t si;                /* +0F */
    uint16_t di;                /* +11 */
    uint32_t data;              /* +13 */
} PACKED;

/* BIOS parameter block (boot sector 0x0B..0x23) */
struct bpb {
    uint16_t bytes_per_sec;
    uint8_t  sec_per_clus;
    uint16_t reserved;
    uint8_t  nfats;
    uint16_t root_ents;
    uint16_t total16;
    uint8_t  media;
    uint16_t fat_secs;
    uint16_t sec_per_track;
    uint16_t heads;
    uint32_t hidden;
    uint32_t total32;
} PACKED;

/* ------------------------------------------------------ DOS structures */

struct dpb {                    /* INC/DPB.INC, 4.0 (21h bytes) */
    uint8_t  drive;             /* 00 */
    uint8_t  unit;              /* 01 */
    uint16_t sector_size;       /* 02 */
    uint8_t  cluster_mask;      /* 04 sectors/cluster - 1 */
    uint8_t  cluster_shift;     /* 05 */
    uint16_t first_fat;         /* 06 */
    uint8_t  nfats;             /* 08 */
    uint16_t root_ents;         /* 09 */
    uint16_t first_data;        /* 0B */
    uint16_t max_cluster;       /* 0D clusters + 1 */
    uint16_t fat_size;          /* 0F sectors per FAT (word in 4.0) */
    uint16_t dir_sector;        /* 11 first root directory sector */
    struct devhdr *driver;      /* 13 */
    uint8_t  media;             /* 17 */
    uint8_t  first_access;      /* 18 FFh = must be (re)built */
    struct dpb *next;           /* 19 (struct dpb *)-1 = last */
    uint16_t next_free;         /* 1D last allocated cluster */
    uint16_t free_count;        /* 1F FFFFh = unknown */
} PACKED;

struct sft {                    /* INC/SF.INC, 3Bh bytes */
    uint16_t ref_count;         /* 00 */
    uint16_t mode;              /* 02 open mode; bit 15 = FCB */
    uint8_t  attr;              /* 04 */
    uint16_t flags;             /* 05 */
    uint32_t devptr;            /* 07 DPB (file) or device header */
    uint16_t first_cluster;     /* 0B */
    uint16_t time;              /* 0D */
    uint16_t date;              /* 0F */
    uint32_t size;              /* 11 */
    uint32_t position;          /* 15 */
    uint16_t rel_cluster;       /* 19 cluster number (relative) of last_cluster */
    uint32_t dir_sector;        /* 1B */
    uint8_t  dir_index;         /* 1F entry index in that sector */
    char     name[11];          /* 20 FCB-style name */
    uint32_t share_prev;        /* 2B */
    uint16_t machine;           /* 2F */
    uint16_t owner_psp;         /* 31 */
    uint16_t mft;               /* 33 */
    uint16_t last_cluster;      /* 35 absolute cluster for rel_cluster */
    uint32_t ifs;               /* 37 */
} PACKED;

/* sft.flags */
#define SF_DRIVE_MASK   0x003F  /* files: drive number */
#define SF_CLEAN        0x0040  /* files: not written since open */
#define SF_DEVICE       0x0080
#define SF_NODATE       0x4000  /* don't stamp the date on close */
#define SF_REMOTE       0x8000
/* devices: low bits mirror IOCTL device info */
#define DI_STDIN        0x0001
#define DI_STDOUT       0x0002
#define DI_NUL          0x0004
#define DI_CLOCK        0x0008
#define DI_SPECIAL      0x0010
#define DI_RAW          0x0020
#define DI_NOEOF        0x0040  /* clear = at EOF on input */

struct sftblock {
    struct sftblock *next;      /* (struct sftblock *)-1 = last */
    uint16_t count;
    struct sft e[];
} PACKED;

struct cds {                    /* INC/CURDIR.INC, 58h bytes */
    char     path[67];          /* 00 "C:\DOS" */
    uint16_t flags;             /* 43 */
    struct dpb *dpb;            /* 45 */
    uint16_t cluster;           /* 49 0 = root, FFFFh = must re-walk */
    uint16_t net1, net2;        /* 4B, 4D */
    uint16_t bsoffset;          /* 4F root backslash offset (2) */
    uint8_t  type;              /* 51 */
    uint32_t ifs;               /* 52 */
    uint16_t fsda;              /* 56 */
} PACKED;
#define CDS_NET         0x8000
#define CDS_VALID       0x4000
#define CDS_JOIN        0x2000
#define CDS_SUBST       0x1000

struct psp {                    /* ARCH.md 9 */
    uint16_t int20;             /* 00 Thumb svc #0x20 */
    uint16_t memtop;            /* 02 */
    uint8_t  res04;             /* 04 */
    uint8_t  cpm[5];            /* 05 */
    uint32_t int22;             /* 0A */
    uint32_t int23;             /* 0E */
    uint32_t int24;             /* 12 */
    uint16_t parent;            /* 16 */
    uint8_t  jft[20];           /* 18 */
    uint16_t envseg;            /* 2C */
    uint32_t savedsp;           /* 2E sp at the last INT 21h */
    uint16_t jftsize;           /* 32 */
    uint32_t jftptr;            /* 34 */
    uint32_t prevpsp;           /* 38 */
    uint8_t  intercon;          /* 3C */
    uint8_t  append;            /* 3D */
    uint8_t  res3e[2];          /* 3E */
    uint16_t dosver;            /* 40 */
    uint8_t  res42[2];          /* 42 */
    uint32_t execframe;         /* 44 ARM-DOS: parent's saved EXEC frame (kernel private) */
    uint8_t  res48[8];          /* 48 */
    uint32_t call21[2];         /* 50 svc #0x21 ; bx lr */
    uint8_t  res58[4];          /* 58 */
    uint8_t  fcb1[16];          /* 5C */
    uint8_t  fcb2[20];          /* 6C */
    uint8_t  tail[128];         /* 80 */
} PACKED;

struct mcb {                    /* ARCH.md 10 */
    char     sig;               /* 'M' / 'Z' */
    uint16_t owner;             /* 0 free, 8 DOS */
    uint16_t size;              /* paragraphs */
    uint8_t  res[3];
    char     name[8];
} PACKED;
#define MCB_OWNER_DOS   8

struct dirent {                 /* INC/DIRENT.INC */
    char     name[11];
    uint8_t  attr;
    uint16_t codepage;          /* 0C */
    uint16_t ea;                /* 0E */
    uint8_t  attr2;             /* 10 */
    uint8_t  res11[5];
    uint16_t time;              /* 16 */
    uint16_t date;              /* 18 */
    uint16_t cluster;           /* 1A */
    uint32_t size;              /* 1C */
} PACKED;

#define ATTR_RDONLY     0x01
#define ATTR_HIDDEN     0x02
#define ATTR_SYSTEM     0x04
#define ATTR_VOLUME     0x08
#define ATTR_DIR        0x10
#define ATTR_ARCHIVE    0x20

struct find_dta {               /* INC/FIND.INC, 43 bytes */
    uint8_t  drive;             /* 00 1 = A:; the kernel keeps the drive here */
    char     pattern[11];       /* 01 */
    uint8_t  sattr;             /* 0C */
    uint16_t index;             /* 0D next entry to look at */
    uint16_t dircluster;        /* 0F 0 = root */
    uint8_t  res11[4];          /* 11 */
    uint8_t  attr;              /* 15 */
    uint16_t time;              /* 16 */
    uint16_t date;              /* 18 */
    uint32_t size;              /* 1A */
    char     name[13];          /* 1E */
} PACKED;

struct fcb {                    /* INC/CPMFCB.INC */
    uint8_t  drive;             /* 00 */
    char     name[11];          /* 01 */
    uint16_t cur_block;         /* 0C */
    uint16_t rec_size;          /* 0E */
    uint32_t size;              /* 10 */
    uint16_t date;              /* 14 */
    uint16_t time;              /* 16 */
    uint8_t  sfn;               /* 18 reserved (kernel: 0x80 | drive-ish flags) */
    uint8_t  dir_index;         /* 19 */
    uint32_t dir_sector;        /* 1A */
    uint16_t first_cluster;     /* 1E */
    uint8_t  cur_rec;           /* 20 */
    uint32_t rand_rec;          /* 21 */
} PACKED;

/* list of lists (INT 21h AH=52h), DOS 4 layout from offset -0Ch */
struct lol {
    uint16_t share_retry;       /* -0C */
    uint16_t share_delay;       /* -0A */
    uint32_t last_buffer;       /* -08 */
    uint16_t contpos;           /* -04 */
    uint16_t first_mcb;         /* -02 */
    struct dpb *dpb_head;       /* 00 */
    struct sftblock *sft_head;  /* 04 */
    struct devhdr *clock;       /* 08 */
    struct devhdr *con;         /* 0C */
    uint16_t max_sector;        /* 10 */
    uint32_t buffer_info;       /* 12 */
    struct cds *cds;            /* 16 */
    struct sftblock *fcb_sft;   /* 1A */
    uint16_t fcb_keep;          /* 1E */
    uint8_t  nblock;            /* 20 */
    uint8_t  lastdrive;         /* 21 */
    uint16_t nulpad;            /* 22 ARM-DOS: so that the NUL header is word aligned */
    struct devhdr nul;          /* 24 NUL device header (24 bytes in ARM-DOS, 18 in DOS) */
    uint8_t  joins;             /* 3C (DOS 4: 34) */
    uint16_t special;           /* 3D */
    uint32_t ifs_entry;         /* 3F */
    uint32_t ifs_head;          /* 43 */
    uint16_t buffers;           /* 47 (DOS 4: 3F) */
    uint16_t lookahead;         /* 49 */
    uint8_t  bootdrive;         /* 4B 1 = A: (DOS 4: 43) */
    uint8_t  dword_moves;       /* 4C */
    uint16_t extmem_kb;         /* 4D */
} PACKED;
#define LOL_BASE_OFFSET 0x0C    /* the LoL pointer is &lol + 0x0C */

/* DEVMARK sub-arena header (BIOS/DEVMARK.INC) */
struct devmark {
    char     id;                /* 'D' DEVICE= 'F' FILES 'X' FCBS 'B' BUFFERS 'L' LASTDRIVE 'S' STACKS 'I' IFS 'T' INSTALL */
    uint16_t seg;               /* this header's segment + 1 */
    uint16_t size;              /* paragraphs */
    uint8_t  res[3];
    char     name[8];
} PACKED;

/* ------------------------------------------- IO.SYS <-> ARMDOS.SYS init */

#define DOSINIT_MAGIC   0x534F4441u     /* "ADOS" */

struct dosconfig {              /* what SYSINIT learned from CONFIG.SYS */
    uint16_t files, fcbs, fcbs_keep, buffers, lookahead;
    uint8_t  lastdrive;         /* number of drives (E: = 5) */
    uint8_t  brk;               /* BREAK=ON */
    uint8_t  switches_k;
    uint8_t  pad;
};

struct dosapi;
struct dosinit {                /* passed to the ARMDOS.SYS entry point */
    uint32_t magic;
    struct devhdr *devchain;    /* IO.SYS resident chain (CON AUX PRN CLOCK$ block COMx LPTx) */
    struct devhdr *con, *clock, *block;
    uint8_t  nunits;            /* block units the resident driver has */
    uint8_t  bootdrive;         /* 0 = A: */
    uint16_t pad;
    struct bpb **bpbs;          /* from the block driver's INIT */
    uint32_t io_start, io_end;  /* IO.SYS resident range (for MEM/fault checks) */
    uint32_t dos_start, dos_end;/* where ARMDOS.SYS was placed (end = image+bss) */
    uint32_t sysinit_base;      /* the arena ends here until SYSINIT is done */
    const struct dosapi *api;   /* out */
};

struct dosapi {                 /* services ARMDOS.SYS gives SYSINIT */
    uint32_t magic;
    struct lol *lol;            /* the real structure (LoL pointer = (char *)lol + 0x0C) */
    /* install a character device (after NUL, so it shadows older ones) */
    void (*add_chardev)(struct devhdr *d);
    /* install a block device's units; returns the first drive number or -1 */
    int  (*add_blockdev)(struct devhdr *d, int units, struct bpb **bpbs, int *toomany);
    /* build FILES/FCBS/BUFFERS/LASTDRIVE tables at addr (with DEVMARK headers); returns end */
    uint32_t (*build_tables)(const struct dosconfig *c, uint32_t addr);
    /* create the arena: a system block (owner 8) from first_seg, sysparas long,
       then free memory up to end_seg */
    void (*make_arena)(uint16_t first_seg, uint16_t sysparas, uint16_t end_seg);
    void (*extend_arena)(uint16_t end_seg);
    /* the shell, for restarting it if the root process ends without an INT 22h */
    void (*set_shell)(const char *path, const char *tail);
    void (*set_break)(int on);
    struct nls_state *nls;      /* the national language tables (COUNTRY=) */
};

/* National language support: DOS 4's COUNTRY_CDPG - the
   country and code pages in use, the COUNTRY.SYS path for NLSFUNC, and the
   tables INT 21h AH=38h/65h hand out. The built-in values are country 001,
   code page 437 (DOS/DOSMES.ASM). SYSINIT fills it from COUNTRY.SYS for
   COUNTRY=; NLSFUNC (INT 2Fh AH=14h, called with DI = r5 = this structure)
   refills it for CHCP / INT 21h AX=6602h and AH=38h "set country". */
struct nls_state {
    uint32_t magic;             /* NLS_MAGIC */
    char     path[64];          /* COUNTRY.SYS as COUNTRY= gave it, default "\COUNTRY.SYS" */
    uint16_t country;           /* ccDosCountry */
    uint16_t cp;                /* ccDosCodePage: the global code page (CHCP) */
    uint16_t syscp;             /* ccSysCodePage: COUNTRY='s (or 437) */
    uint16_t pad;
    uint8_t  info[34];          /* the AH=38h block (+12h, the case-map routine, is DOS's) */
    uint8_t  ucase[130];        /* 80h 00h + upper case of 80h-FFh (also the file-name table) */
    uint8_t  fchar[24];         /* 16h 00h + the file-name character table */
    uint8_t  collate[258];      /* 00h 01h + the collating sequence */
};
#define NLS_MAGIC 0x31534C4Eu   /* "NLS1" */

#endif
