/*
 * raster.c - raster operations of the ARM-DOS GEM screen driver:
 * vro_cpyfm / vrt_cpyfm (COPY_RFM), vr_trnfm (TRAN_FM), vsf_udpat
 * (XFM_UDFL) and the monochrome expander that text and icons use.
 *
 * Rewritten in C from the GEM/3 driver's RASTOP.A86 / IBMBLMPC.A86 /
 * MONMMRE1.A86 (Copyright 1987 Digital Research; GNU GPL v2 via Caldera).
 *
 * Memory forms are described by MFDBs whose flat address arrives in
 * CONTRL[7..8] (source) and CONTRL[9..10] (destination), low word first,
 * where the 8086 driver found a segment:offset.  fd_addr == 0 means the
 * screen.  "Device format" is the screen's own format: 1 plane = bytes,
 * leftmost pixel in bit 7, inverted on the black-on-white mono screens
 * (as rev_vid did on the PC).  On the colour screen (mode 62h, one byte per
 * pixel holding the colour's pixel value) a memory form of 4 planes - what
 * the driver reports, as GEM's 16-colour drivers did - is packed pixels,
 * two to a byte (high nibble left), the same size as its standard format;
 * one of 8 planes is one byte per pixel.
 */
#include "vdi.h"
#include <stdlib.h>
#include <string.h>

struct mfdb {
	LONG	addr;
	WORD	w, h, wdwidth, stand, nplanes;
};

static UWORD rd16(const UBYTE *p) { return p[0] | (p[1] << 8); }
static void wr16(UBYTE *p, UWORD v) { p[0] = v; p[1] = v >> 8; }

static void get_mfdb(int ci, struct mfdb *m)
{
	const UBYTE *p = (const UBYTE *)((UWORD)CONTRL[ci] | ((ULONG)(UWORD)CONTRL[ci + 1] << 16));
	m->addr = rd16(p) | ((LONG)rd16(p + 2) << 16);
	m->w = rd16(p + 4);
	m->h = rd16(p + 6);
	m->wdwidth = rd16(p + 8);
	m->stand = rd16(p + 10);
	m->nplanes = rd16(p + 12);
}

static void set_stand(int ci, WORD v)
{
	UBYTE *p = (UBYTE *)((UWORD)CONTRL[ci] | ((ULONG)(UWORD)CONTRL[ci + 1] << 16));
	wr16(p + 10, v);
}

/* a SURF for an MFDB (0 = the screen); planes = the form's own planes
 * (1, or on the colour screen 4 or 8) */
static int mfdb_surf(struct mfdb *m, SURF *s, int planes)
{
	if (m->addr == 0)
	{
		*s = screen;
		return 1;
	}
	s->addr = (UBYTE *)m->addr;
	s->rows = NULL;
	s->w = m->w;
	s->h = m->h;
	s->planes = planes;
	s->wb = (LONG)m->wdwidth * 2 * planes;
	s->inv = screen.inv;
	return 0;
}

/* can a form of n planes be used in device format with this screen? */
static int planes_ok(int n)
{
	return screen.planes == 1 ? n == 1 : (n == 4 || n == 8);
}

/* one pixel value of a colour row (4 or 8 planes) */
static inline int pget(const UBYTE *row, int planes, int x)
{
	if (planes == 8)
		return row[x];
	return (x & 1) ? row[x >> 1] & 15 : row[x >> 1] >> 4;
}

static inline void pput(UBYTE *row, int planes, int x, int v)
{
	if (planes == 8)
		row[x] = v;
	else if (x & 1)
		row[x >> 1] = (row[x >> 1] & 0xF0) | (v & 15);
	else
		row[x >> 1] = (row[x >> 1] & 0x0F) | (v << 4);
}

static inline int logic(int op, int s, int d)
{
	switch (op & 15)
	{
	case 0: return 0;
	case 1: return s & d;
	case 2: return s & ~d;
	case 3: return s;
	case 4: return ~s & d;
	case 5: return d;
	case 6: return s ^ d;
	case 7: return s | d;
	case 8: return ~(s | d);
	case 9: return ~(s ^ d);
	case 10: return ~d;
	case 11: return s | ~d;
	case 12: return ~s;
	case 13: return ~s | d;
	case 14: return ~(s & d);
	default: return ~0;
	}
}

