/*
 * LABEL - ARM-DOS re-creation of the MS-DOS 4.00 LABEL command.
 *
 *   LABEL [d:][label]
 *
 * Behaviour follows CMD/LABEL/LABEL.ASM of the MS-DOS 4.0 source (MIT
 * licence, (C) Microsoft Corp.):
 *  - the drive comes from DOS's parse of the first operand (FCB 1); a bad
 *    one gives "Invalid drive specification"; network (IOCTL 4409h) and
 *    SUBSTed/ASSIGNed drives (TRUENAME of "d:CON") are refused
 *  - the old label is found with an extended-FCB search (attribute 08h)
 *  - the new label is taken from the command line after "d:" (a blank right
 *    after "d:" means "ask"); characters * ? [ ] : < | > + = ; , / \ . " and
 *    control characters are invalid, more than 11 are silently dropped,
 *    blanks are allowed; a "." is put after the 8th character
 *  - without one: "Volume in drive C is XXXXXXXXXXX" (the 11-byte name with
 *    its blanks) or "... has no label", the serial number (INT 21h AX=6900h),
 *    then "Volume label (11 characters, ENTER for none)? " and a read of up
 *    to 127 bytes from STDIN (INT 21h AH=3Fh)
 *  - ENTER with an old label: "Delete current volume label (Y/N)? " (input
 *    without echo, INT 21h AH=0Ch AL=08h, answer checked with AX=6523h)
 *  - always CR LF, then the old label is deleted (FCB delete, AH=13h) and the
 *    new one created (AH=5Bh, attribute 08h); on failure "Cannot make
 *    directory entry" and the old label is put back.
 * Like 4.00, only the directory entry changes; the boot sector's copy of the
 * label is left alone.
 */
#include "u4.h"

#define USER_INPUT 0x01
#define LABEL_FND  0x02
#define NO_DELETE  0x04
#define GET_INPUT  0x08
#define CHAR_BAD   0x10

static uint8_t flags;
static char new_path[20] = " :\\";        /* " :\" + name + 0 */
static char *const new_name = new_path + 3;
static char label_name[12] = "???????????";
static char drive_char;
static int  fcb_drive;
static uint8_t parm[130];                 /* PSP:81h, or what the user typed */
static uint8_t vol_fcb[44];
static uint8_t dta[64];

static __attribute__((noreturn)) void error_exit(void) { u4_exit(1); }

static void ext_fcb(uint8_t *f, int drive)
{
    memset(f, 0, 44);
    f[0] = 0xFF;
    f[6] = 0x08;
    f[7] = drive;
    memset(f + 8, '?', 11);
}

static int bad_char(int c)
{
    if (c < ' ') return 1;
    return strchr("*?[]:<|>+=;,/\\.\"", c) != 0;
}

/* Get_New_Label's scanner; returns with new_name filled (may be empty) or
 * with GET_INPUT/CHAR_BAD set */
static void scan(void)
{
    const uint8_t *si = parm;
    char *di = new_name;
    int cx = 0;
    int al;
    flags &= ~GET_INPUT;
    do al = *si++; while (al == ' ');                   /* Find_First_Char */
    if (!(flags & USER_INPUT) && al != '\r' && *si == ':') {
        si++;                                           /* drive letter */
        al = *si++;
    }
    if (al == ' ') { flags |= GET_INPUT; return; }
    for (;;) {                                          /* Process_String */
        if (al == '\r') break;
        if (bad_char(al)) { flags |= CHAR_BAD; return; }
        if (cx < 11) *di++ = al;
        cx++;
        if (cx == 8) *di++ = '.';
        al = *si++;
        if (cx == 127) break;
    }
    *di = 0;
}

static void output_old_label(void)
{
    char d[2] = { drive_char, 0 };
    if (flags & LABEL_FND) {
        u4_puts(STDOUT, "Volume in drive "); u4_puts(STDOUT, d);
        u4_puts(STDOUT, " is "); u4_puts(STDOUT, label_name); u4_puts(STDOUT, "\r\n");
    } else {
        u4_puts(STDOUT, "Volume in drive "); u4_puts(STDOUT, d);
        u4_puts(STDOUT, " has no label\r\n");
    }
    static uint8_t buf[26];
    memset(buf, 0, sizeof buf);
    struct armregs r; u4_clr(&r);
    r.r0 = 0x6900; r.r1 = fcb_drive; r.r3 = (uint32_t)buf;
    if (!u4_int21(&r)) {
        char h[10];
        uint32_t sn = buf[2] | (buf[3] << 8) | (buf[4] << 16) | ((uint32_t)buf[5] << 24);
        u4_puts(STDOUT, "Volume Serial Number is ");
        u4_puts(STDOUT, u4_hex(sn >> 16, 4, h)); u4_puts(STDOUT, "-");
        u4_puts(STDOUT, u4_hex(sn & 0xFFFF, 4, h)); u4_puts(STDOUT, "\r\n");
    }
}

static void get_user_input(void)
{
    if (flags & CHAR_BAD) {
        u4_puts(STDERR, "Invalid characters in volume label\r\n");
        flags &= ~CHAR_BAD;
    }
    u4_puts(STDOUT, "Volume label (11 characters, ENTER for none)? ");
    u4_read(STDIN, parm, 0x100 - 0x81);
    flags |= USER_INPUT;
}

