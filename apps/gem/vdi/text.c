/*
 * text.c - text output of the ARM-DOS GEM screen driver (TEXT_BLT and the
 * font helpers that opttext.c calls).
 *
 * Rewritten in C from the GEM/3 driver's OPTTXT1.A86, OPTTXT2.A86,
 * OPTTDRAW.A86 and MONSPBLT.A86 (Copyright 1987 Digital Research; GNU GPL v2
 * via Caldera 1999).  The x86 code blitted glyph by glyph with rotation and
 * doubling tables; here the string is assembled in an off-screen 1-bit
 * buffer, the special effects are applied to it (thicken, lighten, skew,
 * outline, underline, scaling, rotation), and the result is expanded onto
 * the screen in the text colour and writing mode.
 */
#include "vdi.h"
#include <string.h>

#define TB_W	(1024 + 64)		/* pixels */
#define TB_WB	(TB_W / 8)
#define TB_H	40
static UBYTE tbuf[TB_H * TB_WB];
static UBYTE tbuf2[TB_H * TB_WB];
#define rbuf tbuf2				/* rotation target */

static inline int getbit(const UBYTE *b, LONG wb, int x, int y)
{
	return (b[y * wb + (x >> 3)] >> (7 - (x & 7))) & 1;
}
static inline void setbit(UBYTE *b, LONG wb, int x, int y)
{
	b[y * wb + (x >> 3)] |= 0x80 >> (x & 7);
}

/* the fast path for 8-pixel monospaced fonts: not needed, the general
 * code is fast enough here */
WORD MONO8XHT(VOID)
{
	return 0;
}

UWORD CLC_DDA(WORD actual, WORD requested)
{
	ULONG r;
	if (actual <= 0)
		actual = 1;
	if (requested > actual)
	{
		T_SCLSTS = 1;
		requested -= actual;
		if (requested >= actual)
			return 0xFFFF;
		r = ((ULONG)requested << 16) / (ULONG)actual;
		return (UWORD)r;
	}
	T_SCLSTS = 0;
	if (requested == 0)
		requested = 1;
	r = ((ULONG)requested << 16) / (ULONG)actual;
	return (UWORD)r;
}

WORD ACT_SIZ(WORD size)
{
	UWORD acc = 32767;
	WORD n = 0, i;
	ULONG t;

	if (DDA_INC == 0xFFFF)
		return size * 2;
	if (size <= 0)
		return 0;
	for (i = 0; i < size; i++)
	{
		t = (ULONG)acc + DDA_INC;
		acc = (UWORD)t;
		if (T_SCLSTS & 1)
			n += 1 + (t > 0xFFFF);
		else
			n += (t > 0xFFFF);
	}
	if (!(T_SCLSTS & 1) && n == 0)
		n = 1;
	return n;
}

VOID cpy_head(VOID)
{
	FONT_INF = *cur_font;
}

VOID inc_lfu(VOID)
{
	if (act_font && !++act_font->lfu_low)
		act_font->lfu_high++;
}

/* 0: the character is in the current font; 2: out of range */
WORD chk_ade(WORD ch)
{
	ch &= 0xFF;
	if (ch < (WORD)FONT_INF.first_ade || ch > (WORD)FONT_INF.last_ade)
		return 2;
	return 0;
}

VOID chk_fnt(VOID) { }
VOID in_rot(VOID) { }
VOID in_doub(VOID) { }

