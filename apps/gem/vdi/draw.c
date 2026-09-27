/*
 * draw.c - drawing primitives of the ARM-DOS GEM screen driver.
 *
 * Rewritten in C for ARM from the GEM/3 screen driver's x86 assembler
 * (MONMMRE1.A86 / MONMMRE2.A86: ABLINE, HABLINE, BOX_FILL, RECTFILL,
 * CLC_FLIT; IBMMDVSP.A86: CLEARMEM; Copyright 1987 Digital Research, GNU GPL
 * v2 via Caldera 1999).  Same entry conventions: the coordinates come in
 * X1/Y1/X2/Y2, the colour in FG_BP_1, the mode in WRT_MODE, the line style
 * in LN_MASK, the fill pattern in patptr/patmsk/NEXT_PAT.
 *
 * Colours are GEM colour indices mapped through MAP_COL; on the mono
 * screens 1 = ink (black), 0 = paper (white).  Writing modes (WRT_MODE):
 * 0 replace, 1 transparent, 2 XOR, 3 reverse transparent.
 */
#include "vdi.h"

SURF	screen;

/* XOR value that inverts a pixel in XOR mode */
int dev_xor_mask(void)
{
	return screen.planes == 1 ? 1 : (dev->colors - 1);
}

static inline UBYTE *rowp(SURF *s, int y)
{
	return SROW(s, y);
}

/* one horizontal span on row y, x1..x2 inclusive (already clipped to the
 * surface), pattern word pat (bit 15 = pixel x%16 == 0) */
void surf_hspan(SURF *s, int y, int x1, int x2, UWORD pat, int mode, int color)
{
	UBYTE *p = rowp(s, y);

	if (x1 > x2)
		return;
	if (s->planes == 1)
	{
		UBYTE inv = s->inv;
		int b1 = x1 >> 3, b2 = x2 >> 3, b;
		UBYTE pb[2];
		pb[0] = pat >> 8;
		pb[1] = pat & 0xFF;
		for (b = b1; b <= b2; b++)
		{
			UBYTE m = 0xFF, pt = pb[b & 1], d;
			if (b == b1)
				m &= 0xFF >> (x1 & 7);
			if (b == b2)
				m &= 0xFF << (7 - (x2 & 7));
			d = p[b] ^ inv;
			switch (mode)
			{
			case 0:	/* replace: pattern bits in colour, others paper */
				d = (d & ~m) | ((color ? pt : 0) & m);
				break;
			case 1:	/* transparent */
				if (color)
					d |= pt & m;
				else
					d &= ~(pt & m);
				break;
			case 2:	/* xor */
				d ^= pt & m;
				break;
			default: /* reverse transparent */
				if (color)
					d |= ~pt & m;
				else
					d &= ~(~pt & m);
				break;
			}
			p[b] = d ^ inv;
		}
	}
	else
	{
		int x, xm = dev_xor_mask();
		for (x = x1; x <= x2; x++)
		{
			int on = (pat >> (15 - (x & 15))) & 1;
			switch (mode)
			{
			case 0: p[x] = on ? color : 0; break;
			case 1: if (on) p[x] = color; break;
			case 2: if (on) p[x] ^= xm; break;
			default: if (!on) p[x] = color; break;
			}
		}
	}
}

/* one pixel: on = the style/pattern bit */
void surf_pixel(SURF *s, int x, int y, int on, int mode, int color)
{
	UBYTE *p;

	if (x < 0 || y < 0 || x >= s->w || y >= s->h)
		return;
	p = rowp(s, y);
	if (s->planes == 1)
	{
		UBYTE m = 0x80 >> (x & 7), d = p[x >> 3] ^ s->inv;
		switch (mode)
		{
		case 0: if (on && color) d |= m; else d &= ~m; break;
		case 1: if (on) { if (color) d |= m; else d &= ~m; } break;
		case 2: if (on) d ^= m; break;
		default: if (!on) { if (color) d |= m; else d &= ~m; } break;
		}
		p[x >> 3] = d ^ s->inv;
	}
	else
	{
		switch (mode)
		{
		case 0: p[x] = on ? color : 0; break;
		case 1: if (on) p[x] = color; break;
		case 2: if (on) p[x] ^= dev_xor_mask(); break;
		default: if (!on) p[x] = color; break;
		}
	}
}

int surf_get(SURF *s, int x, int y)
{
	UBYTE *p = rowp(s, y);
	if (s->planes == 1)
		return ((p[x >> 3] ^ s->inv) >> (7 - (x & 7))) & 1;
	return p[x];
}

