/*
 * mslib_msg.h - ARM-DOS re-implementation of the MS-DOS 4.0 message retriever
 * (INC/MSGSERV.ASM, INC/SYSMSG.INC) and parser (INC/PARSE.ASM) interfaces,
 * for Microsoft's C utilities compiled for ARM (apps/mslib).
 *
 * The C calling convention is Microsoft's (CMD/MEM/MEM.C and friends):
 *
 *     sysloadmsg(&in, &out);           CF (out.x.cflag bit 0) = wrong DOS version
 *     sysdispmsg(&in, &out);           AX msg, BX handle, CX sublists, DL input fn,
 *                                      DH class, SI -> first sublist, DI input buffer
 *     sysgetmsg(&in, &segs, &out);     AX msg, DH class -> SI text, CX length
 *     parse(&in, &out);                SI line, DI -> p_parms, CX ordinal
 *
 * The only ABI change from the 8086 original: every pointer (sublist value,
 * the parser's control block pointers) is a flat 32-bit pointer, and the
 * program's parser structures follow ARM natural alignment (msgret.c and
 * parse.c document the exact layouts).  SREGS are ignored.
 */
#ifndef MSLIB_MSG_H
#define MSLIB_MSG_H

#include <dos.h>

struct msg_entry {
    unsigned char  cls;     /* 1 extended error, 2 parse error, 0FFh utility */
    unsigned short num;
    unsigned short len;
    const char    *text;
};

/* generated per utility by tools/msgtab.py (the utility's .SKL) */
extern const struct msg_entry _msg_util_table[];
/* generated once (USA-MS.MSG sections EXTEND and PARSE) */
extern const struct msg_entry _msg_extend_table[];
extern const struct msg_entry _msg_parse_table[];

void sysloadmsg(union REGS *in, union REGS *out);
void sysdispmsg(union REGS *in, union REGS *out);
void sysgetmsg(union REGS *in, struct SREGS *segs, union REGS *out);
void parse(union REGS *in, union REGS *out);

/* the DOS version the retriever accepts (SYSLOADMSG's $M_VERSION_CHECK) */
#define MSLIB_EXPECTED_MAJOR 4
#define MSLIB_EXPECTED_MINOR 0

#endif
