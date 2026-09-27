/*
 * dev.c - device-specific part of the ARM-DOS GEM screen driver: the video
 * modes, the device tables, entering and leaving graphics, the escapes,
 * keyboard input and the colour map.
 *
 * Rewritten in C from the GEM/3 drivers' IBMBLMPC.A86 (IBM CGA),
 * HERCSPPC.A86 (Hercules), IBMMDVSP.A86 (escapes, input) and DEVDATA.IBM /
 * DEVDATA.HRC (tables) - Copyright 1985-1987 Digital Research, GNU GPL v2 via
 * Caldera 1999.  The keyboard mouse (cursor keys move the pointer, Home
 * clicks, End holds the button, Ctrl+Right Shift switches it on and off) is
 * the one of IBMMDVSP.
 */
#include "vdi.h"
#include <string.h>
#include <dos.h>
#include <armdos.h>

static VDEV devs[] = {
	/* name              xres yres xsize ysize planes colors mode hires font */
	{ "IBM CGA 640x200",  639, 199,  429,  945,  1,  2,  6, 0,  8 },
	{ "Hercules 720x348", 719, 347,  419,  508,  1,  2, -1, 1, 14 },
	{ "VGA 640x480",      639, 479,  372,  372,  8, 16, 0x62, 1, 16 },
};

VDEV	*dev = &devs[0];
WORD	dev_id = DEV_CGA;
WORD	dispmode = 1;			/* 1 = alpha mode */

WORD	DEV_TAB[45], SIZ_TAB[12], INQ_TAB[45], INQ_PTS[12];
WORD	REAL_COL[3][MAX_COLOR], REQ_COL[3][MAX_COLOR], MAP_COL[MAX_COLOR];
WORD	TERM_CH;

static UBYTE *rowtab[480];
static WORD text_mode = 3;

/* the 16-colour pixel values of GEM index 0..15, as the DRI EGA/VGA
 * drivers had them: white is pixel 0 and black pixel 15, so XOR (all bits)
 * swaps paper and ink like it does on the mono screens */
static const WORD vga_map[16] = { 0, 15, 1, 2, 4, 6, 3, 5, 7, 8, 9, 10, 12, 14, 11, 13 };

/* GEM's 16 standard colours (per mille) */
static const WORD gem_rgb[16][3] = {
	{ 1000, 1000, 1000 }, { 0, 0, 0 }, { 1000, 0, 0 }, { 0, 1000, 0 },
	{ 0, 0, 1000 }, { 0, 1000, 1000 }, { 1000, 1000, 0 }, { 1000, 0, 1000 },
	{ 666, 666, 666 }, { 333, 333, 333 }, { 666, 0, 0 }, { 0, 666, 0 },
	{ 0, 0, 666 }, { 0, 666, 666 }, { 666, 666, 0 }, { 666, 0, 666 },
};

static void int10(unsigned ax, unsigned bx)
{
	struct armregs r;
	memset(&r, 0, sizeof(r));
	r.r0 = ax;
	r.r1 = bx;
	_armdos_int10(&r);
}

/* a Hercules card: the BIOS found a mono adapter and the retrace bit of
 * 3BAh toggles (as apps/hercules checks it) */
static int hercules_present(void)
{
	volatile UWORD *equip = (volatile UWORD *)0x410;
	UBYTE first;
	ULONG t0;
	if ((*equip & 0x30) != 0x30 && ARMDOS_BDA[0x49] != 7)
		return 0;
	first = armdos_inb(0x3BA) & 0x80;
	t0 = ARMDOS_BIOS_TICKS;
	while (ARMDOS_BIOS_TICKS - t0 < 4)
		if ((armdos_inb(0x3BA) & 0x80) != first)
			return 1;
	return 0;
}

/* VGA mode 62h (ARM-PC extension, ARCH.md 6): 640x480, 256 colours,
 * linear at A0000h-EFFFFh? - see dev_open */
static int vga62_present(void)
{
	return armdos_vga_present() && (ARMDOS_BDA[0x49] != 7);
}

void dev_detect(int requested)
{
	if (requested < 0)
		requested = hercules_present() ? DEV_HGC : DEV_CGA;
	if (requested == DEV_HGC && !hercules_present())
		requested = DEV_CGA;
	if (requested == DEV_VGA && !vga62_present())
		requested = DEV_CGA;
	dev_id = requested;
	dev = &devs[requested];
}

