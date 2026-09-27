/* MODE13.C - 320x200 in 256 colours, through the video BIOS.
 *
 * int86(0x10, ...) is INT 10h, exactly as on a PC: on ARM-DOS it becomes
 * an ARM "svc #0x10" with AX..DX in r0..r3. The frame buffer is at
 * 0xA0000 as always - but it is a plain pointer: no segments, no "far".
 * Press any key to stop the palette cycling.
 */
#include <dos.h>
#include <conio.h>
#include <stdio.h>

#define W 320
#define H 200

static void set_mode(int mode)
{
    union REGS r;
    r.h.ah = 0x00;              /* set video mode */
    r.h.al = mode;
    int86(0x10, &r, &r);
}

static void set_colour(int i, int red, int green, int blue)
{
    outp(0x3C8, i);             /* DAC write index, then R, G, B (0-63) */
    outp(0x3C9, red);
    outp(0x3C9, green);
    outp(0x3C9, blue);
}

static void wait_retrace(void)
{
    while (inp(0x3DA) & 8)      /* wait for the end of the current retrace */
        ;
    while (!(inp(0x3DA) & 8))   /* ... and the start of the next one */
        ;
}

/* a smooth 64-step ramp through red, yellow, green, cyan, blue, magenta */
static int ramp(int i)
{
    i &= 255;
    if (i < 64) return i;
    if (i < 128) return 63;
    if (i < 192) return 191 - i;
    return 0;
}

int main(void)
{
    unsigned char *vga = (unsigned char *)0xA0000;
    int x, y, shift = 0;

    set_mode(0x13);
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            int dx = x - W / 2, dy = y - H / 2;
            /* rings around the centre, crossed by an XOR pattern */
            vga[y * W + x] = ((dx * dx + dy * dy) >> 5) + ((x ^ y) >> 2);
        }

    while (!kbhit()) {
        int i;
        wait_retrace();
        for (i = 1; i < 256; i++) {
            int c = (i + shift) * 3;
            set_colour(i, ramp(c), ramp(c + 85), ramp(c + 170));
        }
        shift++;
    }
    getch();

    set_mode(0x03);
    printf("Back in text mode after %d palette cycles.\n", shift);
    return 0;
}
