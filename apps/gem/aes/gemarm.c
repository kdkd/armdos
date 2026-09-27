/*
 * gemarm.c - ARM-DOS machine layer of the GEM AES.
 *
 * Replaces the x86 assembler modules of the GEM/3 AES (Copyright 1987 Digital
 * Research, GNU GPL v2 via Caldera 1999): GEMSTART.A86 (start-up, desk
 * accessory loader), GEMGMAIN.A86, GEMDOSIF.A86 (DOS calls, the INT EFh and
 * INT 24h hooks, the mouse/timer call-backs), GSX2.A86 (VDI calls),
 * LARGE.A86 (long-pointer copies) and OPTIMOPT.A86 (small helpers).  The
 * dispatcher is in gemasm.S.
 *
 * The AES is reached by   svc #0xEF   with r2 (CX) = 200 and r1 (BX) = the
 * parameter block - ES:BX on the PC.  Other CX values go to the VDI.
 */
#include "aes.h"
#include <armdos.h>

#undef strlen
#undef strcmp
#undef strcpy
extern void *memcpy(void *, const void *, unsigned long);
extern void *memset(void *, int, unsigned long);

EXTERN LONG	ad_envrn, ad_stail, ad_s1fcb, ad_s2fcb;
EXTERN ULONG	DOS_AX, DOS_BX, DOS_CX, DOS_DX, DOS_DS, DOS_ES, DOS_SI, DOS_DI, DOS_ERR;
EXTERN WORD	gl_play, gl_bdely;
EXTERN WORD	contrl[], intin[], ptsin[], intout[], ptsout[];
EXTERN VOID	gem_main(VOID);

GLOBAL LONG	ad_psp;
GLOBAL WORD	PARABEG;
GLOBAL WORD	gintstk;
GLOBAL WORD	totpds;			/* GEMSTART.A86 */
GLOBAL LONG	CMP_TICK, NUM_TICK;
GLOBAL LONG	tikaddr = (LONG) tikcod;
GLOBAL LONG	tiksav;
GLOBAL LONG	drwaddr = (LONG) justretf;

/* ----------------------------------------------------------- start --- */

static struct psp *mypsp;

int main(int argc, char **argv)
{
	(void)argc; (void)argv;
	mypsp = _armdos_psp;
	gem_dbgx("AES: main at ", (ULONG) main);
	ad_psp = (LONG) mypsp;
	PARABEG = (WORD) ((ULONG) mypsp >> 4);
	ad_envrn = (LONG) mypsp->envseg << 4;
	ad_s1fcb = (LONG) mypsp + 0x5C;
	ad_s2fcb = (LONG) mypsp + 0x6C;
	ad_stail = (LONG) mypsp + 0x80;
	/* all the process descriptors (GEMSTART's memshrnk: 5, or 2 when
	 * there is no room for desk accessories) */
	totpds = NUM_PDS;
	/* GEMVDI must be there (it owns INT EFh before us) */
	if (!ARMDOS_IVT[0xEF])
	{
		static const char msg[] = "GEMVDI not present in memory.\r\nExecution terminated.\r\n$";
		struct armregs r;
		memset(&r, 0, sizeof(r));
		r.r0 = 0x0900;
		r.r3 = (unsigned) msg;
		_armdos_int21(&r);
		return 1;
	}
	gem_main();
	return 0;
}

WORD dos_regs(struct armregs *r);

/* EXEC: an application (or the DOS program) runs as our child.  Its AES
 * calls use the current process's supervisor stack: point it below here
 * (below the frame the kernel saves on this stack for the EXEC, too). */
VOID __EXEC(VOID)
{
	struct armregs r;
	int cf;
	UDA *u = rlr->p_uda;
	LONG *save = u->u_spsuper;

	u->u_spsuper = (LONG *)((get_sp() - 512) & ~7L);
	dos_regs(&r);
	cf = _armdos_int21(&r);
	u = rlr->p_uda;
	u->u_spsuper = save;
	DOS_AX = r.r0 & 0xFFFF;
	DOS_ERR = cf ? 1 : 0;
}