/* the skew/outline/bold area: build the string into tbuf, return width */
static int build_string(int n, const WORD *str, int *pw, int *ph)
{
	struct font_head *f = &FONT_INF;
	int x = L_OFF + R_OFF > 0 ? 0 : 0, i, rows = f->form_height;
	int adv_extra = ((SPECIAL & THICKEN) && !(f->flags & MONOSPACE)) ? WEIGHT : 0;
	UBYTE *dat = f->dat_table;
	UWORD *off = f->off_table;
	int fwb = f->form_width;

	if (rows > TB_H)
		rows = TB_H;
	memset(tbuf, 0, sizeof(tbuf));
	for (i = 0; i < n; i++)
	{
		int ch = str[i] & 0xFF, cx, cw, yy, xx;
		if (chk_ade(ch) == 2)
			ch = ' ';
		if (chk_ade(ch) == 2)
			ch = f->first_ade;
		ch -= f->first_ade;
		cx = off[ch];
		cw = off[ch + 1] - cx;
		if (f->flags & HORZ_OFF)
			x -= f->hor_table[ch * 2];
		if (x + cw + WEIGHT + 16 >= TB_W)
			break;
		for (yy = 0; yy < rows; yy++)
		{
			const UBYTE *src = dat + yy * fwb;
			for (xx = 0; xx < cw; xx++)
				if ((src[(cx + xx) >> 3] >> (7 - ((cx + xx) & 7))) & 1)
					setbit(tbuf, TB_WB, x + xx, yy);
		}
		x += cw + adv_extra;
		if (f->flags & HORZ_OFF)
			x -= f->hor_table[ch * 2 + 1];
	}
	*pw = x;
	*ph = rows;
	return x;
}

/* effects on tbuf (w x h), may widen w */
static void effects(int *pw, int h)
{
	int w = *pw, yy, k, b;
	struct font_head *f = &FONT_INF;

	if (SPECIAL & THICKEN)
	{
		for (yy = 0; yy < h; yy++)
		{
			UBYTE *r = tbuf + yy * TB_WB;
			for (k = 0; k < WEIGHT; k++)
			{
				UBYTE carry = 0;
				for (b = 0; b < TB_WB; b++)
				{
					UBYTE v = r[b];
					r[b] = v | (v >> 1) | carry;
					carry = (v & 1) << 7;
				}
			}
		}
		if (f->flags & MONOSPACE)
			;
		else
			w += 0;
		w += WEIGHT;
	}
	if (SPECIAL & LIGHT)
	{
		UWORD m = f->lighten;
		for (yy = 0; yy < h; yy++)
		{
			UBYTE *r = tbuf + yy * TB_WB;
			for (b = 0; b < TB_WB; b++)
				r[b] &= (b & 1) ? (m & 0xFF) : (m >> 8);
			m = (UWORD)((m >> 1) | (m << 15));
		}
	}
	if (SPECIAL & SKEW)
	{
		/* shift rows right; the skew mask, rotated per row from the
		 * bottom up, says on which rows the shift grows */
		UWORD m = f->skew;
		int shift = 0, total = L_OFF + R_OFF;
		memcpy(tbuf2, tbuf, sizeof(tbuf));
		memset(tbuf, 0, sizeof(tbuf));
		for (yy = h - 1; yy >= 0; yy--)
		{
			int xx;
			for (xx = 0; xx < w && xx + shift < TB_W; xx++)
				if (getbit(tbuf2, TB_WB, xx, yy))
					setbit(tbuf, TB_WB, xx + shift, yy);
			if (m & 1)
				if (shift < total)
					shift++;
			m = (UWORD)((m >> 1) | (m << 15));
		}
		w += total;
	}
	if (SPECIAL & OUTLINE)
	{
		/* outline: the 1-pixel dilation minus the glyph */
		int xx;
		memcpy(tbuf2, tbuf, sizeof(tbuf));
		memset(tbuf, 0, sizeof(tbuf));
		for (yy = 0; yy < h; yy++)
			for (xx = 0; xx < w + 2 && xx < TB_W - 1; xx++)
			{
				int on = 0, oy, ox, self = xx >= 1 ? getbit(tbuf2, TB_WB, xx - 1, yy) : 0;
				for (oy = -1; oy <= 1 && !on; oy++)
					for (ox = -1; ox <= 1 && !on; ox++)
					{
						int sx = xx - 1 + ox, sy = yy + oy;
						if (sx >= 0 && sy >= 0 && sy < h && getbit(tbuf2, TB_WB, sx, sy))
							on = 1;
					}
				if (on && !self)
					setbit(tbuf, TB_WB, xx, yy);
			}
		w += 2;
	}
	*pw = w;
}

