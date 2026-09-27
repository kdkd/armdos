/*
 * mapper.c - the part of MS-DOS 4.0's MAPPER library (MAPPER/ *.ASM: the
 * OS/2-style family API "mapped" onto INT 21h/10h/16h for the DOS versions
 * of FDISK, BACKUP and RESTORE) that FDISK uses, in C for ARM-DOS.  Each
 * function does what its MAPPER counterpart does (same BIOS/DOS calls, same
 * parameter checks and return codes), with the pascal argument order of
 * H/DOSCALLS.H.
 *
 * Portions (c) Microsoft Corp. (MS-DOS 4.0 MAPPER), MIT License.
 */
#include <string.h>
#include <dos.h>
#include <armdos.h>

struct KeyData {                /* H/DOSCALLS.H */
    char char_code;
    char scan_code;
    char status;
    unsigned shift_state;
    unsigned long time;
};

static unsigned short savedkbdinput;   /* FLUSHBUF.ASM: a key read while flushing */

static int int10(struct armregs *r) { return _armdos_intr(0x10, r); }

/* BEEP.ASM: PIT channel 2 at the frequency, speaker on for duration ms */
unsigned DOSBEEP(unsigned frequency, unsigned duration)
{
    struct armregs r;
    if (frequency < 0x25 || frequency > 0x7FFF) return 2;
    unsigned div = 1193180u / frequency;
    armdos_outb(0x43, 0xB6);
    armdos_outb(0x42, div & 0xFF);
    armdos_outb(0x42, (div >> 8) & 0xFF);
    unsigned char b = armdos_inb(0x61);
    armdos_outb(0x61, b | 3);
    /* the original counted a delay loop; here INT 15h AH=86h waits CX:DX us */
    memset(&r, 0, sizeof r);
    unsigned long us = (unsigned long)duration * 1000;
    r.r0 = 0x8600;
    r.r2 = us >> 16;
    r.r3 = us & 0xFFFF;
    _armdos_intr(0x15, &r);
    armdos_outb(0x61, b);
    return 0;
}

/* EXIT.ASM: action 0/1 end the program with the result code */
void DOSEXIT(unsigned action, unsigned result)
{
    struct armregs r;
    if (action > 1) return;
    memset(&r, 0, sizeof r);
    r.r0 = 0x4C00 | (result & 0xFF);
    _armdos_int21(&r);
}

/* CHARIN.ASM: INT 21h 0Bh + 06h/FFh (wait unless iowait = 1) */
unsigned KBDCHARIN(struct KeyData *data, unsigned iowait, unsigned handle)
{
    struct armregs r;
    unsigned char c = 0;
    int have = 0;
    (void)handle;
    for (;;) {
        if (savedkbdinput >> 8) {
            c = savedkbdinput & 0xFF;
            savedkbdinput = 0;
            have = 1;
        } else {
            memset(&r, 0, sizeof r);
            r.r0 = 0x0B00;
            _armdos_int21(&r);
            memset(&r, 0, sizeof r);
            r.r0 = 0x0600;
            r.r3 = 0xFF;
            _armdos_int21(&r);
            if (!(r.cpsr & ARM_CPSR_Z)) { c = r.r0 & 0xFF; have = 1; }
        }
        if (have && c) break;
        if (!have && iowait) break;
        have = 0;
    }
    if (have) { data->scan_code = 0; data->char_code = c; data->status = 1; }
    else { data->char_code = 0; data->scan_code = 0; data->status = 0; }
    memset(&r, 0, sizeof r);
    r.r0 = 0x0200;
    _armdos_intr(0x16, &r);
    data->shift_state = r.r0 & 0xFF;
    memset(&r, 0, sizeof r);
    r.r0 = 0x2C00;
    _armdos_int21(&r);
    data->time = ((r.r2 >> 8) & 0xFF) | ((r.r2 & 0xFF) << 8) |
                 (((r.r3 >> 8) & 0xFF) << 16) | ((unsigned long)(r.r3 & 0xFF) << 24);
    return 0;
}

/* FLUSHBUF.ASM: INT 21h 0Bh, then AX=0C06h DL=FFh keeps one waiting key */
unsigned KBDFLUSHBUFFER(unsigned handle)
{
    struct armregs r;
    (void)handle;
    memset(&r, 0, sizeof r);
    r.r0 = 0x0B00;
    _armdos_int21(&r);
    memset(&r, 0, sizeof r);
    r.r0 = 0x0C06;
    r.r3 = 0xFF;
    _armdos_int21(&r);
    savedkbdinput = (r.cpsr & ARM_CPSR_Z) ? 0 : (unsigned short)(0x0100 | (r.r0 & 0xFF));
    return 0;
}

/* SCROLLUP.ASM: INT 10h AH=06h with the attribute of the cell (char, attr) */
unsigned VIOSCROLLUP(unsigned toprow, unsigned leftcol, unsigned botrow, unsigned rightcol,
                     unsigned lines, char *cell, unsigned handle)
{
    struct armregs r;
    (void)handle;
    if ((lines & 0xFF) > 25 || (rightcol & 0xFF) > 80 || (botrow & 0xFF) > 25) return 2;
    memset(&r, 0, sizeof r);
    r.r0 = 0x0600 | (lines & 0xFF);
    r.r1 = ((unsigned char)cell[0]) << 8;          /* mov bx,[si]; mov bh,bl */
    r.r2 = ((toprow & 0xFF) << 8) | (leftcol & 0xFF);
    r.r3 = ((botrow & 0xFF) << 8) | (rightcol & 0xFF);
    int10(&r);
    return 0;
}

/* SCURPOS.ASM: INT 10h AH=02h, page 0 */
unsigned VIOSETCURPOS(unsigned row, unsigned column, unsigned handle)
{
    struct armregs r;
    (void)handle;
    if ((row & 0xFF) > 25 || (column & 0xFF) > 80) return 2;
    memset(&r, 0, sizeof r);
    r.r0 = 0x0200;
    r.r3 = ((row & 0xFF) << 8) | (column & 0xFF);
    int10(&r);
    return 0;
}

/* WCHSTRA.ASM: each character with INT 10h AH=09h at the cursor, which moves
   on (AH=02h), wrapping at column 80; the cursor ends after the string */
unsigned VIOWRTCHARSTRATT(char *str, unsigned length, unsigned row, unsigned column,
                          char *attr, unsigned handle)
{
    struct armregs r;
    unsigned dl = column & 0xFF, dh = row & 0xFF;
    (void)handle;
    if (dl > 80 || dh > 25) return 2;
    memset(&r, 0, sizeof r);
    r.r0 = 0x0200;
    r.r3 = (dh << 8) | dl;
    int10(&r);
    unsigned char a = (unsigned char)attr[0];
    for (unsigned n = length & 0xFFFF; n; n--) {
        memset(&r, 0, sizeof r);
        r.r0 = 0x0900 | (unsigned char)*str++;
        r.r1 = a;
        r.r2 = 1;
        int10(&r);
        if (++dl == 80) {
            dl = 0;
            if (++dh == 25) return 1;
        }
        memset(&r, 0, sizeof r);
        r.r0 = 0x0200;
        r.r3 = (dh << 8) | dl;
        int10(&r);
    }
    return 2;                   /* sic: the original falls into its error exit */
}
