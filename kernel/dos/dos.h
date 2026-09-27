/* dos.h - ARMDOS.SYS internals */
#ifndef DOS_H
#define DOS_H

#include "klib.h"

/* ------------------------------------------------------------ errors */
#define E_INVFN         0x01
#define E_NOFILE        0x02
#define E_NOPATH        0x03
#define E_TOOMANY       0x04
#define E_ACCESS        0x05
#define E_BADHANDLE     0x06
#define E_ARENA         0x07
#define E_NOMEM         0x08
#define E_BADBLOCK      0x09
#define E_BADENV        0x0A
#define E_BADFMT        0x0B
#define E_BADACCESS     0x0C
#define E_BADDATA       0x0D
#define E_BADDRIVE      0x0F
#define E_CURDIR        0x10
#define E_NOTSAME       0x11
#define E_NOMORE        0x12
#define E_WRPROT        0x13
#define E_NOTREADY      0x15
#define E_GENFAIL       0x1F
#define E_SHARE         0x20
#define E_LOCK          0x21
#define E_WRONGDISK     0x22
#define E_FCBUNAVAIL    0x23
#define E_EOF           0x26
#define E_DISKFULL      0x27
#define E_NOTSUP        0x32    /* network request not supported */
#define E_EXISTS        0x50
#define E_CANTMAKE      0x52
#define E_FAIL24        0x53
#define E_BADPARAM      0x57

/* ------------------------------------------------------ the kernel */

#define MAX_DRIVES      26
#define SECSIZE         512

struct buf {
    struct buf *next, *prev;    /* LRU list, most recent first */
    uint8_t  drive;             /* 0xFF = free */
    uint8_t  flags;
    uint16_t pad;
    uint32_t sector;
    struct dpb *dpb;
    uint32_t pad2;
    uint8_t  data[SECSIZE];
};
#define BF_DIRTY    0x40
#define BF_FAT      0x02
#define BF_DIR      0x04
#define BF_DATA     0x08

/* where an I/O is on the disk (INT 24h AH bits 1-2) */
#define AREA_DOS    0
#define AREA_FAT    1
#define AREA_DIR    2
#define AREA_DATA   3

struct jmpbuf { uint32_t r[10]; };
int  k_setjmp(struct jmpbuf *jb) __attribute__((returns_twice));
void k_longjmp(struct jmpbuf *jb, int v) __attribute__((noreturn));

/* one INT 21h being served */
struct ctx {
    struct ctx *prev;
    struct armregs *f;
    struct armregs orig;        /* registers as the caller passed them */
    struct jmpbuf jb;
    uint8_t base;               /* the caller was a program (not a nested call) */
    uint8_t fn;
    uint8_t checked[MAX_DRIVES];/* media checked during this call */
};

/* data shared with programs (InDOS etc.) */
struct dosvars {
    uint8_t  errormode;         /* INT 24h in progress; InDOS - 1 as in DOS */
    uint8_t  indos;
};

/* the LoL (packed, word aligned: its NUL device header at +24h must be) */
extern struct lol LOL;
extern struct dosvars DV;
extern struct nls_state nls;         /* misc.c: country, code pages, NLS tables */
extern struct ctx *cur_ctx;
extern uint16_t cur_psp;
extern uint32_t cur_dta;
extern uint8_t cur_drive;
extern uint8_t break_on, verify_on, alloc_strategy, switchar;
extern uint16_t exit_code;
extern struct exterr { uint16_t code; uint8_t class_, action, locus; uint32_t volptr; } exterr;
extern struct cds *cds_tab;
extern int n_cds;
extern struct dosinit *dinit;
extern uint32_t dos_image_start, dos_image_end;
extern uint16_t init_psp_seg;
extern uint8_t fail_err;        /* the user answered FAIL to an INT 24h */
extern uint8_t no_i24;          /* extended open bit 13: fail instead of INT 24h */
extern uint8_t con_col;         /* CARPOS */
extern uint8_t printer_echo;

#define PSP(seg)  ((struct psp *)((uint32_t)(seg) << 4))
#define MCB(seg)  ((struct mcb *)((uint32_t)(seg) << 4))
#define SEG(p)    ((uint16_t)((uint32_t)(p) >> 4))

/* int21.c */
void int21_handler(struct armregs *f);
void sys_err(struct armregs *f, int err);
void sys_ok(struct armregs *f);
void fcb_err(int err);
void set_exterr(int err, int class_, int action, int locus);
void dos_idle(void);
__attribute__((noreturn)) void abort_to_base(void);
struct ctx *base_ctx(void);

