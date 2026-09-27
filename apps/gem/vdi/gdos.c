/*
 * gdos.c - GEMVDI.EXE for ARM-DOS: the GEM VDI entry point ("GDOS").
 *
 *	GEMVDI [/C | /H | /V] [program [arguments]]
 *
 * Like the GEM/3 GEMVDI.EXE it installs the INT EFh handler, keeps the
 * workstation table and starts the next layer: GEM.EXE (the AES, in the
 * same directory) unless a program is named.  When that program ends,
 * GEMVDI puts the vector back and ends too.
 *
 * Written for ARM-DOS after DRI's GDOS (GEM/3 VDI source: ENTRY.A86,
 * DRIVER.A86, UTILITY.A86; Copyright 1987 Digital Research, GNU GPL v2 via
 * Caldera 1999): the screen driver is linked in rather than loaded from an
 * SDxxx.SYS file, there are no printer/plotter drivers and no GDOS disk
 * fonts, so this file is new code; the calling convention is GEM's:
 *
 *	svc #0xEF with r2 (CX) = 0473h and r3 (DX) = the parameter block
 *	{ contrl, intin, ptsin, intout, ptsout } of five flat pointers.
 *
 * /C forces the CGA 640x200 driver, /H the Hercules 720x348 one, /V the VGA
 * 640x480 16-colour one; the default is the Hercules card if the machine has
 * one, else the CGA mode.
 */
#include "vdi.h"
#include <stdio.h>
#include <stdlib.h>

#include <string.h>
#include <process.h>
#include <dos.h>
#include <armdos.h>

/* GEM stays resident under the DOS programs it runs: never let a heap that
 * outgrows conventional memory take the extended memory (libdos startup.c
 * would grab the largest free XMS block, leaving none for, say, EDIT). */
unsigned _armdos_xms_kb = ~0u;

WORD	CONTRL[CONTRL_SIZE], INTIN[INTIN_SIZE], PTSIN[PTSIN_SIZE];
WORD	INTOUT[INTOUT_SIZE], PTSOUT[PTSOUT_SIZE];
WORD	FLIP_Y;
WORD	X1, Y1, X2, Y2, LN_MASK, LSTLIN, FG_BP_1, COPYTRAN;

#define MAX_VWS 16
static struct vws vws_tab[MAX_VWS];	/* handle = index + 1 */
struct vws *vw = &vws_tab[0];
static int phys_open;

extern VOID SCREEN(VOID);
extern void vdi_tramp(void);
extern void sound_tick(void);

struct pblock {
	WORD	*contrl, *intin, *ptsin, *intout, *ptsout;
};

static void init_vws(struct vws *v)
{
	int i;
	memset(v, 0, sizeof(*v));
	for (i = 0; i < 6; i++)
		v->a_line_styl[i] = line_sty_def[i];
	for (i = 0; i < 16; i++)
		v->a_ud_patrn[i] = ud_patrn_def[i];
	v->a_used = 1;
}

struct vws *vws_alloc(void)
{
	int i;
	for (i = 1; i < MAX_VWS; i++)
		if (!vws_tab[i].a_used)
		{
			init_vws(&vws_tab[i]);
			vws_tab[i].a_handle = i + 1;
			return &vws_tab[i];
		}
	return NULL;
}

/* NDC (0..32767, y up) <-> raster coordinates, as DRIVER.A86 */
static void ndc_in(int n)
{
	int i;
	for (i = 0; i < n; i++)
	{
		LONG x = PTSIN[2 * i], y = PTSIN[2 * i + 1];
		PTSIN[2 * i] = (WORD)(((x * 2) * (dev->d_xres + 1)) >> 16);
		PTSIN[2 * i + 1] = (WORD)(dev->d_yres - (((y * 2) * (dev->d_yres + 1)) >> 16));
	}
}

static void ndc_out(int n, int flip)
{
	int i;
	for (i = 0; i < n; i++)
	{
		LONG x = PTSOUT[2 * i], y = PTSOUT[2 * i + 1];
		LONG xr = dev->d_xres + 1, yr = dev->d_yres + 1;
		PTSOUT[2 * i] = (WORD)((((x << 16) + xr - 1) / xr) >> 1);
		if (!flip)
			y = yr - 1 - y;
		PTSOUT[2 * i + 1] = (WORD)((((y << 16) + yr - 1) / yr) >> 1);
	}
}

