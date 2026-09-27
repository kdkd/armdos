/*
 * vditest.c - exercise the ARM-DOS GEM VDI (GEMVDI.EXE) without the AES.
 *   GEMVDI VDITEST.EXE [key]
 * Opens the screen workstation, draws a test picture, waits for a key
 * (or for "go" if run with an argument, for the headless tests), closes.
 */
#include <stdio.h>
#include <string.h>
#include <conio.h>

typedef short WORD;
static WORD contrl[12], intin[256], ptsin[512], intout[128], ptsout[128];
static void *pb[5] = { contrl, intin, ptsin, intout, ptsout };
static WORD handle;

static void vdi(WORD op, WORD np, WORD ni)
{
	register void *r3 __asm__("r3") = pb;
	register unsigned r2 __asm__("r2") = 0x473;
	contrl[0] = op; contrl[1] = np; contrl[3] = ni; contrl[6] = handle;
	__asm__ volatile("svc #0xEF" : : "r"(r2), "r"(r3) : "memory", "cc");
}

static void s(WORD op, WORD v) { intin[0] = v; vdi(op, 0, 1); }

static void text(int x, int y, const char *t)
{
	int n = 0;
	while (*t) intin[n++] = (unsigned char)*t++;
	ptsin[0] = x; ptsin[1] = y;
	vdi(8, 1, n);
}

static void box(int x1, int y1, int x2, int y2)
{
	ptsin[0] = x1; ptsin[1] = y1; ptsin[2] = x2; ptsin[3] = y2;
	vdi(114, 2, 0);
}

int main(int argc, char **argv)
{
	int i, xres, yres, ch_w, ch_h;
	for (i = 0; i < 10; i++) intin[i] = 1;
	intin[10] = 2;
	vdi(1, 0, 11);
	handle = contrl[6];
	xres = intout[0]; yres = intout[1];
	printf("");
	/* extended inquire */
	s(102, 1);
	/* text size */
	ptsin[0] = 0; ptsin[1] = 0; vdi(12, 1, 0);	/* vst_height smallest */
	ptsin[0] = 0; ptsin[1] = 6; vdi(12, 1, 0);
	ch_w = ptsout[2]; ch_h = ptsout[3];
	/* frame */
	s(23, 0);		/* hollow */
	s(32, 1);
	ptsin[0] = 0; ptsin[1] = 0; ptsin[2] = xres; ptsin[3] = 0; ptsin[4] = xres; ptsin[5] = yres;
	ptsin[6] = 0; ptsin[7] = yres; ptsin[8] = 0; ptsin[9] = 0;
	vdi(6, 5, 0);
	/* patterned boxes: the 24 patterns */
	s(23, 2); s(25, 1);
	for (i = 1; i <= 24; i++) {
		int x = 8 + ((i - 1) % 12) * 40, y = 20 + ((i - 1) / 12) * 30;
		s(24, i);
		box(x, y, x + 34, y + 24);
	}
	/* hatches */
	s(23, 3);
	for (i = 1; i <= 12; i++) {
		int x = 8 + (i - 1) * 40, y = 84;
		s(24, i);
		box(x, y, x + 34, y + 20);
	}
	/* text with effects */
	s(23, 1);
	ptsin[0] = 0; ptsin[1] = 7; vdi(12, 1, 0);		/* 10 point: the 8x8 face (top + 1) */
	text(8, 124, "GEM VDI on ARM-DOS");
	s(106, 1); text(166, 124, "Bold");
	s(106, 2); text(206, 124, "Light");
	s(106, 4); text(254, 124, "Italic");
	s(106, 8); text(310, 124, "Under");
	s(106, 16); text(360, 124, "Outline");
	s(106, 0);
	ptsin[0] = 0; ptsin[1] = 14; vdi(12, 1, 0);		/* doubled */
	text(230, 176, "Scaled x2");
	/* small font */
	ptsin[0] = 0; ptsin[1] = 5; vdi(12, 1, 0);
	text(8, 138, "The small system font: 6x6. ABCDEFGHIJKLMNOPQRSTUVWXYZ 0123456789");
	ptsin[0] = 0; ptsin[1] = 7; vdi(12, 1, 0);
	/* lines */
	for (i = 0; i < 7; i++) {
		s(15, i + 1);
		ptsin[0] = 500; ptsin[1] = 20 + i * 8; ptsin[2] = 620; ptsin[3] = 20 + i * 8;
		vdi(6, 2, 0);
	}
	s(15, 1);
	ptsin[0] = 3; ptsin[1] = 0; vdi(16, 1, 0);	/* width 3 */
	ptsin[0] = 500; ptsin[1] = 90; ptsin[2] = 560; ptsin[3] = 140; ptsin[4] = 620; ptsin[5] = 90;
	vdi(6, 3, 0);
	ptsin[0] = 1; ptsin[1] = 0; vdi(16, 1, 0);
	/* circle and rounded box (GDP) */
	s(23, 2); s(24, 4);
	ptsin[0] = 560; ptsin[1] = 170; ptsin[2] = 0; ptsin[3] = 0; ptsin[4] = 20; ptsin[5] = 0;
	contrl[5] = 4; vdi(11, 3, 0);
	ptsin[0] = 440; ptsin[1] = 150; ptsin[2] = 520; ptsin[3] = 190;
	contrl[5] = 9; vdi(11, 2, 0);
	/* XOR text */
	s(32, 3);
	box(8, 150, 200, 175);
	s(32, 1);
	/* the 16 colours (on a mono screen: white, then black) */
	s(23, 1);
	for (i = 0; i < 16; i++) {
		s(25, i);
		box(8 + i * 12, 180, 8 + i * 12 + 10, 196);
	}
	s(25, 1);
	/* the mouse */
	intin[0] = 0; vdi(122, 0, 1);
	if (argc > 1) {
		printf("");
		while (!kbhit()) ;
		getch();
	} else
		getch();
	vdi(2, 0, 0);
	printf("VDITEST: %dx%d, cell %dx%d\n", xres + 1, yres + 1, ch_w, ch_h);
	return 0;
}
