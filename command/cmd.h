/*
 * cmd.h - COMMAND.COM for ARM-DOS 4.00: shared declarations.
 *
 * The structure follows MS-DOS 4.0's COMMAND (CMD/COMMAND, the .ASM files): a
 * "resident" state (echo flag, batch/FOR/pipe state, SINGLECOM, RETCODE, the
 * environment) and a "transient" command processor whose recycle point is
 * TCOMMAND.  Errors that COMMAND reports with CERROR print the message on
 * stderr and jump back to TCOMMAND (here: longjmp).
 */
#ifndef CMD_H
#define CMD_H

#include <stdint.h>
#include <string.h>
#include <armdos.h>

typedef struct armregs REGS;

/* setjmp/longjmp of our own (start.S): newlib's would pull in the unwinder */
typedef uint32_t cmd_jmp_buf[10];
int cmd_setjmp(cmd_jmp_buf b) __attribute__((returns_twice));
__attribute__((noreturn)) void cmd_longjmp(cmd_jmp_buf b, int v);
#define jmp_buf cmd_jmp_buf
#define setjmp cmd_setjmp
#define longjmp cmd_longjmp

#define COMBUFLEN   128
#define ARGMAX      64
#define ARGBLEN     (2 * 128)

/* --------------------------------------------------------------- dos.c -- */
int  int21(REGS *r);                     /* INT 21h; returns CF */
int  int21_raw(REGS *r);                 /* same, no ^C pending check (handlers) */
int  intr(int n, REGS *r);
unsigned dos_error(void);                /* INT 21h 59h: extended error */
int  dos_getdrv(void);                   /* 0 = A */
int  dos_setdrv(int d);
int  dos_curdir(int drive1, char *buf);  /* 47h: drive 1-based (0 = default); 0 ok, else error */
int  dos_chdir(const char *p);           /* 0 ok / error code */
int  dos_mkdir(const char *p);
int  dos_rmdir(const char *p);
int  dos_open(const char *p, int mode);  /* handle, or -error */
int  dos_creat(const char *p, int attr);
int  dos_close(int h);
int  dos_read(int h, void *buf, unsigned n);    /* bytes, or -error */
int  dos_write(int h, const void *buf, unsigned n);
long dos_lseek(int h, long off, int whence);    /* position, or -error (small) */
int  dos_unlink(const char *p);
int  dos_ioctl_info(int h);              /* device info word, or -1 */
int  dos_ioctl_set(int h, unsigned info);
int  dos_dup(int h);
int  dos_dup2(int h, int h2);
void dos_setdta(void *p);
int  dos_findfirst(const char *p, int attr);    /* 0 ok, else error code */
int  dos_findnext(void);
int  dos_fcb_parse(const char **s, uint8_t *fcb, int al);  /* returns AL */
int  dos_truename(const char *in, char *out);
unsigned dos_alloc(unsigned paras, unsigned *largest);     /* seg or 0 */
int  dos_free(unsigned seg);
int  dos_setblock(unsigned seg, unsigned paras, unsigned *max);
void dos_putc(int c);                    /* 02h */
int  dos_getc_echo_flush(void);          /* 0C01h */
int  dos_getc_noecho_flush(void);        /* 0C08h */
void dos_flush_kbd(void);                /* 0C00h */
void dos_bufinput(uint8_t *buf);         /* 0Ah */
void dos_bufinput_flush(uint8_t *buf);   /* 0C0Ah */
int  dos_yesno(int c);                   /* 6523h: 0 no, 1 yes, 2 neither */
int  dos_upcase(int c);                  /* file-name upper case */
void dos_getdate(int *y, int *m, int *d, int *wd);
void dos_gettime(int *h, int *m, int *s, int *hs);
int  dos_setdate(int y, int m, int d);   /* 0 ok */
int  dos_settime(int h, int m, int s, int hs);
int  dos_country(uint8_t *buf34);        /* date format, 0 ok */
uint16_t dos_psp(void);                  /* 62h */
void dos_setvect(int n, void *h);
void *dos_getvect(int n);

/* ------------------------------------------------------------ output.c -- */
void out(int h, const char *s);
void outn(int h, const char *s, unsigned n);
void outc(int h, int c);
void crlf(int h);
void crlf2(void);                        /* CR LF on stdout (CRLF2) */
char *fmt_uint(char *buf, unsigned long v, int width, int pad);  /* right aligned */
char *fmt_hex4(char *buf, unsigned v);
void fmt_date(char *buf, int y, int m, int d, int year4);   /* country order */
void fmt_time(char *buf, int h, int m, int s, int hs, int fields, int h12); /* fields 2..4 */
extern uint8_t country[34];
void load_country(void);