/* char.c: console and character devices */
void con_out(int c);                            /* OUTT: to stdout, with ^C/^S/^P checks */
void con_puts(const char *s);
void con_crlf(void);
int  con_in_noecho(void);                       /* AH=08 semantics (^C checked) */
int  con_in_raw(void);                          /* AH=07 */
void con_string_input(uint8_t *buf);            /* AH=0Ah */
void char_functions(struct armregs *f);         /* AH=01..0C */
void check_ctrl_c(void);                        /* BREAK=ON check / DSKSTATCHK */
void stat_check(void);                          /* STATCHK: ^S ^P ^C on stdin */
__attribute__((noreturn)) void ctrl_c_abort(void);
int  dev_read(struct sft *s, uint8_t *buf, unsigned n);
int  dev_write(struct sft *s, const uint8_t *buf, unsigned n);
void bcon_write(const char *s, int n);          /* straight to the CON device (messages) */

/* dev.c: device calls, INT 24h */
int  devcall(struct devhdr *d, void *req);
int  crit_error(int ah, int drive, int code, struct devhdr *dev);   /* -> 0 ignore 1 retry 3 fail */
int  dsk_io(struct dpb *dpb, int write, uint32_t sector, unsigned count, void *buf, int area);
int  media_check(int drive);                    /* 0 ok, -err */
struct dpb *drive_dpb(int drive, int *err);     /* DPB ready for use (media checked) */
struct devhdr *find_chardev(const char *name8);
struct devhdr *con_device(void);
void label_to_boot(struct dpb *d, const char *name11);

/* buf.c */
void buf_init(struct buf *pool, int n);
struct buf *getbuf(struct dpb *dpb, uint32_t sector, int area, int noread);
void buf_dirty(struct buf *b);
int  flush_bufs(int drive);                     /* -1 = all */
void invalidate_bufs(int drive);
int  bufs_dirty(int drive);
void buf_sync_range(struct dpb *dpb, uint32_t sector, unsigned count, int invalidate);
void buf_add_pool(struct buf *pool, int n);

/* fat.c */
int  dpb_fat16(const struct dpb *d);
uint32_t fat_get(struct dpb *d, unsigned cl);   /* next cluster, or 0xFFFFFFFF on error */
int  fat_set(struct dpb *d, unsigned cl, unsigned v);
int  fat_eof(const struct dpb *d, unsigned v);
unsigned fat_eofval(const struct dpb *d);
int  fat_alloc(struct dpb *d, unsigned prev);   /* new cluster or 0 (disk full) or -err */
int  fat_free_chain(struct dpb *d, unsigned cl);
int  fat_count_free(struct dpb *d);
uint32_t clus2sec(const struct dpb *d, unsigned cl);
int  fat_walk(struct dpb *d, unsigned start, unsigned n, unsigned *out);

/* name.c */
int  canon_path(const char *in, char *out, int *drive, int allow_wild);   /* TRUENAME */
int  cds_rootlen(const struct cds *c);
int  drive_usable(int drive);
extern char canon_prejoin[84];
extern int canon_ldrive;
int  name_to_fcb(const char *s, char *name11);  /* one component, 8.3 */
void fcb_to_name(const char *name11, char *out);/* "NAME.EXT" */
int  name_match(const char *pat11, const char *name11);
int  has_wild(const char *name11);
int  valid_fchar(int c);
uint8_t dos_upcase(uint8_t c);
int  parse_fcb_name(struct armregs *f);         /* AH=29h */

/* dir.c */
struct dirloc { uint32_t sector; uint8_t index; uint16_t entry; uint16_t dircl; };
struct pathinfo {
    int drive;
    struct dpb *dpb;
    char full[80];              /* canonical path */
    char name11[11];            /* last component */
    uint16_t dircl;             /* containing directory (0 = root) */
    struct devhdr *dev;         /* a character device */
    int wild;
    int isroot;                 /* the path names a root directory */
    int remote;                 /* a redirected drive (redir.c): no DPB, no dircl */
};
int  resolve(const char *path, struct pathinfo *pi, int allow_wild);
struct dirent *dir_get(struct dpb *d, uint16_t dircl, unsigned index, struct buf **pb, struct dirloc *loc, int area);
int  dir_find(struct dpb *d, uint16_t dircl, const char *name11, int sattr, unsigned start, struct dirent *out, struct dirloc *loc, int create);
int  dir_new_entry(struct dpb *d, uint16_t dircl, struct dirloc *loc);
int  dir_write(struct dpb *d, const struct dirloc *loc, const struct dirent *de);
int  dir_lookup_path(struct pathinfo *pi, struct dirent *de, struct dirloc *loc, int sattr);
int  dir_walk_to(struct dpb *d, const char *full, uint16_t *cl);
int  attr_match(int fattr, int sattr);
void dos_now(uint16_t *date, uint16_t *time);
/* stamp a packed structure's date/time fields (never pass &packed->field around:
   an unaligned halfword store on ARMv5 writes the wrong bytes) */