/* calls during which the cursor must stay off the screen */
static int draws(int op)
{
	switch (op)
	{
	case 3: case 5: case 6: case 7: case 8: case 9: case 11:
	case 109: case 114: case 121:
		return 1;
	}
	return 0;
}

/* copy in, run the driver, copy out: ENTRY.A86 */
static void call_driver(struct pblock *pb)
{
	int i, n, blk;

	for (i = 0; i < 12; i++)
		CONTRL[i] = pb->contrl[i];
	CONTRL[2] = CONTRL[4] = 0;
	n = CONTRL[1] * 2;
	if (n < 0)
		n = 0;
	if (n > PTSIN_SIZE - 2)
	{
		n = PTSIN_SIZE - 2;
		CONTRL[1] = n / 2;
	}
	for (i = 0; i < n; i++)
		PTSIN[i] = pb->ptsin[i];
	n = CONTRL[3];
	if (n < 2)
		n = 2;
	if (n > INTIN_SIZE)
	{
		n = INTIN_SIZE;
		CONTRL[3] = n;
	}
	for (i = 0; i < n; i++)
		INTIN[i] = pb->intin ? pb->intin[i] : 0;
	FLIP_Y = 0;
	if (vw->a_xfm_mode != 2 && CONTRL[1] && CONTRL[0] != 1 && CONTRL[0] != 100)
		ndc_in(CONTRL[1]);

	blk = !dispmode && draws(CONTRL[0]) && !HIDE_CNT;
	if (blk)
		mouse_block();
	SCREEN();
	if (blk)
		mouse_unblock();

	if (vw->a_xfm_mode != 2 && CONTRL[2])
		ndc_out(CONTRL[2], FLIP_Y);
	pb->contrl[2] = CONTRL[2];
	pb->contrl[4] = CONTRL[4];
	pb->contrl[7] = CONTRL[7];
	pb->contrl[8] = CONTRL[8];
	pb->contrl[9] = CONTRL[9];
	pb->contrl[10] = CONTRL[10];
	if (CONTRL[2] && pb->ptsout)
		for (i = 0; i < CONTRL[2] * 2 && i < PTSOUT_SIZE; i++)
			pb->ptsout[i] = PTSOUT[i];
	if (CONTRL[4] && pb->intout)
		for (i = 0; i < CONTRL[4] && i < INTOUT_SIZE; i++)
			pb->intout[i] = INTOUT[i];
}

/* the GDOS escape -1 (sub-function 1): exec a program - gsx_exec of the
 * AES: INTIN[0..1] = path, [2..3] = the DOS exec block */
static void gdos_exec(struct pblock *pb)
{
	const char *path = (const char *)((UWORD)pb->intin[0] | ((ULONG)(UWORD)pb->intin[1] << 16));
	const UBYTE *eb = (const UBYTE *)((UWORD)pb->intin[2] | ((ULONG)(UWORD)pb->intin[3] << 16));
	struct armregs r;
	memset(&r, 0, sizeof(r));
	r.r0 = 0x4B00;
	r.r3 = (unsigned)path;
	r.r1 = (unsigned)eb;
	_armdos_int21(&r);
	pb->contrl[4] = 1;
	if (pb->intout)
		pb->intout[0] = (r.cpsr & ARM_CPSR_C) ? -1 : 0;
}