/* scale tbuf (w x h) with the DDA to (ACT_SIZ(w) x ACTDELY) */
static void scale(int *pw, int *ph)
{
	int w = *pw, h = *ph, nw, nh, yy, xx, sy, sx;
	UWORD acc;
	static WORD xmap[TB_W], ymap[TB_H];
	ULONG t;

	nw = 0;
	acc = XACC_DDA;
	for (xx = 0; xx < w && nw < TB_W; xx++)
	{
		int rep;
		t = (ULONG)acc + DDA_INC;
		acc = (UWORD)t;
		if (DDA_INC == 0xFFFF)
			rep = 2;
		else if (T_SCLSTS & 1)
			rep = 1 + (t > 0xFFFF);
		else
			rep = (t > 0xFFFF);
		while (rep-- > 0 && nw < TB_W)
			xmap[nw++] = xx;
	}
	XACC_DDA = acc;
	nh = 0;
	acc = 32767;
	for (yy = 0; yy < h && nh < TB_H; yy++)
	{
		int rep;
		t = (ULONG)acc + DDA_INC;
		acc = (UWORD)t;
		if (DDA_INC == 0xFFFF)
			rep = 2;
		else if (T_SCLSTS & 1)
			rep = 1 + (t > 0xFFFF);
		else
			rep = (t > 0xFFFF);
		while (rep-- > 0 && nh < TB_H)
			ymap[nh++] = yy;
	}
	memcpy(tbuf2, tbuf, sizeof(tbuf));
	memset(tbuf, 0, sizeof(tbuf));
	for (yy = 0; yy < nh; yy++)
	{
		sy = ymap[yy];
		for (xx = 0; xx < nw; xx++)
		{
			sx = xmap[xx];
			if (getbit(tbuf2, TB_WB, sx, sy))
				setbit(tbuf, TB_WB, xx, yy);
		}
	}
	*pw = nw;
	*ph = nh;
}

VOID TEXT_BLT(VOID)
{
	int n = CONTRL[3], w, h, cx0, cy0, cx1, cy1;
	const UBYTE *src = tbuf;
	LONG swb = TB_WB;
	int sw, sh;

	if (n <= 0)
		return;
	build_string(n, INTIN, &w, &h);
	if (SPECIAL & SCALE)
		scale(&w, &h);
	effects(&w, h);
	if (SPECIAL & UNDER)
	{
		/* underline under the baseline, across the string */
		int top = FONT_INF.top + 1, k, xx;
		for (k = 0; k < FONT_INF.ul_size && top + k < h; k++)
			for (xx = 0; xx < w; xx++)
				setbit(tbuf, TB_WB, xx, top + k);
	}
	sw = w;
	sh = h;
	if (rot_case)
	{
		/* rotate into rbuf; GEM's angles are counter-clockwise */
		int xx, yy, nw, nh, rwb;
		if (rot_case == 2)
		{
			nw = w;
			nh = h;
		}
		else
		{
			nw = h;
			nh = w;
		}
		rwb = (nw + 7) >> 3;
		if ((LONG)rwb * nh > (LONG)sizeof(rbuf))
			return;
		memset(rbuf, 0, (LONG)rwb * nh);
		for (yy = 0; yy < h; yy++)
			for (xx = 0; xx < w; xx++)
				if (getbit(tbuf, TB_WB, xx, yy))
				{
					int rx, ry;
					switch (rot_case)
					{
					case 1:  rx = yy; ry = w - 1 - xx; break;	/* 90 */
					case 2:  rx = w - 1 - xx; ry = h - 1 - yy; break;
					default: rx = h - 1 - yy; ry = xx; break;	/* 270 */
					}
					setbit(rbuf, rwb, rx, ry);
				}
		src = rbuf;
		swb = rwb;
		sw = nw;
		sh = nh;
	}
	if (CLIP)
	{
		cx0 = XMN_CLIP; cy0 = YMN_CLIP; cx1 = XMX_CLIP; cy1 = YMX_CLIP;
	}
	else
	{
		cx0 = 0; cy0 = 0; cx1 = screen.w - 1; cy1 = screen.h - 1;
	}
	{
		int dx = DESTX, dy = DESTY;
		if (rot_case == 1)
			dy -= sh - 1;
		else if (rot_case == 2)
			dx -= sw - 1;
		mouse_hidden++;
		surf_mono_blit(src, swb, 0, 0, sw, sh, &screen, dx, dy,
			(WRT_MODE & 3) + 1, TEXT_BP, 0, cx0, cy0, cx1, cy1);
		mouse_hidden--;
	}
}