VOID cli(VOID) { armdos_disable(); }
VOID sti(VOID) { armdos_enable(); }

/* the dispatcher has nothing to run: wait for an interrupt (ARCH.md 2,
 * CP15 c7 WFI) instead of spinning, so an idle desktop costs no host CPU.
 * Every event GEM waits for (keys, the mouse, the timer) comes from an
 * IRQ, and WFI wakes on a pending IRQ even with IRQs masked, so testing
 * the lists with IRQs off and then sleeping cannot miss a wake-up. */
VOID gem_idle(VOID)
{
	armdos_disable();
	if (!rlr && !fpcnt)
		__asm__ volatile("mcr p15, 0, %0, c7, c0, 4" : : "r"(0) : "memory");
	armdos_enable();
}

/* ------------------------------------------------------ INT 24h ---- */

static armdos_vect_t old24;
static WORD err_taken;

/*
 * A critical error while GEM owns the screen.  The PC AES (GEMDOSIF.A86
 * err_trap) called eralert() from inside the INT 24h handler, running the
 * dispatcher with DOS in the middle of the call, and on Cancel returned to
 * the program with a pseudo error (64 + the error).  Here the handler runs
 * in SVC mode inside the kernel's INT 21h, where switching GEM processes
 * is not safe, so the call is failed at once (AL = 3) and the alert is put
 * up at the program's next AES call (crit_alert, from xif) - the same
 * alert, with the drive letter.  Retry cannot retry any more; both buttons
 * leave the call failed.  The program's own reaction to the failed call
 * (an alert of its own, e.g. the Desktop's "path too long") is the next
 * form_alert/form_error, which is then answered without being shown, as the
 * PC's pseudo error codes kept programs quiet.
 */
static const UBYTE crit_tbl[13] = { 0, 5, 1, 5, 2, 5, 2, 2, 2, 4, 3, 3, 3 };
static volatile WORD crit_pending, crit_n, crit_d;
static PD *volatile crit_pd;
static volatile WORD crit_quiet;	/* alert shown: until the program waits
					 * for an event again, more critical
					 * errors fail without another one */
static WORD crit_eat;			/* its own alert still to swallow */

static void err_trap(struct armregs *f)
{
	if (!crit_pending && !crit_quiet)
	{
		if (f->r0 & 0x8000)		/* a character device */
		{
			crit_n = 4;
			crit_d = -1;
		}
		else
		{
			UWORD e = f->r5 & 0xFF;	/* DI: the error code */
			crit_n = crit_tbl[e > 12 ? 12 : e];
			crit_d = f->r0 & 0xFF;	/* AL: the drive */
		}
		crit_pd = rlr;
		crit_pending = 1;
	}
	f->r0 = (f->r0 & ~0xFFu) | 3;		/* AL = 3: fail */
}

/* called by xif() before each AES call: TRUE = answer it with 1 unseen */
WORD crit_alert(WORD opcode)
{
	if (crit_pending && crit_pd == rlr)
	{
		crit_quiet = 1;
		crit_eat = 1;
		eralert(crit_n, crit_d);
		crit_pending = 0;
	}
	if (crit_quiet && crit_pd == rlr)
	{
		if (crit_eat && (opcode == FORM_ALERT || opcode == FORM_ERROR))
		{
			crit_eat = 0;
			return TRUE;
		}
		if (opcode >= EVNT_KEYBD && opcode <= EVNT_DCLICK)
			crit_quiet = crit_eat = 0;
	}
	return FALSE;
}

VOID takeerr(VOID)
{
	if (!err_taken)
	{
		old24 = ARMDOS_IVT[0x24];
		ARMDOS_IVT[0x24] = err_trap;
		err_taken = 1;
	}
}

VOID giveerr(VOID)
{
	if (err_taken)
	{
		ARMDOS_IVT[0x24] = old24;
		err_taken = 0;
	}
}

/* ------------------------------------------------------ INT EFh ---- */

static armdos_vect_t oldef;
static WORD ef_taken;