/* the C side of INT EFh (in SYS mode, on the caller's stack) */
void vdi_entry(struct pblock *pb)
{
	int op = pb->contrl[0], h = pb->contrl[6];
	struct vws *v;
#ifdef GEMTRACE
	if (op != 128 && op != 31 && op != 33)
	{
		static const char hx[] = "0123456789ABCDEF";
		char b[16];
		b[0] = hx[(op >> 8) & 15]; b[1] = hx[(op >> 4) & 15]; b[2] = hx[op & 15];
		b[3] = '/'; b[4] = hx[h & 15]; b[5] = ' '; b[6] = 0;
		armdos_debug(b);
	}
#endif

	if (op == -1)
	{
		if (pb->contrl[5] == 1)
			gdos_exec(pb);
		return;
	}
	if (op == 1)
	{
		/* open workstation: only screens (ids 1-10) */
		if (pb->intin[0] < 1 || pb->intin[0] > 10)
		{
			pb->contrl[6] = 0;
			return;
		}
		vw = &vws_tab[0];
		init_vws(vw);
		vw->a_handle = 1;
		vw->a_xfm_mode = pb->intin[10];
		dev_tables();
		call_driver(pb);
		phys_open = 1;
		pb->contrl[6] = 1;
		return;
	}
	if (op == 100)
	{
		if (!phys_open || !(v = vws_alloc()))
		{
			pb->contrl[6] = 0;
			return;
		}
		vw = v;
		vw->a_xfm_mode = pb->intin[10];
		call_driver(pb);
		pb->contrl[6] = v->a_handle;
		return;
	}
	if (h < 1 || h > MAX_VWS || !vws_tab[h - 1].a_used)
		return;
	vw = &vws_tab[h - 1];
	if (op == 101)
	{
		if (h != 1)
			vw->a_used = 0;
		vw = &vws_tab[0];
		return;
	}
	call_driver(pb);
#ifdef GEMTRACE
	if (op == 122 || op == 123)
	{
		char b[4];
		b[0] = '='; b[1] = '0' + (HIDE_CNT & 15); b[2] = ' '; b[3] = 0;
		armdos_debug(b);
	}
#endif
	if (op == 2)
	{
		int i;
		for (i = 1; i < MAX_VWS; i++)
			vws_tab[i].a_used = 0;
		phys_open = 0;
	}
}

/* ------------------------------------------------------------ INT EFh */

static armdos_vect_t old_ef;

static void int_ef(struct armregs *f)
{
	if ((f->r2 & 0xFFFF) != 0x473)
	{
		armdos_callold(old_ef, f);
		return;
	}
	if ((f->cpsr & 0x1F) == 0x13)
	{
		/* called in SVC mode (from an interrupt handler): run here */
		vdi_entry((struct pblock *)f->r3);
		return;
	}
	/* push the return address on the caller's stack and resume the
	 * caller in vdi_tramp, which calls vdi_entry and returns: the VDI runs
	 * in the caller's mode with interrupts as the caller had them */
	{
		uint32_t *sp = (uint32_t *)f->sp;
		*--sp = f->pc | ((f->cpsr & ARM_CPSR_T) ? 1 : 0);
		f->sp = (uint32_t)sp;
		f->pc = (uint32_t)vdi_tramp;
		f->cpsr &= ~ARM_CPSR_T;
	}
}

static void cleanup(void)
{
	if (!dispmode)
		DINIT_G();
	timer_exit();
	_dos_setvect(0xEF, old_ef);
}

int main(int argc, char **argv)
{
	int req = -1, i, rc;
	char path[128], *p;
	char *args[32];
	int na = 0;

	for (i = 1; i < argc && argv[i][0] == '/'; i++)
	{
		switch (argv[i][1] | 0x20)
		{
		case 'c': req = DEV_CGA; break;
		case 'h': req = DEV_HGC; break;
		case 'v': req = DEV_VGA; break;
		default:
			printf("GEMVDI [/C | /H | /V] [program [arguments]]\n");
			return 1;
		}
	}
	dev_detect(req);
	if (i < argc)
	{
		strncpy(path, argv[i], sizeof(path) - 1);
		path[sizeof(path) - 1] = 0;
		i++;
	}
	else
	{
		/* GEM.EXE next to GEMVDI.EXE */
		strncpy(path, argv[0], sizeof(path) - 16);
		path[sizeof(path) - 16] = 0;
		p = strrchr(path, '\\');
		if (!p)
			p = strchr(path, ':');
		p = p ? p + 1 : path;
		strcpy(p, "GEM.EXE");
	}
	args[na++] = path;
	for (; i < argc && na < 31; i++)
		args[na++] = argv[i];
	args[na] = NULL;

	old_ef = _dos_getvect(0xEF);
	_dos_setvect(0xEF, int_ef);
	rc = spawnv(P_WAIT, path, args);
	cleanup();
	if (rc < 0)
	{
		printf("GEMVDI: cannot run %s\n", path);
		return 1;
	}
	return rc;
}