/* clear the box that a skewed string will cover (replace and reverse
 * transparent modes), X1..X2 / Y1..Y2 set by d_gtext */
VOID clr_skew(VOID)
{
	int x1 = X1, x2 = X2, y1 = Y1, y2 = Y2, yy, h = ACTDELY;
	if (rot_case == 0 || rot_case == 2)
	{
		int top = rot_case == 0 ? y1 - h + 1 : y1;
		for (yy = top; yy < top + h; yy++)
			if (yy >= 0 && yy < screen.h)
			{
				int a = x1 < 0 ? 0 : x1, b = x2 >= screen.w ? screen.w - 1 : x2;
				if (CLIP)
				{
					if (yy < YMN_CLIP || yy > YMX_CLIP) continue;
					if (a < XMN_CLIP) a = XMN_CLIP;
					if (b > XMX_CLIP) b = XMX_CLIP;
				}
				if (a <= b)
					surf_hspan(&screen, yy, a, b, 0xFFFF, 0,
						WRT_MODE == 3 ? TEXT_BP : 0);
			}
	}
}

/* ------------------------------------------------ justified text ------ */

/* v_justified (GDP 10): PTSIN[2] = length, INTIN[0] = inter-word
 * spacing allowed, INTIN[1] = inter-character spacing allowed, then the
 * string.  Characters are drawn one by one with the extra space spread
 * over the gaps. */
VOID d_justified(VOID)
{
	int n = CONTRL[3] - 2, i, wordsp = INTIN[0], charsp = INTIN[1];
	int want = PTSIN[2], natural, spaces = 0, extra, x, y;
	static WORD s[INTIN_SIZE];
	WORD save3 = CONTRL[3];
	int del_w = 0, del_c = 0, rem_w = 0, rem_c = 0;

	if (n <= 0)
		return;
	for (i = 0; i < n; i++)
		s[i] = INTIN[i + 2];
	/* natural width */
	for (i = 0; i < n; i++)
		INTIN[i] = s[i];
	CONTRL[3] = n;
	dqt_extent();
	natural = width;
	for (i = 0; i < n; i++)
		if (s[i] == ' ')
			spaces++;
	extra = want - natural;
	if (wordsp && spaces)
	{
		del_w = extra / spaces;
		rem_w = extra % spaces;
	}
	else if (charsp && n > 1)
	{
		del_c = extra / (n - 1);
		rem_c = extra % (n - 1);
	}
	x = PTSIN[0];
	y = PTSIN[1];
	for (i = 0; i < n; i++)
	{
		INTIN[0] = s[i];
		CONTRL[3] = 1;
		PTSIN[0] = x;
		PTSIN[1] = y;
		d_gtext();
		dqt_extent();
		x += width;
		if (s[i] == ' ' && del_w | rem_w)
		{
			x += del_w;
			if (rem_w > 0) { x++; rem_w--; }
			else if (rem_w < 0) { x--; rem_w++; }
		}
		if (del_c | rem_c)
		{
			x += del_c;
			if (rem_c > 0) { x++; rem_c--; }
			else if (rem_c < 0) { x--; rem_c++; }
		}
	}
	CONTRL[3] = save3;
	CONTRL[2] = 0;
	CONTRL[4] = 0;
}

VOID dqt_just(VOID)
{
	/* vqt_justified (GEM/3 extension, opcode 132): returns the x offsets of
	 * the characters for a justified string - approximated as the plain
	 * advances */
	int n = CONTRL[3] - 2, i, x = 0;
	WORD save0;
	if (n <= 0)
		return;
	for (i = 0; i < n && i < PTSOUT_SIZE / 2; i++)
	{
		save0 = INTIN[0];
		INTIN[0] = INTIN[i + 2];
		CONTRL[3] = 1;
		dqt_extent();
		INTIN[0] = save0;
		PTSOUT[i * 2] = x;
		PTSOUT[i * 2 + 1] = 0;
		x += width;
	}
	CONTRL[2] = i;
	CONTRL[4] = 0;
}
