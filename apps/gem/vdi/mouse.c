/*
 * mouse.c - the mouse cursor, the mouse and timer vectors of the ARM-DOS GEM
 * screen driver.
 *
 * Rewritten in C from the GEM/3 driver's IMOUSE.A86 and the cursor code of
 * MONMMRE1.A86 (DIS_CUR, HIDE_CUR, MOV_CUR, XFM_CRFM, VEX_*; Copyright 1987
 * Digital Research, GNU GPL v2 via Caldera 1999).  The PC driver read the
 * serial or bus mouse itself; here the mouse comes from the INT 33h driver
 * (MOUSE.COM) through its event handler (AX=000Ch), and the tick from INT
 * 1Ch.  Both run at interrupt time (SVC mode, IRQs off).
 *
 * The vectors that vex_butv / vex_motv / vex_curv / vex_timv exchange are
 * flat C function pointers on ARM-DOS (ARCH.md: a "far call" is blx):
 *	butv(WORD buttons)
 *	motv(WORD *x, WORD *y)		may change the position
 *	curv(WORD x, WORD y)		draws the cursor
 *	timv(VOID)			once per tick
 */
#include "vdi.h"
#include <dos.h>
#include <armdos.h>

typedef VOID (*BUTV)(WORD);
typedef VOID (*MOTV)(WORD *, WORD *);
typedef VOID (*CURV)(WORD, WORD);
typedef VOID (*TIMV)(VOID);

static VOID ubutvec(WORD b);
static VOID umotvec(WORD *x, WORD *y);
static VOID mov_cur(WORD x, WORD y);
static VOID no_tick(VOID) { }

static BUTV userbut = ubutvec;
static MOTV usermot = umotvec;
static CURV usercur = mov_cur;
static TIMV usertim = no_tick;

WORD	HIDE_CNT = 1, MOUSE_BT, GCURX, GCURY;
volatile WORD mouse_hidden;		/* > 0: a primitive is drawing */
static volatile WORD cur_drawn;		/* the cursor is on the screen */
static volatile WORD cur_pending;	/* moved while drawing was blocked */
static WORD cur_x, cur_y;		/* where it is drawn (hot spot applied) */
static WORD mouse_on;			/* INT 33h handler installed */

/* the cursor form */
static WORD m_xhot, m_yhot, m_fg = 1, m_bg = 0;
static UWORD m_mask[16], m_data[16];

/* what is under the cursor */
static UBYTE save_buf[16 * 16];
static WORD save_w, save_h, save_x, save_y;

extern WORD def_mform[];

static void cur_undraw(void)
{
	int yy;
	if (!cur_drawn)
		return;
	if (screen.planes == 1)
	{
		int nb = ((save_x + save_w - 1) >> 3) - (save_x >> 3) + 1;
		for (yy = 0; yy < save_h; yy++)
		{
			UBYTE *p = SROW(&screen, save_y + yy) + (save_x >> 3);
			int k;
			for (k = 0; k < nb; k++)
				p[k] = save_buf[yy * 3 + k];
		}
	}
	else
		for (yy = 0; yy < save_h; yy++)
		{
			UBYTE *p = SROW(&screen, save_y + yy) + save_x;
			int k;
			for (k = 0; k < save_w; k++)
				p[k] = save_buf[yy * 16 + k];
		}
	cur_drawn = 0;
}

static void cur_draw(WORD x, WORD y)
{
	int x0 = x - m_xhot, y0 = y - m_yhot, sx = 0, sy = 0, w = 16, h = 16, yy, xx;
	if (x0 < 0) { sx = -x0; w += x0; x0 = 0; }
	if (y0 < 0) { sy = -y0; h += y0; y0 = 0; }
	if (x0 + w > screen.w) w = screen.w - x0;
	if (y0 + h > screen.h) h = screen.h - y0;
	if (w <= 0 || h <= 0)
		return;
	save_x = x0; save_y = y0; save_w = w; save_h = h;
	if (screen.planes == 1)
	{
		int nb = ((x0 + w - 1) >> 3) - (x0 >> 3) + 1;
		for (yy = 0; yy < h; yy++)
		{
			UBYTE *p = SROW(&screen, y0 + yy) + (x0 >> 3);
			int k;
			for (k = 0; k < nb; k++)
				save_buf[yy * 3 + k] = p[k];
		}
	}
	else
		for (yy = 0; yy < h; yy++)
		{
			UBYTE *p = SROW(&screen, y0 + yy) + x0;
			for (xx = 0; xx < w; xx++)
				save_buf[yy * 16 + xx] = p[xx];
		}
	for (yy = 0; yy < h; yy++)
	{
		UWORD mk = m_mask[sy + yy], dt = m_data[sy + yy];
		for (xx = 0; xx < w; xx++)
		{
			UWORD bit = 0x8000 >> (sx + xx);
			if (dt & bit)
				surf_pixel(&screen, x0 + xx, y0 + yy, 1, 0, m_fg);
			else if (mk & bit)
				surf_pixel(&screen, x0 + xx, y0 + yy, 1, 0, m_bg);
		}
	}
	cur_x = x;
	cur_y = y;
	cur_drawn = 1;
}