/* ---------------------------------------------------------- messages.c -- */
const char *msg(int n);                  /* text of message n (COMMAND.SKL numbering) */
const char *ext_msg(int err);            /* extended error text (class 1/E) */
const char *parse_msg(int n);            /* parse error text (class C) */
void msgout(int h, int n, const char *s1, const char *s2, const char *s3);
void std_printf(int n);                  /* msg n on stdout, no substitution */
void std_eprintf(int n);
void printf_crlf(int h, const char *s);
void ext_error_out(int err, const char *sub);    /* stderr, " - sub", CR LF */
void parse_error_out(int n, const char *sub);
/* message numbers used by name */
enum {
    M_RENERR = 1002, M_BADCPMES = 1003, M_NOSPACE = 1004, M_ENVERR = 1007,
    M_FULDIR = 1008, M_BADBAT = 1009, M_NEEDBAT = 1010, M_BADNAM = 1011,
    M_ACCDEN = 1014, M_OVERWR = 1015, M_LOSTERR = 1016, M_INORNOT = 1017,
    M_COPIED = 1018, M_DIRMES = 1019, M_BYTMES = 1020, M_BADDRV = 1021,
    M_CPNOTSET = 1022, M_CPNOTALL = 1023, M_CPACTIVE = 1024, M_NLSFUNC = 1025,
    M_INVCP = 1026, M_BADCURDRV = 1027, M_PAUSEMES = 1028, M_BADLAB = 1029,
    M_SYNTMES = 1030, M_BADDAT = 1031, M_CURDAT = 1032, M_WEEKTAB = 1033,
    M_NEWDAT = 1034, M_BADTIM = 1035, M_CURTIM = 1036, M_NEWTIM = 1037,
    M_DELYN = 1038, M_SUREMES = 1039, M_VERMES = 1040, M_VOLMES2 = 1041,
    M_VOLMES = 1042, M_VOLSERMES = 1043, M_BADCD = 1044, M_BADMKD = 1045,
    M_BADRMD = 1046, M_BADONOFF = 1047, M_DIRHEAD = 1048, M_NULPATH = 1049,
    M_BADPMES = 1050, M_BADDEV = 1051, M_FORNEST = 1052, M_PIPEEMES = 1053,
    M_INBDEV = 1054, M_CTRLCMES = 1055, M_VERIMES = 1056, M_ECHOMES = 1057,
    M_OFFMES = 1059, M_ONMES = 1060, M_DEVWMES = 1061, M_INVALPATH = 1062,
    M_DMES = 1068, M_DBACK = 1069, M_USADAT = 1072, M_EURDAT = 1073,
    M_JAPDAT = 1074, M_MDEXISTS = 1078,
    /* resident / init */
    M_REQ_ABORT = 210, M_REQ_RETRY = 211, M_REQ_IGNORE = 212, M_REQ_FAIL = 213,
    M_REQ_END = 214, M_MREAD = 215, M_MWRITE = 216, M_DRVNUM = 217,
    M_DEVEMES = 218, M_ERR15MES = 219, M_BADFAT = 220, M_COMBAD = 221,
    M_COMPRMT1 = 222, M_RPAUSE = 223, M_ENDBATMES = 224, M_EXECEMES = 225,
    M_EXEBAD = 226, M_TOOBIG = 227, M_NOHANDMES = 228, M_RBADNAM = 229,
    M_RACCDEN = 230, M_BMEMMES = 231, M_HALTMES = 232, M_FRETMES = 233,
    M_PATRICIDE = 234,
    M_BADVER = 461, M_OUTENVERR = 463, M_HEADER = 464, M_BADCOMLK = 465,
    M_BADCOMACC = 466,
};
/* parse error numbers (class C) */
enum { P_TOOMANY = 1, P_MISSING = 2, P_BADSWITCH = 3, P_BADKEYWORD = 4,
       P_RANGE = 6, P_VALUE = 7, P_VALUE2 = 8, P_FORMAT = 9, P_BADPARM = 10,
       P_COMBO = 11 };

/* ------------------------------------------------------------- main.c -- */
extern jmp_buf jb_tcommand;
__attribute__((noreturn)) void tcommand(void);          /* JMP TCOMMAND */
__attribute__((noreturn)) void cerror_msg(int n);       /* CERROR with a transient message */
__attribute__((noreturn)) void cerror_ext(int err, const char *sub);
__attribute__((noreturn)) void cerror_parse(int n, const char *sub);
__attribute__((noreturn)) void cmd_exit(int code);