/* the tables of DEVDATA.IBM / .HRC, with this device's numbers */
void dev_tables(void)
{
	static const WORD dev_tab[45] = {
		639, 199, 0, 429, 945, 0, 8, 40, 8, 8,
		0, 24, 12, 2, 10, 1, 2, 3, 4, 5,
		6, 7, 8, 9, 10, 3, 0, 3, 3, 3,
		0, 3, 0, 3, 2, 0, 1, 1, 0, 2,
		2, 1, 1, 1, 2
	};
	static const WORD siz_tab[12] = { 0, 0, 0, 0, 1, 0, 40, 0, 15, 11, 120, 88 };
	static const WORD inq_tab[20] = {
		4, 1, 1, 0, 1, 0, 50, 0, 1, 4, 2, 1, 0, 0,
		PTSIN_SIZE / 2 - 1, INTIN_SIZE, 3, 0, 0, 0
	};
	int i;

	memcpy(DEV_TAB, dev_tab, sizeof(dev_tab));
	memcpy(SIZ_TAB, siz_tab, sizeof(siz_tab));
	memset(INQ_TAB, 0, sizeof(INQ_TAB));
	memcpy(INQ_TAB, inq_tab, sizeof(inq_tab));
	DEV_TAB[0] = dev->d_xres;
	DEV_TAB[1] = dev->d_yres;
	DEV_TAB[3] = dev->d_xsize;
	DEV_TAB[4] = dev->d_ysize;
	DEV_TAB[13] = dev->colors;
	/* planes, as GEM sees them: 4 for 16 colours (the screen itself
	 * holds a byte per pixel; memory forms are 4 bits per pixel) */
	INQ_TAB[4] = dev->colors > 2 ? 4 : 1;
	if (dev->colors > 2)
	{
		DEV_TAB[35] = 1;		/* colour capability */
		DEV_TAB[39] = 0;		/* palette: > 32767 */
		INQ_TAB[5] = 1;			/* lookup table */
	}
	for (i = 0; i < MAX_COLOR; i++)
	{
		int k;
		for (k = 0; k < 3; k++)
		{
			REQ_COL[k][i] = gem_rgb[i][k];
			REAL_COL[k][i] = dev->colors > 2 ? gem_rgb[i][k] : (i ? 0 : 1000);
		}
		MAP_COL[i] = dev->colors > 2 ? vga_map[i] : (i ? 1 : 0);
	}
	pattern_init(dev->hires);
	fonts_for(dev->font_h);
}

/* ------------------------------------------------------- the modes ---- */

static void hgc_graphics(void)
{
	static const UBYTE gtab[12] = { 0x35, 0x2D, 0x2E, 0x07, 0x5B, 0x02, 0x57, 0x57, 0x02, 0x03, 0x00, 0x00 };
	int i;
	armdos_outb(0x3BF, 0x03);
	armdos_outb(0x3B8, 0x02);
	for (i = 0; i < 12; i++)
	{
		armdos_outb(0x3B4, i);
		armdos_outb(0x3B5, gtab[i]);
	}
	armdos_outb(0x3B8, 0x0A);
}

static void hgc_text(void)
{
	armdos_outb(0x3B8, 0x20);
	armdos_outb(0x3BF, 0x00);
}

static void set_dac(int i, int r, int g, int b)
{
	armdos_outb(0x3C8, i);
	armdos_outb(0x3C9, r * 63 / 1000);
	armdos_outb(0x3C9, g * 63 / 1000);
	armdos_outb(0x3C9, b * 63 / 1000);
}

int dev_open(int id)
{
	int yy;
	(void)id;
	screen.w = dev->d_xres + 1;
	screen.h = dev->d_yres + 1;
	screen.planes = dev->planes;
	screen.rows = rowtab;
	screen.addr = 0;
	screen.wb = 0;
	switch (dev_id)
	{
	case DEV_HGC:
		text_mode = 7;
		hgc_graphics();
		for (yy = 0; yy < screen.h; yy++)
			rowtab[yy] = (UBYTE *)0xB0000 + 0x2000 * (yy & 3) + 90 * (yy >> 2);
		screen.inv = 0xFF;
		break;
	case DEV_VGA:
		text_mode = 3;
		int10(0x0062, 0);
		screen.addr = (UBYTE *)0xA0000;
		for (yy = 0; yy < screen.h; yy++)
			rowtab[yy] = (UBYTE *)0xA0000 + 640 * yy;
		screen.inv = 0;
		for (yy = 0; yy < 16; yy++)
			set_dac(vga_map[yy], gem_rgb[yy][0], gem_rgb[yy][1], gem_rgb[yy][2]);
		break;
	default:
		text_mode = 3;
		int10(0x0006, 0);
		armdos_outb(0x3D9, 0x3F);	/* white foreground, as the IBM BIOS */
		for (yy = 0; yy < screen.h; yy++)
			rowtab[yy] = (UBYTE *)0xB8000 + 0x2000 * (yy & 1) + 80 * (yy >> 1);
		screen.inv = 0xFF;
		break;
	}
	return 1;
}