/* extract w pixels of 1-plane row data starting at x into logical bits
 * aligned at bit 7 of out[0] */
static void extract_bits(const UBYTE *row, int x, int w, UBYTE inv, UBYTE *out)
{
	int nb = (w + 7) >> 3, sh = x & 7, i;
	const UBYTE *p = row + (x >> 3);
	if (sh == 0)
		for (i = 0; i < nb; i++)
			out[i] = p[i] ^ inv;
	else
		for (i = 0; i < nb; i++)
			out[i] = ((p[i] << sh) | (p[i + 1] >> (8 - sh))) ^ inv;
	if (w & 7)
		out[nb - 1] &= 0xFF << (8 - (w & 7));
	out[nb] = 0;
}

/* write w logical bits (bit-aligned at 0) to a 1-plane row at x with op */
static void put_bits_op(UBYTE *row, int x, int w, UBYTE inv, const UBYTE *bits, int op)
{
	int sh = x & 7, b1 = x >> 3, b2 = (x + w - 1) >> 3, b, j;
	for (b = b1, j = 0; b <= b2; b++, j++)
	{
		UBYTE m = 0xFF, s, d, r;
		if (b == b1)
			m &= 0xFF >> sh;
		if (b == b2)
			m &= 0xFF << (7 - ((x + w - 1) & 7));
		if (sh == 0)
			s = bits[j];
		else
			s = (j ? (bits[j - 1] << (8 - sh)) : 0) | (bits[j] >> sh);
		d = row[b] ^ inv;
		r = logic(op, s, d);
		row[b] = ((d & ~m) | (r & m)) ^ inv;
	}
}

/* mono source expansion: logical bits (1 = set) of src (swb bytes/row)
 * starting at (sx,sy), w x h, onto d at (dx,dy) clipped to cx0..cx1/cy0..cy1,
 * in writing mode 1..4 (vrt_cpyfm numbering: 1 replace, 2 transparent,
 * 3 xor, 4 reverse transparent) with colours fg/bg. */
void surf_mono_blit(const UBYTE *src, LONG swb, int sx, int sy, int w, int h,
	SURF *d, int dx, int dy, int mode, int fg, int bg,
	int cx0, int cy0, int cx1, int cy1)
{
	static UBYTE tmp[1024];
	int yy;

	if (cx0 < 0) cx0 = 0;
	if (cy0 < 0) cy0 = 0;
	if (cx1 >= d->w) cx1 = d->w - 1;
	if (cy1 >= d->h) cy1 = d->h - 1;
	if (dx < cx0) { w -= cx0 - dx; sx += cx0 - dx; dx = cx0; }
	if (dy < cy0) { h -= cy0 - dy; sy += cy0 - dy; dy = cy0; }
	if (dx + w - 1 > cx1) w = cx1 - dx + 1;
	if (dy + h - 1 > cy1) h = cy1 - dy + 1;
	if (w <= 0 || h <= 0 || w > 8000)
		return;
	for (yy = 0; yy < h; yy++)
	{
		UBYTE *drow = SROW(d, dy + yy);
		extract_bits(src + (sy + yy) * swb, sx, w, 0, tmp);
		if (d->planes == 1)
		{
			int op;
			/* the logic op of RASTOP's tran_blt_tbl for this colour */
			switch (mode)
			{
			case 1: op = fg ? (bg ? 15 : 3) : (bg ? 12 : 0); break;
			case 2: op = fg ? 7 : 4; break;
			case 3: op = 6; break;
			default: op = bg ? 13 : 1; break;
			}
			put_bits_op(drow, dx, w, d->inv, tmp, op);
		}
		else if (d->planes == 8)
		{
			int x, xm = dev_xor_mask();
			UBYTE *p = drow + dx;
			for (x = 0; x < w; x++)
			{
				int on = (tmp[x >> 3] >> (7 - (x & 7))) & 1;
				switch (mode)
				{
				case 1: p[x] = on ? fg : bg; break;
				case 2: if (on) p[x] = fg; break;
				case 3: if (on) p[x] ^= xm; break;
				default: if (!on) p[x] = bg; break;
				}
			}
		}
		else
		{
			int x, xm = dev_xor_mask();
			for (x = 0; x < w; x++)
			{
				int on = (tmp[x >> 3] >> (7 - (x & 7))) & 1;
				switch (mode)
				{
				case 1: pput(drow, d->planes, dx + x, on ? fg : bg); break;
				case 2: if (on) pput(drow, d->planes, dx + x, fg); break;
				case 3: if (on) pput(drow, d->planes, dx + x, pget(drow, d->planes, dx + x) ^ xm); break;
				default: if (!on) pput(drow, d->planes, dx + x, bg); break;
				}
			}
		}
	}
}

