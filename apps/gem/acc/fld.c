/*
 * fld.c - decimal floating point for the GEM Calculator (ARM-DOS).
 *
 * The GEM/3 CalClock sources call _FLD_ADD/_SUB/_MUL/_DIV from CBARITH.OBJ,
 * a binary-only 8086 BCD library that is not in the source release.  This
 * is a new implementation of the same interface and number format:
 *
 *   byte 0     bit 7 = sign, bits 0-6 = exponent + 64
 *   bytes 1-9  18 BCD digits, the most significant in the high nibble of
 *              byte 9: value = d1.d2d3...d18 x 10^(exponent)
 *   zero       exponent byte 0
 *
 * _FLD_op(result, y, x) computes result = x op y (the calculator passes
 * (result, op2, op1): op1 is the left operand), rounded to 18 digits.  As
 * CALC.C expects (its "special kludge for times and divide"), the exponent
 * of a product is stored one lower and that of a quotient one higher than
 * the normalised value's.
 */
#include <portab.h>
#include "machine.h"
#include "accarm.h"

typedef unsigned long long U64;
typedef long long S64;

#define P17 100000000000000000ULL
#define P18 1000000000000000000ULL
#define P9  1000000000ULL

struct num { int neg; int e; U64 m; };	/* value = m x 10^e, m < 10^18 */

static void decode(const UBYTE *f, struct num *n)
{
	int i;
	n->m = 0;
	for (i = 19; i >= 2; i--)
	{
		UBYTE b = f[i >> 1];
		n->m = n->m * 10 + ((i & 1) ? (b >> 4) : (b & 15));
	}
	n->neg = (f[0] & 0x80) != 0;
	n->e = (f[0] & 0x7F) - 64 - 17;
	if ((f[0] & 0x7F) == 0)
		n->m = 0;
	/* normalise to 18 digits */
	if (n->m)
		while (n->m < P17)
		{
			n->m *= 10;
			n->e--;
		}
}

static void encode(UBYTE *f, struct num *n, int roundup)
{
	int i, e;
	U64 m = n->m;
	if (roundup)
		m++;
	while (m >= P18)
	{
		U64 r = m % 10;
		m /= 10;
		if (r >= 5)
			m++;
		n->e++;
	}
	for (i = 0; i < 10; i++)
		f[i] = 0;
	if (!m)
		return;
	while (m < P17)
	{
		m *= 10;
		n->e--;
	}
	e = n->e + 17 + 64;
	if (e < 1)				/* underflow: zero */
		return;
	if (e > 127)
		e = 127;
	f[0] = (n->neg ? 0x80 : 0) | e;
	for (i = 2; i <= 19; i++)
	{
		UBYTE d = m % 10;
		m /= 10;
		if (i & 1)
			f[i >> 1] |= d << 4;
		else
			f[i >> 1] |= d;
	}
}

static void add(UBYTE *res, const UBYTE *pa, const UBYTE *pb, int sub)
{
	struct num a, b, r;
	S64 sa, sb, s;
	int k, round = 0;

	decode(pa, &a);
	decode(pb, &b);
	if (sub)
		b.neg = !b.neg;
	if (!a.m) { r = b; encode(res, &r, 0); return; }
	if (!b.m) { r = a; encode(res, &r, 0); return; }
	if (a.e < b.e)
	{
		struct num t = a; a = b; b = t;
	}
	k = a.e - b.e;
	if (k > 18)
		b.m = 0;
	else
		while (k-- > 0)
		{
			U64 rem = b.m % 10;
			b.m /= 10;
			if (k == 0 && rem >= 5)
				round = 1;
		}
	if (round)
		b.m++;
	sa = a.neg ? -(S64) a.m : (S64) a.m;
	sb = b.neg ? -(S64) b.m : (S64) b.m;
	s = sa + sb;
	r.neg = s < 0;
	r.m = s < 0 ? (U64) -s : (U64) s;
	r.e = a.e;
	encode(res, &r, 0);
}

VOID _FLD_ADD(UBYTE *res, UBYTE *y, UBYTE *x) { add(res, x, y, 0); }
VOID _FLD_SUB(UBYTE *res, UBYTE *y, UBYTE *x) { add(res, x, y, 1); }

/* the exponent convention of products and quotients */
static void bias(UBYTE *res, int d)
{
	int e = res[0] & 0x7F;
	if (!e)
		return;
	e += d;
	if (e < 1) e = 1;
	if (e > 127) e = 127;
	res[0] = (res[0] & 0x80) | e;
}

VOID _FLD_MUL(UBYTE *res, UBYTE *pb, UBYTE *pa)
{
	struct num a, b, r;
	U64 ah, al, bh, bl, hh, mid, ll, low, top;
	int up;

	decode(pa, &a);
	decode(pb, &b);
	if (!a.m || !b.m)
	{
		r.m = 0; r.e = 0; r.neg = 0;
		encode(res, &r, 0);
		return;
	}
	ah = a.m / P9; al = a.m % P9;
	bh = b.m / P9; bl = b.m % P9;
	hh = ah * bh;
	mid = ah * bl + al * bh;
	ll = al * bl;
	low = (mid % P9) * P9 + ll;
	top = hh + mid / P9 + low / P18;
	low %= P18;
	up = low >= P18 / 2;
	r.m = top;
	r.e = a.e + b.e + 18;
	r.neg = a.neg != b.neg;
	encode(res, &r, up);
	bias(res, -1);
}

VOID _FLD_DIV(UBYTE *res, UBYTE *pb, UBYTE *pa)
{
	struct num a, b, r;
	U64 q, rem;
	int i, up;

	decode(pa, &a);
	decode(pb, &b);
	if (!a.m || !b.m)
	{
		r.m = 0; r.e = 0; r.neg = 0;
		encode(res, &r, 0);
		return;
	}
	q = a.m / b.m;
	rem = a.m % b.m;
	for (i = 0; i < 18; i++)
	{
		rem *= 10;
		q = q * 10 + rem / b.m;
		rem %= b.m;
	}
	rem *= 10;
	up = rem / b.m >= 5;
	r.m = q;
	r.e = a.e - b.e - 18;
	r.neg = a.neg != b.neg;
	encode(res, &r, up);
	bias(res, 1);
}