int main(void)
{
    if (!u4_check_version()) error_exit();

    /* DOS's FCB 1: the drive of the first operand */
    const char *t = u4_cmdline();
    memcpy(parm, t, sizeof parm - 1);
    uint8_t fcb1[40];
    struct armregs r; u4_clr(&r);
    r.r0 = 0x2901; r.r4 = (uint32_t)t; r.r5 = (uint32_t)fcb1;
    u4_int21(&r);
    if ((r.r0 & 0xFF) == 0xFF) {
        u4_exterr(STDERR, 15, 0);
        error_exit();
    }
    fcb_drive = fcb1[0] ? fcb1[0] : u4_curdrive() + 1;
    drive_char = '@' + fcb_drive;
    u4_clr(&r);
    r.r0 = 0x4409; r.r1 = fcb_drive;
    if (!u4_int21(&r) && (r.r3 & 0x1000)) {
        u4_puts(STDERR, "Cannot LABEL a network drive\r\n");
        error_exit();
    }
    {
        static char src[8] = "A:CON", dst[70];
        src[0] = drive_char;
        memset(dst, ' ', sizeof dst);
        u4_clr(&r);
        r.r0 = 0x6000; r.r4 = (uint32_t)src; r.r5 = (uint32_t)dst;
        if (!u4_int21(&r) && dst[0] != src[0]) {
            u4_puts(STDERR, "Cannot LABEL a SUBSTed or ASSIGNed drive\r\n");
            error_exit();
        }
    }
    new_path[0] = drive_char;

    /* Find_Old_Label */
    ext_fcb(vol_fcb, fcb_drive);
    u4_clr(&r); r.r0 = 0x1A00; r.r3 = (uint32_t)dta; u4_int21(&r);
    u4_clr(&r); r.r0 = 0x1100; r.r3 = (uint32_t)vol_fcb; u4_int21(&r);
    if ((r.r0 & 0xFF) != 0xFF) {
        flags |= LABEL_FND;
        memcpy(label_name, dta + 8, 11);            /* the found entry's name */
    }

    /* Get_New_Label */
    for (;;) {
        scan();
        if (!(flags & (GET_INPUT | CHAR_BAD))) {
            if (new_name[0] || (flags & USER_INPUT)) break;
        }
        if (!(flags & USER_INPUT)) output_old_label();
        get_user_input();
    }

    /* Check_Delete */
    if ((flags & LABEL_FND) && !new_name[0]) {
        for (;;) {
            u4_puts(STDOUT, "\r\nDelete current volume label (Y/N)? ");
            int c = u4_getkey(8);
            u4_clr(&r); r.r0 = 0x6523; r.r3 = c;
            int a;
            if (!u4_int21(&r)) a = r.r0 & 0xFFFF;
            else a = (c == 'y' || c == 'Y') ? 1 : (c == 'n' || c == 'N') ? 0 : 2;
            if (a == 1) break;
            if (a == 0) { flags |= NO_DELETE; break; }
        }
    }
    u4_puts(STDOUT, "\r\n");

    /* Delete_Old_Label */
    if (!(flags & NO_DELETE)) {
        ext_fcb(vol_fcb, fcb_drive);
        u4_clr(&r); r.r0 = 0x1300; r.r3 = (uint32_t)vol_fcb; u4_int21(&r);
    }

    /* Create_New_Label */
    if (new_name[0]) {
        u4_clr(&r); r.r0 = 0x5B00; r.r2 = 0x08; r.r3 = (uint32_t)new_path;
        int cf = u4_int21(&r);
        if (cf && (r.r0 & 0xFFFF) != 4 && strchr(new_name, ' ')) {
            /* ARM-DOS's kernel refuses a blank inside a path name, DOS 4
             * takes it ("LABEL MY DISK"); make the entry with an extended
             * FCB instead (AH=16h), which has no such rule */
            uint8_t f[44];
            ext_fcb(f, fcb_drive);
            memset(f + 8, ' ', 11);
            const char *n = new_name;
            for (int i = 0; *n && *n != '.' && i < 8; i++) f[8 + i] = u4_upcase(*n++);
            if (*n == '.') n++;
            for (int i = 0; *n && i < 3; i++) f[16 + i] = u4_upcase(*n++);
            struct armregs q; u4_clr(&q);
            q.r0 = 0x1600; q.r3 = (uint32_t)f;
            u4_int21(&q);
            if ((q.r0 & 0xFF) == 0) {
                u4_clr(&q); q.r0 = 0x1000; q.r3 = (uint32_t)f; u4_int21(&q);
                return 0;
            }
        }
        if (!cf) {
            u4_close(r.r0 & 0xFFFF);
        } else {
            if ((r.r0 & 0xFFFF) == 4) u4_exterr(STDERR, 4, 0);
            else {
                u4_exterr(STDERR, 82, 0);
                if (flags & LABEL_FND) {
                    memcpy(new_name, label_name, 8);
                    new_name[8] = '.';
                    memcpy(new_name + 9, label_name + 8, 3);
                    new_name[12] = 0;
                    u4_clr(&r); r.r0 = 0x5B00; r.r2 = 0x08; r.r3 = (uint32_t)new_path;
                    u4_int21(&r);
                }
            }
            error_exit();
        }
    }
    return 0;
}
