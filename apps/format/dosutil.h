/*
 * dosutil.h - the small runtime shared by the ARM-DOS disk utilities
 * (FORMAT, SYS, CHKDSK, DISKCOPY, DISKCOMP).  It stands in for what the
 * MS-DOS 4.00 utilities got from the common message retriever and parser
 * (INC/MSGSERV.ASM, INC/PARSE.ASM): output through DOS handles, the
 * 10-column right-aligned numbers, "%1-%2" serial numbers, the parser's
 * error messages, Y/N via INT 21h AX=6523h, and the raw disk calls
 * (INT 25h/26h packets, IOCTL 440Dh generic block requests).
 *
 * Every program includes this and links apps/format/dosutil.c (the other
 * apps compile it through a one-line wrapper).  No stdio: every byte of a
 * utility is conventional memory.
 */
#ifndef DOSUTIL_H
#define DOSUTIL_H

#include <stdint.h>
#include <string.h>
#include "armdos.h"

typedef struct armregs R;

#define CF(r) (((r)->cpsr & ARM_CPSR_C) != 0)

int  dos(R *r);                         /* INT 21h; returns CF */
int  intr(int n, R *r);                 /* INT n */

/* ---- output (DOS handles, so redirection works) ------------------------- */
#define STDOUT 1
#define STDERR 2
void wrn(int h, const char *s, unsigned n);
void wr(int h, const char *s);
void outs(const char *s);               /* stdout */
void errs(const char *s);               /* stderr */
void crlf(void);                        /* CR LF on stdout */
/* v right-aligned in `width` columns padded with `pad`; returns the end (NUL written) */
char *fmtnum(char *p, uint32_t v, int width, char pad);
char *fmthex4(char *p, unsigned v);
/* "%10lu" + text + CR LF on stdout (the retriever's standard byte counts) */
void outnum(uint32_t v, const char *text);
/* text with %1..%3 replaced by the given strings */
void outfmt(int h, const char *text, const char *a1, const char *a2, const char *a3);
void serial_line(uint32_t serial);      /* "Volume Serial Number is XXXX-XXXX" CR LF */

/* ---- input -------------------------------------------------------------- */
/* INT 21h AX=0C0Ah: flush, then buffered line input (echoes; Enter echoes CR).
   buf[0] = max; returns the length typed (text at buf+2). */
int  getline_flush(uint8_t *buf, int max);
int  getkey_flush(void);                /* AX=0C08h: flush, read a key, no echo */
int  getche_flush(void);                /* AX=0C01h: flush, read a key with echo */
int  getkey(void);                      /* AH=08h */
int  yesno(int c);                      /* AX=6523h: 1 yes, 0 no, -1 neither */

/* ---- misc DOS ----------------------------------------------------------- */
void check_version(void);               /* "Incorrect DOS version" unless 4.00 */
void dos_exit(int code) __attribute__((noreturn));
int  cur_drive(void);                   /* 0 = A: */
int  boot_drive(void);                  /* 0 = A: (AX=3305h) */
void get_date(unsigned *year, unsigned *mon, unsigned *day);
void get_time(unsigned *h, unsigned *m, unsigned *s, unsigned *hs);
uint16_t dos_date(void);                /* directory-entry format */
uint16_t dos_time(void);
void disk_reset(void);                  /* AH=0Dh */
/* INT 21h AH=32h: the drive's DPB (media-checked), or 0 */
struct kdpb {
    uint8_t  drive, unit;
    uint16_t sector_size;
    uint8_t  cluster_mask, cluster_shift;
    uint16_t first_fat;
    uint8_t  nfats;
    uint16_t root_ents, first_data, max_cluster, fat_size, dir_sector;
    uint32_t driver;
    uint8_t  media, first_access;
    uint32_t next;
    uint16_t next_free, free_count;
} __attribute__((packed));
struct kdpb *get_dpb(int drive);
void forget_free(int drive, int next_free);   /* DPB free count = unknown */

/* ---- drives ------------------------------------------------------------- */
int  drive_valid(int drive);            /* via AH=0Eh/19h-free check (4409h) */
int  ioctl_removable(int drive);        /* 4408h: 1 removable, 0 fixed, -1 error */
int  ioctl_remote(int drive, unsigned *attr);   /* 4409h: 0 ok, -1 error; attr = DX */
int  truename_letter(int drive);        /* AH=60h on "d:\": the drive letter it maps to */
int  get_logical(int drive);            /* 440Eh: 0 = only letter, else owner (1 = A:) */
void set_logical(int drive);            /* 440Fh */
/* generic IOCTL for block devices, category 08h: returns 0 or the DOS error */
int  gen_ioctl(int drive, int minor, void *packet);

/* device parameters as the DOS 4 A_DEVICEPARAMETERS (INC/IOCTL.INC) */
struct bpb {
    uint16_t bps;
    uint8_t  spc;
    uint16_t reserved;
    uint8_t  nfats;
    uint16_t root;
    uint16_t total16;
    uint8_t  media;
    uint16_t spf;
    uint16_t spt;
    uint16_t heads;
    uint32_t hidden;
    uint32_t total32;
} __attribute__((packed));
struct devparams {
    uint8_t  special;
    uint8_t  devtype;
    uint16_t attr;
    uint16_t cylinders;
    uint8_t  mediatype;
    struct bpb bpb;
    uint8_t  res[6];
    uint16_t tracks;                    /* track layout: count, then (number, size) */
    uint16_t layout[2 * 63];
} __attribute__((packed));

/* the physical layout the block driver uses for track requests */
struct geom { unsigned cyls, heads, spt; uint32_t total; };
int  drive_geometry(int drive, struct geom *g);
/* sectors through IOCTL 61h/41h (bypasses DOS; works on unformatted media).
   lba is relative to the start of the drive (the partition for C:). */
int  trk_rw(int drive, int write, const struct geom *g, uint32_t lba, unsigned count, void *buf);
int  trk_format(int drive, unsigned cyl, unsigned head, int verify_only);
/* INT 25h/26h, DOS 4 packet form; returns 0 or AX (error) */
int  abs_rw(int drive, int write, uint32_t sector, unsigned count, void *buf);

/* ---- parsing (the common parser's user-visible behaviour) ----------------- */
struct arg {
    char  text[80];                     /* as typed (switch includes the '/') */
    char  shown[82];                    /* what an error message shows */
    char *val;                          /* switch value after ':' or '=', or 0 */
    uint8_t sw;                         /* 1 = a switch */
};
int  parse_tail(struct arg *a, int max);   /* from PSP+80h; returns the count */
/* parse-class error: text + " - " + the offending parameter, CR LF, on STDERR */
void parse_err(const char *msg, const char *shown);
void upcase(char *s);
int  is_drive_spec(const char *s);      /* "X:" alone */

/* message texts shared by several utilities (USA-MS.MSG) */
extern const char M_INVSW[], M_TOOMANY[], M_REQMISS[], M_BADFMT[], M_INVPARM[],
                  M_INVDRIVE[], M_PRESSKEY[];

#endif