/* the resident state (res.h): the transient reaches it through R */
struct batseg;
struct forinfo;
#include "res.h"
extern struct res *R;
#define echoflag        (R->echoflag)
#define permcom         (R->permcom)
#define singlecom       (R->singlecom)
#define ffail           (R->ffail)
#define retcode         (R->retcode)
#define forflag         (R->forflag)
#define ifflag          (R->ifflag)
#define pipeflag        (R->pipeflag)
#define pipefiles       (R->pipefiles)
#define nullflag        (R->nullflag)
#define call_flag       (R->call_flag)
#define call_batch_flag (R->call_batch_flag)
#define extcom          (R->extcom)
#define restdir         (R->restdir)
#define curdrv          (R->curdrv)
#define verval          (R->verval)
#define in_init         (R->in_init)
#define init_special    (R->init_special)
#define in_batch        (R->in_batch)
#define batch_abort     (R->batch_abort)
#define comspec         (R->comspec)
#define userdir1        (R->userdir1)
#define io_save         (R->io_save)
#define re_instr        (R->re_instr)
#define re_outstr       (R->re_outstr)
#define re_out_app      (R->re_out_app)
#define mypsp           (R->mypsp)
#define ctrlc_hit       (R->ctrlc_hit)
#define env_seg         (R->envseg)
#define nest            (R->nest)
#define pipestr         (R->pipestr)
#define pipeptr         (R->pipeptr)
#define inpipeptr       (R->inpipeptr)
#define outpipeptr      (R->outpipeptr)
#define pipe1           (R->pipe1)
#define pipe2           (R->pipe2)
#define batch           (*(struct batseg **)&R->batch)
#define next_batch      (*(struct batseg **)&R->next_batch)
#define forptr          (*(struct forinfo **)&R->forptr)
extern uint8_t  combuf[COMBUFLEN + 4];  /* 0: max, 1: count, 2..: text, CR */
/* the transient's parameter area (TRANGROUP 0-100h): FCB at 5Ch (with room
 * for an extended FCB header before it), second FCB at 6Ch, tail at 80h */
extern uint8_t  hdr[256];
#define FCB   (hdr + 0x5C)
#define FCB2  (hdr + 0x6C)
#define TAIL  (hdr + 0x80)
extern uint8_t  parm1, parm2;   /* results of the FCB parses */
extern uint16_t comsw, arg1s, arg2s, argts;
extern int      pathpos_len;    /* length of the internal command name */
extern uint8_t  switchar;
extern uint8_t  dirchar;
extern uint8_t  dirbuf[128];    /* DTA */

/* the parsed command line (PARSE2.ASM arg_unit) */
struct argv_ele {
    char    *argpointer;        /* token text (NUL terminated, in argbuf) */
    char    *argstartel;        /* start of the last path element within it */
    uint16_t arglen;
    uint8_t  argflags;          /* 1 switch, 2 wildcard, 4 path separator */
    uint16_t argsw_word;        /* switch bits that followed this arg */
    char    *arg_ocomptr;       /* where the token began in the command line */
};
#define AF_SWITCH   1
#define AF_WILD     2
#define AF_PATHSEP  4
struct arg_unit {
    int      argvcnt;
    uint16_t argswinfo;
    struct argv_ele argv[ARGMAX];
    char     argbuf[ARGBLEN];
    char     argforcombuf[COMBUFLEN];
};
extern struct arg_unit arg;
/* switch bits (COMEQU.ASM: switch_list "VBAPW") */
#define SW_W    0x0001
#define SW_P    0x0002
#define SW_A    0x0004
#define SW_B    0x0008
#define SW_V    0x0010
#define SW_ANY  0x8000
#define SW_BAD  0x4000

/* ------------------------------------------------------------ parse.c -- */
int  is_delim(int c);                   /* DELIM: space = , ; TAB LF */
const char *scanoff(const char *s);
int  upconv(int c);
int  pathchr(int c);                    /* '\' (or '/' if switchar isn't '/') */
int  prescan(void);                     /* nonzero = pipe */
int  parseline(void);                   /* 0 ok, -1 error */
char *cparse(const char **ps, char *dst, int bl, int cpyflag, int *flags, uint16_t *swbits,
             char **startel, int *count, int expand_star);