#define STAMP_NOW(p) do { uint16_t _d, _t; dos_now(&_d, &_t); (p)->date = _d; (p)->time = _t; } while (0)
int  get_volume_label(struct dpb *d, char *out11);

/* file.c */
struct sft *sft_get(unsigned idx);
int  sft_count(void);
int  sft_alloc(unsigned *idx);
struct sft *handle_sft(unsigned h, unsigned *sfn);
int  jft_alloc(void);
uint8_t *jft_ptr(struct psp *p, unsigned *size);
int  file_read(struct sft *s, uint8_t *buf, unsigned n);
int  file_write(struct sft *s, const uint8_t *buf, unsigned n);
int  file_close_sft(struct sft *s);
int  file_commit(struct sft *s);
int  open_path(const char *path, int mode, int *ph);
int  create_path(const char *path, int attr, int mode, int newonly, int *ph);
void close_process_files(struct psp *p);
void file_functions(struct armregs *f);
int  file_open_sft(const char *path, int mode, struct sft *s, int create, int attr, int newonly);
uint32_t sft_seek(struct sft *s, int whence, int32_t off);

/* redir.c: INT 2Fh AH=11h redirector callouts */
int  drive_remote(int drive);
int  redir_call(int fn, struct armregs *r);
int  redir_path(int fn, const struct pathinfo *pi, unsigned cx, struct armregs *r);
int  redir_open(const struct pathinfo *pi, struct sft *s, int mode, int create, int attr, int newonly);
int  redir_rw(int write, struct sft *s, const uint8_t *buf, unsigned n);
int  redir_sft(int fn, struct sft *s);
int  redir_space(int drive, unsigned *spc, unsigned *total, unsigned *bps, unsigned *avail);
int  redir_find(const struct pathinfo *pi, unsigned sattr, uint8_t *dta);
void redir_name11(const char *s, char *n11);

/* find.c */
void find_first(struct armregs *f);
void find_next(struct armregs *f);

/* fcb.c */
void fcb_functions(struct armregs *f);

/* mem.c */
int  mem_alloc(unsigned paras, unsigned *seg, unsigned *largest);
int  mem_free(unsigned seg);
int  mem_resize(unsigned seg, unsigned paras, unsigned *maxp);
int  mem_check(void);
void mem_free_owner(unsigned owner);
unsigned mem_largest(void);
void mem_functions(struct armregs *f);
void mem_make_arena(uint16_t first, uint16_t sysparas, uint16_t end);
void mem_extend(uint16_t end);
void mem_set_owner(unsigned seg, unsigned owner);

/* proc.c */
void exec_fn(struct armregs *f);
void terminate(struct armregs *f, int code, int type);
void proc_functions(struct armregs *f);
void fault_install(void);
void int20_handler(struct armregs *f);
void int27_handler(struct armregs *f);
void set_shell(const char *path, const char *tail);
void new_psp(uint16_t seg, uint16_t memtop, int copy_from_current);

/* ioctl.c */
void ioctl_fn(struct armregs *f);

/* misc.c */
void misc_functions(struct armregs *f);
void int25_handler(struct armregs *f);
void int26_handler(struct armregs *f);
void int28_handler(struct armregs *f);
void int2f_handler(struct armregs *f);
void int23_default(struct armregs *f);
void int24_default(struct armregs *f);
void int22_default(struct armregs *f);
int  clock_read(uint8_t *six);
int  clock_write(const uint8_t *six);
void dos_date(int *y, int *m, int *d, int *wd);
int  days_to_ymd(unsigned n, int *y, int *m, int *d);
unsigned ymd_to_days(int y, int m, int d);

/* init.c */
void build_dpb(struct dpb *d, const struct bpb *b);
struct cds *get_cds(int drive);

#endif
