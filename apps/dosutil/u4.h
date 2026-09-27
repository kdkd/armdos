/*
 * u4.h - shared helpers for the ARM-DOS re-creations of the MS-DOS 4.00
 * external commands (FIND, SORT, MORE, TREE, COMP, XCOPY, LABEL, REPLACE,
 * PRINT).  Written from the behaviour of the 4.00 sources (MIT licensed,
 * Microsoft) and the real binaries; no Microsoft code is copied.
 *
 *  - INT 21h helpers that go through DOS handles (so redirection works)
 *  - the DOS 4 "message retriever" conventions (class 1 extended errors,
 *    class 2 parse errors, " - " + parameter)
 *  - a C re-implementation of the common command-line parser SysParse
 *    (INC/PARSE.ASM): delimiters, switches, positionals, file specs, quoted
 *    strings, numbers, dates, drives, with the same return codes.
 */
#ifndef U4_H
#define U4_H

#include <stdint.h>
#include <string.h>
#include "armdos.h"

#define STDIN  0
#define STDOUT 1
#define STDERR 2

/* ---------------------------------------------------------- INT 21h */
int  u4_int21(struct armregs *r);                 /* returns CF */
int  u4_int10(struct armregs *r), u4_int16(struct armregs *r), u4_int17(struct armregs *r);
int  u4_int25(struct armregs *r), u4_int26(struct armregs *r);
int  u4_int28(struct armregs *r), u4_int2f(struct armregs *r);
/* the program's memory block is shrunk to image + bss + stack + this many
 * bytes (default 0; a program may define it) */
extern unsigned u4_heap_bytes;
extern uint8_t *u4_heap;                          /* start of that extra memory */
static inline void u4_clr(struct armregs *r) { memset(r, 0, sizeof *r); }
int  u4_write(int h, const void *p, unsigned n);  /* bytes written, -1 error */
void u4_puts(int h, const char *s);
int  u4_read(int h, void *p, unsigned n);         /* bytes read, -1 error (u4_err) */
int  u4_open(const char *name, int mode);         /* handle or -1 (u4_err = DOS error) */
int  u4_creat(const char *name, int attr);
int  u4_close(int h);
long u4_lseek(int h, long off, int whence);
extern int u4_err;                                /* last DOS error code */
__attribute__((noreturn)) void u4_exit(int code);
int  u4_getkey(int fn);                           /* INT 21h AH=0Ch, AL=fn (1/7/8) */
int  u4_curdrive(void);                           /* 0 = A: */
int  u4_version_ok(void);                         /* INT 21h AH=30h says 4.00 */
/* "Incorrect DOS version" to STDERR if not 4.00; returns 0 then */
int  u4_check_version(void);

char *u4_utoa(uint32_t v, char *buf);             /* decimal, returns buf */
char *u4_hex(uint32_t v, int width, char *buf);   /* upper-case hex, zero padded */
char *u4_pad(const char *s, int width, char *buf);/* right align in width, blanks */
unsigned char u4_upcase(unsigned char c);         /* country (CP437 US) case map */
void u4_strupr(char *s);

/* the command tail (PSP:81h, CR-terminated) copied to a private buffer */
char *u4_cmdline(void);

/* ---------------------------------------------------------- messages */
/* "%1".."%9" replaced by args[0..8] (NULL = empty) */
void u4_msg(int h, const char *text, const char *const *args);
static inline void u4_msg1(int h, const char *text, const char *a1)
{ const char *a[1] = { a1 }; u4_msg(h, text, a); }
/* class 1 (extended error) and class 2 (parse error) messages: text,
 * then " - " + param when param != NULL, then CR LF */
const char *u4_exterr_text(int code);
const char *u4_parse_text(int rc);
void u4_class_msg(int h, const char *text, const char *param);
void u4_exterr(int h, int code, const char *param);
void u4_parse_err(int h, int rc, const char *param);

/* ---------------------------------------------------------- the parser */
/* match flags (CONTROL.match) */
#define P_NUM       0x8000
#define P_SNUM      0x4000
#define P_SIMPLE    0x2000
#define P_DATE      0x1000
#define P_TIME      0x0800
#define P_CMPX      0x0400
#define P_FILE      0x0200
#define P_DRIVE     0x0100
#define P_QUOTED    0x0080
#define P_IGCOLON   0x0010
#define P_REPEAT    0x0002
#define P_OPTIONAL  0x0001
/* function flags (CONTROL.func) */
#define P_CAP_FILE  0x0001
#define P_CAP_CHAR  0x0002
#define P_RM_COLON  0x0010
#define P_COLON_NN  0x0020     /* "/+10" as well as "/+:10" */
/* result types */
#define R_EOL     0
#define R_NUMBER  1
#define R_LISTIDX 2
#define R_STRING  3
#define R_COMPLEX 4
#define R_FILE    5
#define R_DRIVE   6
#define R_DATE    7
#define R_TIME    8
#define R_QUOTED  9
/* return codes */
#define P_OK          0
#define P_TOO_MANY    1
#define P_MISSING     2
#define P_BAD_SWITCH  3
#define P_BAD_KEYWORD 4
#define P_RANGE       6
#define P_NOT_IN_VAL  7
#define P_NOT_IN_STR  8
#define P_SYNTAX      9
#define P_RC_EOL      (-1)

struct u4_range { uint32_t lo, hi; };
struct u4_ctl {
    uint16_t match, func;
    const char *names;          /* switches/keywords: "/N\0/V\0" ... ends "\0\0" */
    uint8_t nrange;             /* numeric value list: ranges */
    const struct u4_range *range;
};
struct u4_parms {
    uint8_t minp, maxp;
    const struct u4_ctl *const *pos;   /* maxp positional controls */
    uint8_t nsw;
    const struct u4_ctl *const *sw;
    uint8_t nkey;
    const struct u4_ctl *const *key;
    const char *delims;         /* extra delimiters, e.g. ";" */
    const char *eols;           /* extra end-of-line characters, or NULL */
};
struct u4_result {
    uint8_t type;
    const struct u4_ctl *ctl;   /* the control that matched */
    const char *synonym;        /* matched switch/keyword name */
    uint32_t value;             /* number; drive (1 = A:) */
    uint16_t year; uint8_t month, day;        /* R_DATE */
    uint8_t hour, minute, second, hundredth;  /* R_TIME */
    char *str;                  /* string result (points into state buffer) */
};
struct u4_pstate {
    const char *si;             /* next character of the command line */
    int ordinal;
    uint8_t terminator;
    char buf[160];
};
/* One call = one operand, as SysParse.  Returns P_OK, an error code, or
 * P_RC_EOL.  st->si is advanced past the operand. */
int u4_parse(const struct u4_parms *p, struct u4_pstate *st, struct u4_result *r);

#endif