/* ------------------------------------------------------------- lines ---- */

static inline UWORD rol16(UWORD v)
{
	return (UWORD)((v << 1) | (v >> 15));
}

/* ABLINE: line from (X1,Y1) to (X2,Y2) in FG_BP_1 with LN_MASK, WRT_MODE.
 * The style mask rotates one bit per pixel and keeps its phase from one
 * call to the next (a polyline continues its pattern).  In XOR mode the
 * last pixel is left out unless LSTLIN is set, so that the joints of a
 * polyline are not inverted twice (the "TENNIS" routine of MONMMRE1). */
VOID ABLINE(VOID)
{
	int x1 = X1, y1 = Y1, x2 = X2, y2 = Y2;
	int dx, dy, sx, sy, err, n, i;
	int color = FG_BP_1, mode = WRT_MODE;
	UWORD mask = LN_MASK;

	/* draw left to right like the original (the style starts at x1) */
	if (x1 > x2)
	{
		int t;
		t = x1; x1 = x2; x2 = t;
		t = y1; y1 = y2; y2 = t;
	}
	dx = x2 - x1;
	dy = y2 - y1;
	sy = dy < 0 ? -1 : 1;
	if (dy < 0)
		dy = -dy;
	sx = 1;
	n = (dx > dy ? dx : dy) + 1;
	if (mode == 2 && !LSTLIN && n > 1)
		n--;
	if (dy == 0 && mask == 0xFFFF && y1 >= 0 && y1 < screen.h)
	{
		/* horizontal solid line: one span */
		int a = x1, b = x1 + n - 1;
		if (a < 0) a = 0;
		if (b >= screen.w) b = screen.w - 1;
		if (a <= b)
			surf_hspan(&screen, y1, a, b, 0xFFFF, mode, color);
		return;
	}
	if (dx >= dy)
	{
		err = 2 * dy - dx;
		for (i = 0; i < n; i++)
		{
			mask = rol16(mask);
			surf_pixel(&screen, x1, y1, mask & 1, mode, color);
			if (err >= 0)
			{
				y1 += sy;
				err -= 2 * dx;
			}
			err += 2 * dy;
			x1 += sx;
		}
	}
	else
	{
		err = 2 * dx - dy;
		for (i = 0; i < n; i++)
		{
			mask = rol16(mask);
			surf_pixel(&screen, x1, y1, mask & 1, mode, color);
			if (err >= 0)
			{
				x1 += sx;
				err -= 2 * dy;
			}
			err += 2 * dx;
			y1 += sy;
		}
	}
	LN_MASK = mask;
}

/* ------------------------------------------------------------- fills ---- */

/* BOX_FILL: X1,Y1 .. X2,Y2 inclusive with the fill pattern, not clipped
 * except to the screen */
static VOID box_fill(VOID)
{
	int x1 = X1, x2 = X2, y1 = Y1, y2 = Y2, yy;
	WORD *pat = patptr;
	WORD msk = patmsk;
	int color = FG_BP_1;

	if (x1 > x2) { int t = x1; x1 = x2; x2 = t; }
	if (y1 > y2) { int t = y1; y1 = y2; y2 = t; }
	if (x1 < 0) x1 = 0;
	if (y1 < 0) y1 = 0;
	if (x2 >= screen.w) x2 = screen.w - 1;
	if (y2 >= screen.h) y2 = screen.h - 1;
	if (x1 > x2 || y1 > y2)
		return;
	for (yy = y1; yy <= y2; yy++)
	{
		UWORD pw = (UWORD)pat[yy & msk];
		if (screen.planes > 1 && NEXT_PAT)
		{
			/* multi-plane user pattern: build the colour per pixel */
			int x, pl, planes = udpt_np ? udpt_np : 1;
			UBYTE *p = rowp(&screen, yy);
			for (x = x1; x <= x2; x++)
			{
				int c = 0, bit = 15 - (x & 15);
				for (pl = 0; pl < planes && pl < 8; pl++)
					c |= (((UWORD)pat[(yy & msk) + pl * 16] >> bit) & 1) << pl;
				switch (WRT_MODE)
				{
				case 0: p[x] = c; break;
				case 1: if (c) p[x] = c; break;
				case 2: p[x] ^= c; break;
				default: if (!c) p[x] = color; break;
				}
			}
		}
		else
			surf_hspan(&screen, yy, x1, x2, pw, WRT_MODE, color);
	}
}