static void cpmcod(struct armregs *f)
{
	unsigned cx = f->r2 & 0xFFFF;
	if (cx != 200 && cx != 201)
	{
		armdos_callold(oldef, f);		/* the VDI */
		return;
	}
	if (cx == 201)
		return;
	/* resume the caller in aes_tramp (gemasm.S), return address pushed */
	{
		uint32_t *sp = (uint32_t *) f->sp;
		*--sp = f->pc | ((f->cpsr & ARM_CPSR_T) ? 1 : 0);
		f->sp = (uint32_t) sp;
		f->pc = (uint32_t) aes_tramp;
		f->cpsr &= ~ARM_CPSR_T;
	}
}

VOID takecpm(VOID)
{
	if (!ef_taken)
	{
		oldef = ARMDOS_IVT[0xEF];
		ARMDOS_IVT[0xEF] = cpmcod;
		ef_taken = 1;
	}
}

VOID givecpm(VOID)
{
	if (ef_taken)
	{
		ARMDOS_IVT[0xEF] = oldef;
		ef_taken = 0;
	}
}

/* after a DOS program: take the vectors back */
VOID retake(VOID)
{
	ARMDOS_IVT[0xEF] = cpmcod;
	if (err_taken)
		ARMDOS_IVT[0x24] = err_trap;
}

VOID setdsss(UDA *u)
{
	u->u_insuper = 1;
}

VOID supret(WORD x)
{
	(void)x;
}

/* --------------------------------------- mouse and timer call-backs -- */

VOID justretf(VOID) { }

/* the VDI's button vector (interrupt time) */
VOID far_bcha(WORD buttons)
{
	if (!gl_play)
		b_click(buttons);
}

/* the VDI's motion vector (interrupt time) */
VOID far_mcha(WORD *x, WORD *y)
{
	if (!gl_play)
		m_forkq((WORD (*)()) mchange, *x, *y);
}

/* draw the cursor with the VDI's own routine; 0 = drawn */
WORD drawrat(WORD x, WORD y)
{
	((VOID (*)(WORD, WORD)) drwaddr)(x, y);
	return 0;
}

/* the timer vector (interrupt time, 18.2 Hz): the event timer and the
 * button delay, then the previous vector */
VOID tikcod(VOID)
{
#ifdef GEMTRACE
	static WORD n;
	if ((++n & 31) == 0)
		gem_dbgx("tick ", (ULONG) CMP_TICK | ((ULONG) gl_bdely << 16));
#endif
	if (CMP_TICK)
	{
		NUM_TICK++;
		if (--CMP_TICK == 0)
			forkq((WORD (*)()) tchange, (WORD) LLOWD(NUM_TICK), (WORD) LHIWD(NUM_TICK));
	}
	if (gl_bdely)
		b_delay(1);
	if (tiksav)
		((VOID (*)(VOID)) tiksav)();
}

/* a G_USERDEF object's drawing code: C function taking the PARMBLK */
WORD far_call(WORD (*fcode)(LONG), LONG fdata)
{
	return (*fcode)(fdata);
}

/* --------------------------------------------- desk accessories ------ */

/*
 * Desk accessories are ARM-DOS programs (AR1 images, built with the SDK)
 * that the AES loads into memory of its own and runs as GEM processes, as
 * GEMSTART's pgmld did with DOS function 4B03h.  Each gets a block with a
 * copy of the AES's PSP in front, so the SDK start-up code finds the
 * normal entry state (r0 = PSP, r1 = load base, r2 = block end, sp).
 */
struct accinfo {
	LONG	entry;
	LONG	psp, base, end, sp;
};
static struct accinfo accs[NUM_ACCS];
static WORD naccs;

static UWORD rd16(const UBYTE *p) { return p[0] | (p[1] << 8); }
static ULONG rd32(const UBYTE *p) { return rd16(p) | ((ULONG) rd16(p + 2) << 16); }