void dev_close(void)
{
	if (dev_id == DEV_HGC)
		hgc_text();
	int10(text_mode, 0);
}

/* escape 2: enter graphics (also the end of v_opnwk) */
VOID INIT_G(VOID)
{
	if (!dispmode)
		return;
	dev_open(dev_id);
	dispmode = 0;
	HIDE_CNT = 1;
	CLEARMEM();
	mouse_default_form();
	mouse_init();
}

/* escape 3 / v_clswk: back to the text screen */
VOID DINIT_G(VOID)
{
	if (dispmode)
		return;
	mouse_exit();
	HIDE_CNT = 1;
	dispmode = 1;
	dev_close();
}

/* ---------------------------------------------------------- sound ---- */
static volatile WORD snd_ticks;
static WORD snd_mute;

void sound_tick(void)
{
	if (snd_ticks > 0 && --snd_ticks == 0)
		armdos_outb(0x61, armdos_inb(0x61) & ~3);
}

static void sound(int freq, int ticks)
{
	unsigned div;
	if (snd_mute || freq <= 0)
		return;
	div = 1193182 / freq;
	armdos_outb(0x43, 0xB6);
	armdos_outb(0x42, div & 0xFF);
	armdos_outb(0x42, div >> 8);
	armdos_outb(0x61, armdos_inb(0x61) | 3);
	snd_ticks = ticks > 0 ? ticks : 1;
}

/* ------------------------------------------------------- escapes ---- */

static void cursor_get(int *row, int *col)
{
	struct armregs r;
	memset(&r, 0, sizeof(r));
	r.r0 = 0x0300;
	_armdos_int10(&r);
	*row = (r.r3 >> 8) & 0xFF;
	*col = r.r3 & 0xFF;
}

static void cursor_set(int row, int col)
{
	struct armregs r;
	memset(&r, 0, sizeof(r));
	r.r0 = 0x0200;
	r.r3 = (row << 8) | col;
	_armdos_int10(&r);
}

static UBYTE txt_attr = 0x07;

static void put_chars(int ch, int n)
{
	struct armregs r;
	memset(&r, 0, sizeof(r));
	r.r0 = 0x0900 | (ch & 0xFF);
	r.r1 = txt_attr;
	r.r2 = n;
	_armdos_int10(&r);
}

VOID CHK_ESC(VOID)
{
	int row, col, i;
	int rows = 25, cols = 80;

	switch (CONTRL[5])
	{
	case 1:		/* vq_chcells */
		INTOUT[0] = rows;
		INTOUT[1] = cols;
		CONTRL[4] = 2;
		break;
	case 2:		/* v_exit_cur: graphics */
		INIT_G();
		break;
	case 3:		/* v_enter_cur: text */
		DINIT_G();
		break;
	case 4: cursor_get(&row, &col); if (row > 0) cursor_set(row - 1, col); break;
	case 5: cursor_get(&row, &col); if (row < rows - 1) cursor_set(row + 1, col); break;
	case 6: cursor_get(&row, &col); if (col < cols - 1) cursor_set(row, col + 1); break;
	case 7: cursor_get(&row, &col); if (col > 0) cursor_set(row, col - 1); break;
	case 8: cursor_set(0, 0); break;
	case 9:		/* erase to end of screen */
		cursor_get(&row, &col);
		put_chars(' ', (rows - row) * cols - col);
		break;
	case 10:	/* erase to end of line */
		cursor_get(&row, &col);
		put_chars(' ', cols - col);
		break;
	case 11:	/* direct cursor address (1-based) */
		cursor_set(INTIN[0] - 1, INTIN[1] - 1);
		break;
	case 12:	/* output cursor-addressable text */
		for (i = 0; i < CONTRL[3]; i++)
		{
			cursor_get(&row, &col);
			put_chars(INTIN[i], 1);
			if (col < cols - 1)
				cursor_set(row, col + 1);
		}
		break;
	case 13: txt_attr = 0x70; break;
	case 14: txt_attr = 0x07; break;
	case 15:	/* inquire cursor address */
		cursor_get(&row, &col);
		INTOUT[0] = row + 1;
		INTOUT[1] = col + 1;
		CONTRL[4] = 2;
		break;
	case 16:	/* tablet status: a mouse */
		INTOUT[0] = 1;
		CONTRL[4] = 1;
		break;
	case 18:	/* place graphic cursor */
		GCURX = PTSIN[0];
		GCURY = PTSIN[1];
		HIDE_CNT = 1;
		DIS_CUR();
		break;
	case 19:	/* remove graphic cursor */
		HIDE_CUR();
		break;
	case 61:	/* sound: INTIN[0] Hz, INTIN[1] ticks */
		sound(INTIN[0], INTIN[1]);
		break;
	case 62:	/* mute */
		if (INTIN[0] != -1)
			snd_mute = INTIN[0];
		INTOUT[0] = snd_mute;
		CONTRL[4] = 1;
		break;
	default:
		break;
	}
}