VOID HABLINE(VOID)
{
	WORD sy2 = Y2;
	Y2 = Y1;
	box_fill();
	Y2 = sy2;
}

/* RECTFILL: box fill, clipped when CLIP is on */
VOID RECTFILL(VOID)
{
	if (CLIP)
	{
		if (X1 < XMN_CLIP)
		{
			if (X2 < XMN_CLIP)
				return;
			X1 = XMN_CLIP;
		}
		if (X2 > XMX_CLIP)
		{
			if (X1 > XMX_CLIP)
				return;
			X2 = XMX_CLIP;
		}
		if (Y1 < YMN_CLIP)
		{
			if (Y2 < YMN_CLIP)
				return;
			Y1 = YMN_CLIP;
		}
		if (Y2 > YMX_CLIP)
		{
			if (Y1 > YMX_CLIP)
				return;
			Y2 = YMX_CLIP;
		}
	}
	box_fill();
}

static VOID hline_clip(VOID)
{
	if (CLIP)
	{
		if (X1 < XMN_CLIP)
		{
			if (X2 < XMN_CLIP)
				return;
			X1 = XMN_CLIP;
		}
		if (X2 > XMX_CLIP)
		{
			if (X1 > XMX_CLIP)
				return;
			X2 = XMX_CLIP;
		}
	}
	HABLINE();
}

/* CLC_FLIT: fill the polygon PTSIN[0..CONTRL[1]] (closed by plygn) on
 * scan line Y1: intersections as the x86 code computes them, sorted, drawn
 * in pairs. */
VOID CLC_FLIT(VOID)
{
	static WORD xs[PTSIN_SIZE / 2];
	int n = 0, i, cnt = CONTRL[1];
	WORD *pt = PTSIN;

	for (i = 0; i < cnt; i++, pt += 2)
	{
		int ya = pt[1], yb = pt[3];
		int dyy = yb - ya, d1, d2, ddx, q;
		if (dyy == 0)
			continue;
		d1 = Y1 - ya;
		d2 = Y1 - yb;
		if ((d1 ^ d2) >= 0)	/* same sign: no intersection */
			continue;
		ddx = (pt[2] - pt[0]) * 2;
		q = (ddx * d1) / dyy;
		if (q >= 0)
			q = (q + 1) >> 1;
		else
			q = -((-q + 1) >> 1);
		if (n < (int)(sizeof(xs) / sizeof(xs[0])))
			xs[n++] = pt[0] + q;
	}
	if (n < 2)
		return;
	/* insertion sort */
	for (i = 1; i < n; i++)
	{
		WORD v = xs[i];
		int j = i - 1;
		while (j >= 0 && xs[j] > v)
		{
			xs[j + 1] = xs[j];
			j--;
		}
		xs[j + 1] = v;
	}
	for (i = 0; i + 1 < n; i += 2)
	{
		X1 = xs[i];
		X2 = xs[i + 1];
		hline_clip();
	}
}

/* ------------------------------------------------------------ clear ---- */
VOID CLEARMEM(VOID)
{
	int yy;
	HIDE_CUR();
	for (yy = 0; yy < screen.h; yy++)
		surf_hspan(&screen, yy, 0, screen.w - 1, 0xFFFF, 0, 0);
	DIS_CUR();
}

/* ------------------------------------------------------------- misc ---- */

/* ((m1 * m2) / d1) rounded, as MONMMRE1's SMUL_DIV */
WORD SMUL_DIV(WORD m1, WORD m2, WORD d1)
{
	LONG prod = (LONG)m1 * (LONG)m2, q, r;
	int s = 1;
	if (d1 == 0)
		return prod < 0 ? -32767 : 32767;
	q = prod / d1;
	r = prod % d1;
	if (prod < 0)
		s = -s;
	if (d1 < 0)
	{
		s = -s;
		d1 = -d1;
	}
	if (r < 0)
		r = -r;
	if (2 * r >= d1)
		q += s;
	return (WORD)q;
}

/* integer length of the vector (dx, dy) */
WORD vec_len(WORD dx, WORD dy)
{
	ULONG sq = (LONG)dx * dx + (LONG)dy * dy, lo, hi;
	if (!sq)
		return 0;
	lo = 0;
	hi = 46341;
	while (hi - lo > 1)
	{
		ULONG mid = (lo + hi) / 2;
		if (mid * mid > sq)
			hi = mid;
		else
			lo = mid;
	}
	return (WORD)lo;
}
