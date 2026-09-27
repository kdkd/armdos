/* modetest.c - INT 10h mode 62h (the ARM-PC's 640x480 256-colour linear
 * mode, ARCH.md 6) through the BIOS: set it, write and read pixels with
 * AH=0Ch/0Dh and directly, teletype text in it (AH=0Eh, 8x16 cells, 30
 * rows, scrolling), the state (AH=0Fh, AH=1Bh), then back to text mode
 * and report.  With a parameter it waits for a key in mode 62h (so a test
 * can look at the screen). */
#include <armdos.h>
#include <conio.h>
#include <stdio.h>
#include <string.h>

static unsigned int10(unsigned ax, unsigned bx, unsigned cx, unsigned dx, struct armregs *out)
{
	struct armregs r;
	memset(&r, 0, sizeof r);
	r.r0 = ax; r.r1 = bx; r.r2 = cx; r.r3 = dx;
	_armdos_int10(&r);
	if (out)
		*out = r;
	return r.r0 & 0xFFFF;
}

int main(int argc, char **argv)
{
	volatile unsigned char *fb = (volatile unsigned char *)0xA0000;
	unsigned char state[64];
	struct armregs r;
	unsigned mode, px1, px2, px3, rows, cell, top_after;
	int i, x, y;

	int10(0x0062, 0, 0, 0, 0);
	mode = int10(0x0F00, 0, 0, 0, 0) & 0xFF;
	rows = ARMDOS_BDA[0x84] + 1;
	cell = ARMDOS_BDA[0x85];
	/* pixels through the BIOS, at the corners (the last one is in the
	 * former adapter hole, above C0000h) */
	int10(0x0C04, 0, 0, 0, 0);
	int10(0x0C0E, 0, 639, 479, 0);
	px1 = int10(0x0D00, 0, 639, 479, 0) & 0xFF;
	px2 = fb[479 * 640 + 639];
	/* and directly: a colour bar of the 256 DAC entries */
	for (y = 200; y < 260; y++)
		for (x = 0; x < 512; x++)
			fb[y * 640 + 64 + x] = x / 2;
	px3 = int10(0x0D00, 0, 64 + 2 * 200, 230, 0) & 0xFF;
	memset(state, 0, sizeof state);
	memset(&r, 0, sizeof r);
	r.r0 = 0x1B00; r.r1 = 0; r.r5 = (unsigned)state;
	_armdos_int10(&r);
	/* teletype: 32 lines so that it scrolls twice */
	for (i = 0; i < 32; i++) {
		char line[40];
		char *p = line;
		sprintf(line, "Mode 62h line %d\r\n", i);
		while (*p)
			int10(0x0E00 | (unsigned char)*p++, 0x000F, 0, 0, 0);
	}
	/* after scrolling (33 lines on 30 rows: 3 times), the colour bar moved up 48 lines */
	top_after = fb[(230 - 48) * 640 + 64 + 2 * 200];
	if (argc > 1)
		getch();
	int10(0x0003, 0, 0, 0, 0);
	printf("MODETEST mode %02X rows %u cell %u\n", mode, rows, cell);
	printf("MODETEST pixels bios %u direct %u bar %u scrolled %u\n", px1, px2, px3, top_after);
	printf("MODETEST 1Bh al %02X mode %02X colours %u scanlines %u\n",
		(unsigned)(r.r0 & 0xFF), state[4], state[0x27] | (state[0x28] << 8), state[0x2A]);
	return 0;
}