/* ------------------------------------------------------- colour ----- */
VOID S_COLMAP(VOID)
{
	int i = INTIN[0], k;
	if (i < 0 || i >= dev->colors || i >= MAX_COLOR)
		return;
	for (k = 0; k < 3; k++)
	{
		int v = INTIN[1 + k];
		if (v < 0) v = 0;
		if (v > 1000) v = 1000;
		REQ_COL[k][i] = v;
		REAL_COL[k][i] = dev->colors > 2 ? (v * 63 / 1000) * 1000 / 63 : REAL_COL[k][i];
	}
	if (dev->colors > 2)
		set_dac(MAP_COL[i], REQ_COL[0][i], REQ_COL[1][i], REQ_COL[2][i]);
}

VOID I_COLMAP(VOID)
{
	int i = INTIN[0], k;
	if (i < 0 || i >= dev->colors || i >= MAX_COLOR)
	{
		INTOUT[0] = -1;
		return;
	}
	INTOUT[0] = INTIN[0];
	for (k = 0; k < 3; k++)
		INTOUT[1 + k] = INTIN[1] ? REAL_COL[k][i] : REQ_COL[k][i];
}

/* ------------------------------------------------------- keyboard ---- */

static UBYTE kbd_mouse = 0xFF, last_ctl, ctl_status;

/* scan code (| 80h shifted), button (| 80h sticky), dx, dy */
static const signed char kbd_mouse_tbl[11][4] = {
	{ 0x47, 1, 0, 0 }, { (signed char)0xC7, 1, 0, 0 }, { 0x4F, (signed char)0x81, 0, 0 },
	{ 0x48, 0, 0, -8 }, { (signed char)0xC8, 0, 0, -1 },
	{ 0x4B, 0, -8, 0 }, { (signed char)0xCB, 0, -1, 0 },
	{ 0x4D, 0, 8, 0 }, { (signed char)0xCD, 0, 1, 0 },
	{ 0x50, 0, 0, 8 }, { (signed char)0xD0, 0, 0, 1 },
};

static int bios16(unsigned ax, unsigned *out)
{
	struct armregs r;
	memset(&r, 0, sizeof(r));
	r.r0 = ax;
	_armdos_int16(&r);
	*out = r.r0 & 0xFFFF;
	return (r.cpsr & ARM_CPSR_Z) ? 0 : 1;
}

WORD GSHIFT_S(VOID)
{
	unsigned v;
	bios16(0x0200, &v);
	return v & 0x0F;
}

/* GCHR_KEY: 1 = a key in TERM_CH, 0 = nothing */
WORD GCHR_KEY(VOID)
{
	unsigned v, sh;
	int got = 0, i;

	if (bios16(0x0100, &v))
	{
		bios16(0x0000, &v);
		TERM_CH = v;
		ctl_status = 0;
		got = 1;
	}
	sh = GSHIFT_S();
	/* Ctrl + Right Shift, pressed and released alone: keyboard mouse on/off */
	{
		UBYTE c = ((sh & 5) == 5) ? 5 : 0;
		if (c != last_ctl)
		{
			last_ctl = c;
			if (c)
			{
				if (!got)
					ctl_status = 0xFF;
			}
			else if (ctl_status)
			{
				kbd_mouse = ~kbd_mouse;
				ctl_status = 0;
				sound(2000, 2);
				if (!kbd_mouse && MOUSE_BT)
					keyboard_mouse(0, 0, 0);
			}
		}
	}
	if (kbd_mouse && got)
	{
		int sc = (TERM_CH >> 8) & 0xFF;
		if (sh & 3)
			sc |= 0x80;
		for (i = 0; i < 11; i++)
			if ((UBYTE)kbd_mouse_tbl[i][0] == sc)
			{
				int b = (UBYTE)kbd_mouse_tbl[i][1];
				keyboard_mouse(b & 0x0F, kbd_mouse_tbl[i][2], kbd_mouse_tbl[i][3]);
				if (!(b & 0x80) && (b & 0x0F))
					keyboard_mouse(0, 0, 0);
				return 0;
			}
	}
	return got;
}

WORD GLOC_KEY(VOID)
{
	if (GCHR_KEY())
		return 1;
	return 0;
}

WORD GCHC_KEY(VOID)
{
	TERM_CH = 1;
	return 1;
}