/* the default cursor-draw vector (MOV_CUR) */
static VOID mov_cur(WORD x, WORD y)
{
	/* called at interrupt time and (through the AES's drawrat) from
	 * the AES dispatcher: keep the undraw/draw pair atomic */
	unsigned long irqs = irq_save();
	if (HIDE_CNT || mouse_hidden || dispmode)
		cur_pending = 1;
	else
	{
		cur_undraw();
		cur_draw(x, y);
		cur_pending = 0;
	}
	irq_restore(irqs);
}

static VOID ubutvec(WORD b) { (void)b; }
static VOID umotvec(WORD *x, WORD *y) { (void)x; (void)y; }

/* hide/show with interrupts off */
WORD HIDE_CUR(VOID)
{
	unsigned long irqs = irq_save();
	if (HIDE_CNT++ == 0)
		cur_undraw();
	irq_restore(irqs);
	return 0;
}

VOID DIS_CUR(VOID)
{
	unsigned long irqs = irq_save();
	if (HIDE_CNT > 0)
		HIDE_CNT--;
	if (HIDE_CNT == 0 && !dispmode)
	{
		cur_undraw();
		cur_draw(GCURX, GCURY);
	}
	irq_restore(irqs);
}

/* around every VDI call that may draw (gdos.c): take the cursor off the
 * screen, block the interrupt from drawing it, put it back afterwards */
void mouse_block(void)
{
	unsigned long irqs = irq_save();
	mouse_hidden++;
	cur_undraw();
	irq_restore(irqs);
}

void mouse_unblock(void)
{
	unsigned long irqs = irq_save();
	if (mouse_hidden > 0)
		mouse_hidden--;
	if (!mouse_hidden && !HIDE_CNT && !dispmode)
	{
		cur_undraw();
		cur_draw(GCURX, GCURY);
	}
	cur_pending = 0;
	irq_restore(irqs);
}

/* new position (from INT 33h or the keyboard mouse) */
static void mouse_moved(WORD x, WORD y)
{
	if (x < 0) x = 0;
	if (y < 0) y = 0;
	if (x > xres) x = xres;
	if (y > yres) y = yres;
	usermot(&x, &y);
	GCURX = x;
	GCURY = y;
	usercur(x, y);
}

static void mouse_buttons(WORD b)
{
	if (b != MOUSE_BT)
	{
		MOUSE_BT = b;
		userbut(b);
	}
}

/* the INT 33h event handler (AX=000Ch): r0 = events, r1 = buttons,
 * r2 = x, r3 = y; interrupt time */
static void ms_event(unsigned ax, unsigned bx, unsigned cx, unsigned dx)
{
#ifdef GEMDEBUG
	{
		static const char hx[] = "0123456789ABCDEF";
		char b[64], *q = b;
		unsigned v[6] = { ax, cx, dx, HIDE_CNT, mouse_hidden, (usermot == umotvec) | (usercur == mov_cur) << 4 | dispmode << 8 };
		int i, k;
		for (k = 0; k < 6; k++) { for (i = 12; i >= 0; i -= 4) *q++ = hx[(v[k] >> i) & 15]; *q++ = ' '; }
		*q++ = '\n'; *q = 0;
		armdos_debug("ms_event ");
		armdos_debug(b);
	}
#endif
	if (ax & 1)
		mouse_moved((WORD)cx, (WORD)dx);
	if (ax & 0x7E)
		/* GEM's button word: bit 0 left, bit 1 right */
		mouse_buttons((WORD)(bx & 3));
}

/* the keyboard mouse (IBMMDVSP's KEYBOARD_MOUSE): cursor keys move it */
void keyboard_mouse(WORD buttons, WORD dx, WORD dy)
{
	unsigned long irqs = irq_save();
	if (dx || dy)
		mouse_moved(GCURX + dx, GCURY + dy);
	mouse_buttons(buttons);
	irq_restore(irqs);
}

static struct armregs r33;
static int int33(unsigned ax, unsigned bx, unsigned cx, unsigned dx, void *es_dx)
{
	r33.r0 = ax; r33.r1 = bx; r33.r2 = cx; r33.r3 = dx;
	if (es_dx)
		r33.r3 = (unsigned)es_dx;
	_armdos_int33(&r33);
	return r33.r0 & 0xFFFF;
}

