/* mtest.c - MOUSE.COM test program: reset, show the cursor, report clicks.
 *   MTEST      text mode: waits for a left click (reported on screen and on
 *              COM1), then shows the cursor in mode 13h until a right click
 *   MTEST /N   only checks that no driver answers
 * Results on COM1 as T:... lines for tests/run.mjs. */
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "armdos.h"

static void ser(const char *fmt, ...)
{
    char b[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    for (char *s = b; *s; s++) armdos_outb(0x3F8, *s);
}

static struct armregs m(int ax, int bx, int cx, int dx)
{
    struct armregs r = { 0 };
    r.r0 = ax; r.r1 = bx; r.r2 = cx; r.r3 = dx;
    _armdos_int33(&r);
    return r;
}

static volatile int hcalls, hmask, hbx, hcx, hdx;
static void handler(uint32_t ax, uint32_t bx, uint32_t cx, uint32_t dx)
{
    hcalls++; hmask = ax & 0xFFFF; hbx = bx & 0xFFFF; hcx = cx & 0xFFFF; hdx = dx & 0xFFFF;
}

static void setmode(int mode)
{
    struct armregs r = { 0 };
    r.r0 = mode;
    _armdos_int10(&r);
}

int main(int argc, char **argv)
{
    struct armregs r;
    if (argc > 1 && !strcmp(argv[1], "/N")) {
        r = m(0, 0, 0, 0);
        ser("T:NODRIVER %04X\r\n", (unsigned)(r.r0 & 0xFFFF));
        return 0;
    }
    r = m(0, 0, 0, 0);
    ser("T:RESET %04X %u\r\n", (unsigned)(r.r0 & 0xFFFF), (unsigned)(r.r1 & 0xFFFF));
    r = m(0x24, 0, 0, 0);
    ser("T:VERSION %04X %04X\r\n", (unsigned)(r.r1 & 0xFFFF), (unsigned)(r.r2 & 0xFFFF));
    r = m(3, 0, 0, 0);
    ser("T:POS %u %u %u\r\n", (unsigned)(r.r1 & 0xFFFF), (unsigned)(r.r2 & 0xFFFF), (unsigned)(r.r3 & 0xFFFF));
    m(0x0C, 0, 0x0A, (uint32_t)handler);        /* CX = mask: left press, right press */
    printf("Move the mouse and click the left button.\n");
    m(1, 0, 0, 0);
    ser("T:READY\r\n");
    for (;;) {
        r = m(5, 0, 0, 0);                      /* left button presses */
        if (r.r1 & 0xFFFF) {
            unsigned x = r.r2 & 0xFFFF, y = r.r3 & 0xFFFF;
            m(2, 0, 0, 0);
            printf("Left click at %u,%u (column %u, row %u)\n", x, y, x / 8, y / 8);
            m(1, 0, 0, 0);
            ser("T:CLICK %u %u\r\n", x, y);
            break;
        }
        armdos_halt();
    }
    r = m(0x0B, 0, 0, 0);
    ser("T:MICKEYS %d %d\r\n", (int16_t)r.r2, (int16_t)r.r3);
    ser("T:HANDLER %d %u %u %u %u\r\n", hcalls, hmask, hbx, hcx, hdx);

    setmode(0x13);
    m(0, 0, 0, 0);
    m(1, 0, 0, 0);
    r = m(3, 0, 0, 0);
    ser("T:GFX %u %u\r\n", (unsigned)(r.r2 & 0xFFFF), (unsigned)(r.r3 & 0xFFFF));
    for (;;) {
        r = m(5, 1, 0, 0);                      /* right button presses */
        if (r.r1 & 0xFFFF) break;
        armdos_halt();
    }
    m(2, 0, 0, 0);
    setmode(3);
    ser("T:DONE\r\n");
    return 0;
}
