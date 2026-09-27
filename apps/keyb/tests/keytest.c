/* keytest.c - KEYB tests' key reader: prints every key INT 16h AH=10h returns as
   "SSCC" (scan, character, hex) to standard output until Esc, after showing
   "KTEST READY" on the screen (INT 10h, not redirected) */
#include <stdio.h>
#include <dos.h>
#include <bios.h>
static void say(const char *s) { union REGS r; for (; *s; s++) { r.h.ah = 0x0E; r.h.al = *s; r.x.bx = 7; int86(0x10, &r, &r); } }
int main(void)
{
    say("KTEST READY\r\n");
    for (;;) {
        union REGS r = { 0 };
        r.x.ax = 0x1000;
        int86(0x16, &r, &r);
        unsigned k = r.x.ax & 0xFFFF;
        printf("%04X ", k);
        if ((k >> 8) == 0x01) break;
    }
    printf("\n");
    say("KTEST DONE\r\n");
    return 0;
}