/* COPY_RFM: vro_cpyfm (COPYTRAN == 0) and vrt_cpyfm (COPYTRAN != 0).
 * PTSIN[0..3] source rectangle, PTSIN[4..7] destination (only its upper
 * left corner counts).  Clipped to the clip rectangle when the
 * destination is the screen and clipping is on, as RASTOP did. */
VOID COPY_RFM(VOID)
{
	struct mfdb sm, dm;
	SURF ss, ds;
	int sx, sy, w, h, dx, dy, yy, step, y0, op;
	int tran = COPYTRAN != 0;
	static UBYTE tmp[2048];

	get_mfdb(7, &sm);
	get_mfdb(9, &dm);
	if (tran)
		mfdb_surf(&sm, &ss, 1);
	else
	{
		if (sm.addr && !planes_ok(sm.nplanes))
			return;
		mfdb_surf(&sm, &ss, sm.nplanes);
	}
	if (dm.addr && !planes_ok(dm.nplanes))
		return;
	mfdb_surf(&dm, &ds, dm.nplanes);

	sx = PTSIN[0]; sy = PTSIN[1];
	w = PTSIN[2] - PTSIN[0] + 1;
	h = PTSIN[3] - PTSIN[1] + 1;
	dx = PTSIN[4]; dy = PTSIN[5];

	/* clip the destination (to the clip rectangle on the screen) */
	{
		int cx0 = 0, cy0 = 0, cx1 = ds.w - 1, cy1 = ds.h - 1;
		if (CLIP && !dm.addr)
		{
			if (XMN_CLIP > cx0) cx0 = XMN_CLIP;
			if (YMN_CLIP > cy0) cy0 = YMN_CLIP;
			if (XMX_CLIP < cx1) cx1 = XMX_CLIP;
			if (YMX_CLIP < cy1) cy1 = YMX_CLIP;
		}
		if (dx < cx0) { w -= cx0 - dx; sx += cx0 - dx; dx = cx0; }
		if (dy < cy0) { h -= cy0 - dy; sy += cy0 - dy; dy = cy0; }
		if (dx + w - 1 > cx1) w = cx1 - dx + 1;
		if (dy + h - 1 > cy1) h = cy1 - dy + 1;
	}
	/* and the source to its form */
	if (sx < 0) { w += sx; dx -= sx; sx = 0; }
	if (sy < 0) { h += sy; dy -= sy; sy = 0; }
	if (sx + w > ss.w) w = ss.w - sx;
	if (sy + h > ss.h) h = ss.h - sy;
	if (w <= 0 || h <= 0)
		return;

	mouse_hidden++;
	if (tran)
	{
		int fg = MAP_COL[INTIN[1] & 15], bg = MAP_COL[INTIN[2] & 15];
		int mode = INTIN[0];
		if (mode < 1 || mode > 4)
			mode = 1;
		/* the source bits are in device format: undo the inversion */
		for (yy = 0; yy < h; yy++)
		{
			UBYTE *srow = SROW(&ss, sy + yy);
			extract_bits(srow, sx, w, ss.inv, tmp);
			surf_mono_blit(tmp, 0, 0, 0, w, 1, &ds, dx, dy + yy, mode, fg, bg,
				0, 0, ds.w - 1, ds.h - 1);
		}
		mouse_hidden--;
		return;
	}

	op = INTIN[0] & 15;
	/* vertical overlap: copy bottom-up when moving down in one form */
	if (ss.addr == ds.addr && ss.rows == ds.rows && dy > sy)
	{
		y0 = h - 1;
		step = -1;
	}
	else
	{
		y0 = 0;
		step = 1;
	}
	for (yy = y0; yy >= 0 && yy < h; yy += step)
	{
		UBYTE *srow = SROW(&ss, sy + yy);
		UBYTE *drow = SROW(&ds, dy + yy);
		if (screen.planes == 1)
		{
			extract_bits(srow, sx, w, ss.inv, tmp);
			put_bits_op(drow, dx, w, ds.inv, tmp, op);
		}
		else if (ss.planes == 8 && ds.planes == 8)
		{
			int x, cm = dev->colors - 1;
			if (op == 3)
				memmove(drow + dx, srow + sx, w);
			else
			{
				memmove(tmp, srow + sx, w);
				for (x = 0; x < w; x++)
					drow[dx + x] = logic(op, tmp[x], drow[dx + x]) & cm;
			}
		}
		else
		{
			int x, cm = dev->colors - 1;
			for (x = 0; x < w; x++)
				tmp[x] = pget(srow, ss.planes, sx + x);
			for (x = 0; x < w; x++)
				pput(drow, ds.planes, dx + x, logic(op, tmp[x], pget(drow, ds.planes, dx + x)) & cm);
		}
	}
	mouse_hidden--;
}