static int mouse_present(void)
{
	/* INT 33h vector set and pointing somewhere? */
	return *(volatile unsigned *)(0x33 * 4) != 0;
}

void mouse_init(void)
{
	if (mouse_present() && int33(0, 0, 0, 0, 0) == 0xFFFF)
	{
		int33(7, 0, 0, xres, 0);
		int33(8, 0, 0, yres, 0);
		int33(4, 0, GCURX, GCURY, 0);
		int33(0x0C, 0, 0x1F, 0, (void *)ms_event);
		mouse_on = 1;
#ifdef GEMDEBUG
		armdos_debug("VDI: mouse on\n");
#endif
	}
}

void mouse_exit(void)
{
	if (mouse_on)
	{
		int33(0x0C, 0, 0, 0, 0);
		int33(0, 0, 0, 0, 0);
		mouse_on = 0;
	}
	unsigned long irqs = irq_save();
	cur_drawn = 0;
	irq_restore(irqs);
}

void mouse_set_form(const WORD *f)
{
	int i;
	unsigned long irqs = irq_save();
	if (cur_drawn)
		cur_undraw();
	m_xhot = f[0];
	m_yhot = f[1];
	if (f[3] >= 0 && f[3] < MAX_COLOR)
		m_fg = MAP_COL[f[3]];
	if (f[4] >= 0 && f[4] < MAX_COLOR)
		m_bg = MAP_COL[f[4]];
	for (i = 0; i < 16; i++)
	{
		m_mask[i] = f[5 + i];
		m_data[i] = f[21 + i];
	}
	if (!HIDE_CNT && !mouse_hidden && !dispmode)
		cur_draw(GCURX, GCURY);
	irq_restore(irqs);
}

/* vsc_form: INTIN = xhot, yhot, planes, fg, bg, mask[16], data[16] */
VOID XFM_CRFM(VOID)
{
	mouse_set_form(INTIN);
}

void mouse_default_form(void)
{
	WORD f[37];
	int i;
	f[0] = def_mform[0];
	f[1] = def_mform[1];
	f[2] = 1;
	f[3] = 1;
	f[4] = 0;
	for (i = 0; i < 32; i++)
		f[5 + i] = def_mform[2 + i];
	mouse_set_form(f);
}

/* ------------------------------------------------ vector exchange ----- */

static void *get_vec(void)
{
	return (void *)((UWORD)CONTRL[7] | ((ULONG)(UWORD)CONTRL[8] << 16));
}

static void put_old(void *old)
{
	CONTRL[9] = (UWORD)(ULONG)old;
	CONTRL[10] = (UWORD)((ULONG)old >> 16);
}

VOID VEX_BUTV(VOID)
{
	void *old = (void *)userbut;
	unsigned long irqs = irq_save();
	userbut = (BUTV)get_vec();
	if (!userbut)
		userbut = ubutvec;
	irq_restore(irqs);
	put_old(old);
}

VOID VEX_MOTV(VOID)
{
	void *old = (void *)usermot;
	unsigned long irqs = irq_save();
	usermot = (MOTV)get_vec();
	if (!usermot)
		usermot = umotvec;
	irq_restore(irqs);
	put_old(old);
}

VOID VEX_CURV(VOID)
{
	void *old = (void *)usercur;
	unsigned long irqs = irq_save();
	usercur = (CURV)get_vec();
	if (!usercur)
		usercur = mov_cur;
	irq_restore(irqs);
	put_old(old);
}

/* ------------------------------------------------------------ timer --- */
static void (*old_1c)(struct armregs *);
static WORD tick_on;

extern void sound_tick(void);

static void tick(struct armregs *f)
{
#ifdef GEMDEBUG
	static int n;
	if ((++n & 63) == 0)
		armdos_debug("VDI tick\n");
#endif
	sound_tick();
	usertim();
	if (old_1c)
		_chain_intr(old_1c, f);
}

VOID EX_TIMV(VOID)
{
	void *old = (void *)usertim;
	unsigned long irqs = irq_save();
	usertim = (TIMV)get_vec();
#ifdef GEMDEBUG
	armdos_debug("VDI: EX_TIMV\n");
#endif
	if (!usertim)
		usertim = no_tick;
	if (!tick_on)
	{
		old_1c = _dos_getvect(0x1C);
		_dos_setvect(0x1C, tick);
		tick_on = 1;
	}
	irq_restore(irqs);
	put_old(old);
	INTOUT[0] = 55;		/* milliseconds per tick (18.2 Hz) */
	CONTRL[4] = 1;
}

void timer_exit(void)
{
	if (tick_on)
	{
		unsigned long irqs = irq_save();
		_dos_setvect(0x1C, old_1c);
		tick_on = 0;
		irq_restore(irqs);
	}
}