WORD pgmld(WORD handle, BYTE *ploadname, LONG *pldaddr, WORD *paccroom)
{
	UBYTE hdr[64], ar1[64];
	ULONG lfanew, image_off, image_size, bss, stack, entry, roff, rcount, total, i;
	UWORD paras, seg;
	UBYTE *blk, *base;
	struct armregs r;

	(void)ploadname;
	gem_dbg("pgmld ");
	gem_dbg(ploadname);
	gem_dbgx(" room ", *paccroom);
	if (naccs >= NUM_ACCS)
		return -1;
	dos_lseek(handle, 0, 0L);
	if (dos_read(handle, 64, ADDR(hdr)) != 64 || hdr[0] != 'M' || hdr[1] != 'Z')
		return -1;
	lfanew = rd32(hdr + 0x3C);
	dos_lseek(handle, 0, (LONG) lfanew);
	if (dos_read(handle, 64, ADDR(ar1)) != 64 || ar1[0] != 'A' || ar1[1] != 'R' || ar1[2] != '1')
		return -1;
	image_off = rd32(ar1 + 8);
	image_size = rd32(ar1 + 12);
	bss = rd32(ar1 + 16);
	stack = rd32(ar1 + 20);
	entry = rd32(ar1 + 24);
	roff = rd32(ar1 + 28);
	rcount = rd32(ar1 + 32);
	total = 0x100 + ((image_size + bss + 15) & ~15UL) + stack + 512;
	paras = (UWORD) ((total + 15) >> 4);
	if (paras > *paccroom)
		return -1;
	memset(&r, 0, sizeof(r));
	r.r0 = 0x4800;
	r.r1 = paras;
	if (_armdos_int21(&r))
		return -1;
	seg = r.r0 & 0xFFFF;
	blk = (UBYTE *) ((ULONG) seg << 4);
	base = blk + 0x100;
	/* the image */
	dos_lseek(handle, 0, (LONG) image_off);
	for (i = 0; i < image_size; )
	{
		WORD n = (image_size - i > 16384) ? 16384 : (WORD) (image_size - i);
		if (dos_read(handle, n, ADDR(base + i)) != n)
			goto fail;
		i += n;
	}
	memset(base + image_size, 0, bss);
	/* relocations: u32 image offsets */
	dos_lseek(handle, 0, (LONG) roff);
	for (i = 0; i < rcount; i++)
	{
		UBYTE rb[4];
		ULONG off;
		if (dos_read(handle, 4, ADDR(rb)) != 4)
			goto fail;
		off = rd32(rb);
		if (off + 4 <= image_size)
		{
			ULONG v = rd32(base + off) + (ULONG) base;
			base[off] = v; base[off + 1] = v >> 8; base[off + 2] = v >> 16; base[off + 3] = v >> 24;
		}
	}
	/* a PSP: ours, with this block's size and an empty command tail */
	memcpy(blk, mypsp, 0x100);
	((struct psp *) blk)->memtop = seg + paras;
	blk[0x80] = 0;
	blk[0x81] = 0x0D;
	*paccroom -= paras;
	accs[naccs].entry = (LONG) base + entry;
	accs[naccs].psp = (LONG) blk;
	accs[naccs].base = (LONG) base;
	accs[naccs].end = (LONG) blk + ((ULONG) paras << 4);
	accs[naccs].sp = (accs[naccs].end - 16) & ~7L;
	*pldaddr = accs[naccs].entry;
	naccs++;
	return 0;
fail:
	memset(&r, 0, sizeof(r));
	r.r0 = 0x4900;
	r.r8 = seg;
	_armdos_int21(&r);
	return -1;
}

extern VOID enter_prog(LONG entry, LONG psp, LONG base, LONG end, LONG sp);

/* a new desk accessory process starts here (pstart(gotopgm, ...)): the
 * process's AES calls will use the stack we are on now */
VOID gotopgm(VOID)
{
	UDA *u = rlr->p_uda;
	int i;
	u->u_insuper = 0;
	u->u_spsuper = (LONG *) ((get_sp() - 64) & ~7L);
	for (i = 0; i < naccs; i++)
		if (accs[i].entry == rlr->p_ldaddr)
			enter_prog(accs[i].entry, accs[i].psp, accs[i].base, accs[i].end, accs[i].sp);
	for (;;)
		dsptch();
}