/* TRAN_FM: vr_trnfm, standard <-> device format */
VOID TRAN_FM(VOID)
{
	struct mfdb sm, dm;
	UBYTE *s, *d;
	LONG words, i;
	int planes;

	get_mfdb(7, &sm);
	get_mfdb(9, &dm);
	s = (UBYTE *)sm.addr;
	d = (UBYTE *)dm.addr;
	planes = sm.nplanes;
	if (!s || !d)
		return;
	words = (LONG)sm.wdwidth * sm.h;
	if (planes == 1)
	{
		/* standard: 16-bit words (little-endian in memory), MSB = left;
		 * device: bytes in screen order, inverted on mono screens */
		UBYTE inv = screen.planes == 1 ? screen.inv : 0;
		for (i = 0; i < words; i++)
		{
			UBYTE a = s[2 * i], b = s[2 * i + 1];
			d[2 * i] = b ^ inv;
			d[2 * i + 1] = a ^ inv;
		}
	}
	else if ((planes == 4 || planes == 8) && screen.planes == 8)
	{
		/* standard: the planes one after the other, each a sequence of
		 * words (plane 0 = bit 0 of the colour); device: packed pixels,
		 * 4 bits (high nibble left) or 8.  Same size either way, so the
		 * transform may be in place: go through a copy. */
		LONG pw = words, x, n = words * 2 * planes;
		UBYTE *t = malloc(n);
		int p;
		if (!t)
			return;
		memset(t, 0, n);
		if (sm.stand)
		{
			for (p = 0; p < planes; p++)
				for (i = 0; i < pw; i++)
				{
					UWORD v = rd16(s + 2 * (p * pw + i));
					for (x = 0; x < 16; x++)
						if (v & (0x8000 >> x))
							pput(t, planes, i * 16 + x, pget(t, planes, i * 16 + x) | (1 << p));
				}
		}
		else
		{
			for (p = 0; p < planes; p++)
				for (i = 0; i < pw; i++)
				{
					UWORD v = 0;
					for (x = 0; x < 16; x++)
						if (pget(s, planes, i * 16 + x) & (1 << p))
							v |= 0x8000 >> x;
					wr16(t + 2 * (p * pw + i), v);
				}
		}
		memcpy(d, t, n);
		free(t);
	}
	set_stand(9, sm.stand ? 0 : 1);
}

/* XFM_UDFL: vsf_udpat, CONTRL[3] words = 16 per plane */
VOID XFM_UDFL(VOID)
{
	int n = CONTRL[3], i, planes = n / 16;
	if (planes < 1)
		return;
	if (planes > 8)
		planes = 8;
	for (i = 0; i < planes * 16; i++)
		vw->a_ud_patrn[i] = INTIN[i];
	udpt_np = planes > 1 ? planes : 0;
	if (fill_style == 4)
		NEXT_PAT = udpt_np;
}
