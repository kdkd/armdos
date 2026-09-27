/* atest.c - functional checks of ANSI.SYS that need a program: install check,
   the cursor position report, key reassignment, the display IOCTL.
   Keys are typed by the harness when it sees T:WAITKEY n. */
#include "tt.h"

static void getn(char *out, int n) { for (int i = 0; i < n; i++) out[i] = tt_getc(); out[n] = 0; }

static int ioctl_con(int minor, uint8_t *d)
{
    struct armregs r = { 0 };
    r.r0 = 0x440C; r.r1 = 1; r.r2 = 0x0300 | minor; r.r3 = (uint32_t)d;
    int cf = tt_dos(&r);
    return cf ? (int)(r.r0 & 0xFFFF) : 0;
}

int main(void)
{
    char s[64];
    struct armregs r = { 0 };

    /* INT 2Fh AX=1A00h: installed */
    r.r0 = 0x1A00;
    _armdos_int2f(&r);
    TT_EQ(r.r0 & 0xFF, 0xFF);

    /* DSR: ESC[6n queues ESC[rr;ccR CR as keyboard input */
    tt_out("\x1b[5;10H\x1b[6n");
    getn(s, 9);
    TT_CHECK(!strcmp(s, "\x1b[05;10R\r"), "report '%s'", s + 1);
    tt_out("\x1b[25;80H\x1b[6n");
    getn(s, 9);
    TT_CHECK(!strcmp(s, "\x1b[25;80R\r"), "report '%s'", s + 1);
    tt_out("\x1b[1;1H");

    /* key reassignment: A -> "xyz" */
    tt_out("\x1b[65;\"xyz\"p");
    tt_log("T:WAITKEY 1\n");
    getn(s, 3);
    TT_CHECK(!strcmp(s, "xyz"), "A gives '%s'", s);
    /* F1 (0;59) -> "dir" CR, and q (113) -> 'Q' ('q' = 113) */
    tt_out("\x1b[0;59;\"dir\";13p\x1b[113;81p");
    tt_log("T:WAITKEY 2\n");
    getn(s, 5);
    TT_CHECK(!strcmp(s, "dir\rQ"), "F1 q gives '%s'", s);
    /* undo: ESC[65p deletes a one-byte key's definition; extended keys are
       reset by assigning them to themselves (ESC[0;59p would leave an
       empty definition, as in DOS 4) */
    tt_out("\x1b[65p\x1b[0;59;0;59p\x1b[113;113p");
    tt_log("T:WAITKEY 3\n");
    getn(s, 4);
    TT_CHECK(s[0] == 'A' && s[1] == 0 && s[2] == 59 && s[3] == 'q', "after delete: %02x %02x %02x %02x",
             s[0], s[1], s[2], s[3]);

    /* generic IOCTL 440Ch CX=037Fh: get display information */
    uint8_t d[18];
    memset(d, 0, sizeof d);
    d[2] = 14;
    TT_EQ(ioctl_con(0x7F, d), 0);
    TT_EQ(d[6], 1);
    TT_EQ(d[8] | (d[9] << 8), 16);
    TT_EQ(d[14] | (d[15] << 8), 80);
    TT_EQ(d[16] | (d[17] << 8), 25);
    TT_EQ(d[10] | (d[11] << 8), 0xFFFF);
    /* set 50 lines, then 43, then back to 25 */
    d[16] = 50;
    TT_EQ(ioctl_con(0x5F, d), 0);
    TT_EQ(*(volatile uint8_t *)0x484, 49);
    tt_out("fifty lines\r\n");
    tt_log("T:WAITKEY 4\n");
    tt_getc();
    d[16] = 43;
    TT_EQ(ioctl_con(0x5F, d), 0);
    TT_EQ(*(volatile uint8_t *)0x484, 42);
    tt_out("forty-three lines\r\n");
    tt_log("T:WAITKEY 5\n");
    tt_getc();
    d[16] = 30;
    TT_CHECK(ioctl_con(0x5F, d) != 0, "30 lines accepted");
    d[16] = 25;
    TT_EQ(ioctl_con(0x5F, d), 0);
    TT_EQ(*(volatile uint8_t *)0x484, 24);
    /* 40 columns */
    d[14] = 40;
    TT_EQ(ioctl_con(0x5F, d), 0);
    TT_EQ(*(volatile uint8_t *)0x449, 1);
    d[14] = 80;
    TT_EQ(ioctl_con(0x5F, d), 0);
    TT_EQ(*(volatile uint8_t *)0x449, 3);

    tt_log(tt_fails ? "T:RESULT FAIL\n" : "T:RESULT PASS\n");
    return tt_fails ? 1 : 0;
}