/* SYSPARSE, reduced to what COMMAND's parse blocks need */
struct pres {
    int      type;                      /* PT_* */
    char     text[COMBUFLEN];           /* the item (file spec, string), upper-cased if requested */
    int      drive;                     /* PT_DRIVE: 1-based */
    unsigned long num;                  /* PT_NUM */
    int      sw;                        /* index of the matched switch */
    int      y, m, d, h, mi, s, hs;     /* PT_DATE / PT_TIME */
};
enum { PT_EOL = -1, PT_NONE = 0, PT_FILE, PT_DRIVE, PT_STRING, PT_NUM, PT_SWITCH, PT_DATE, PT_TIME };
struct pblock {
    int  minp, maxp;                    /* positionals */
    int  kind;                          /* K_FILE, K_FILE_OR_DRIVE, K_DRIVE, K_ONOFF, K_NUM, K_DATE, K_TIME, K_STRING */
    const char *const *switches;        /* NULL terminated, e.g. "/P" */
    int  capfile;                       /* capitalize */
    unsigned long lo, hi;               /* numeric range */
};
enum { K_FILE = 1, K_FILE_OR_DRIVE, K_DRIVE, K_ONOFF, K_NUM, K_DATE, K_TIME, K_STRING };
/* returns 0 ok (res filled), PT_EOL at end of line, or a parse error number >0;
 * *ps advances; npos counts positionals seen so far. On error, err_sub gets
 * the substitution text (from the item start) as SETUP_PARSE_ERROR_MSG builds it. */
int  sysparse(const char **ps, const struct pblock *pb, int *npos, struct pres *res);
int  parse_with_msg(const char **ps, const struct pblock *pb, int *npos, struct pres *res);
int  parse_check_eol(const char **ps, const struct pblock *pb, int *npos);
extern char err_sub[COMBUFLEN];

/* ------------------------------------------------------------ batch.c -- */
void readbat(void);
void batcom(void);
void batchoff(void);
void foroff(void);
void pipeoff(void);
void forproc(void);                     /* never returns: jumps to docom */
int  askend(void);
int  batch_free_one(void);
#define BOGUS_BATCH ((struct batseg *)1)                      /* 1 = terminate */
void getkeystroke(void);
void c_call(void), c_goto(void), c_shift(void), c_if(void), c_for(void);
int  batch_param_ok(void);

/* -------------------------------------------------------------- env.c -- */
void env_init(unsigned envsize_paras, int build);
char *env_find(const char *name);       /* "NAME=" -> pointer to the value, or NULL */
void env_delete(const char *name);      /* name with '=' */
char *env_end(void);                    /* the double NUL */
void env_store(char **dp, int c);       /* STORE_CHAR (may CERROR) */
void print_prompt(void);
void print_version(void);
void print_date(void);
void print_time(void);
void c_set(void), c_prompt(void), c_path(void);

/* ------------------------------------------------------------- exec.c -- */
int  path_search(char *execpath);        /* 0 none, 2 bat, 4 exe, 8 com */
void external(void);                     /* external command */
void ioset(void);
void headfix(void);
void pipedel(void);
void pipeprocstrt(void);
int  pipeproc(void);                    /* 1 = element ready in combuf */
void testdorein(void);
void testdoreout(void);
extern char execpath[128];

void install_handlers(void);

/* ---------------------------------------------------------- commands -- */
void c_dir(void), c_vol(void), c_del(void), c_ren(void), c_type(void);
void c_pause(void), c_rem(void), c_copy(void), c_date(void), c_time(void);
void c_ver(void), c_cd(void), c_md(void), c_rd(void), c_break(void);
void c_verify(void), c_exit(void), c_ctty(void), c_echo(void), c_cls(void);
void c_truename(void), c_chcp(void);
void e_msd(void), e_win(void), e_deltree(void), e_memmaker(void), e_intel(void);
void e_xyzzy(void), e_plugh(void), e_iddqd(void), e_idkfa(void), e_hal(void);
void e_eliza(void), e_42(void), e_sudo(void), e_ls(void), e_uname(void);
void okvolarg(void);
int  pathcrunch(int dirflag, int *zf);   /* CF result; *zf as ZF */
void restudir(void);
void setpath(const char *src);
extern int destinfo, destisdir, msg_numb;
extern char srcbuf[COMBUFLEN + 16];
extern char *desttail;
extern char bwdbuf[80];
void build_dir_string(void);            /* bwdbuf = "D:\cur" for drive in fcb1 */
int  date_prompt_loop(const char *arg);
int  time_prompt_loop(const char *arg);
void datinit(void);
void copy_cleanup(void);
void batcom_init(const char *file);
void set_psp_handlers(void);

/* util */
size_t xstrlcpy(char *d, const char *s, size_t n);

#endif
