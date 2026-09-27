/*
 * deskarm.c - ARM-DOS machine layer of the GEM Desktop.
 *
 * Replaces DESKSTAR.A86 (start-up, the AES call) and DESKOSIF.A86 (console
 * helpers, the keyboard/DOS hooks used while formatting a disk) of the GEM/3
 * Desktop (Copyright 1987 Digital Research, GNU GPL v2 via Caldera 1999).
 * The shared helpers are in common/gemrt.c.
 */
#include "desk.h"
#include <armdos.h>

extern void *memset(void *, int, unsigned long);

/* the AES: svc #0xEF with CX (r2) = 200 and BX (r1) = the parameter block */
WORD gem(LONG pb)
{
	register LONG r1 __asm__("r1") = pb;
	register unsigned r2 __asm__("r2") = 200;
	__asm__ volatile("svc #0xEF" : : "r"(r1), "r"(r2) : "memory", "cc");
	return 0;
}

VOID __EXEC(VOID)
{
}

VOID chrout(WORD ch)
{
	struct armregs r;
	memset(&r, 0, sizeof(r));
	r.r0 = 0x0200;
	r.r3 = ch & 0xFF;
	_armdos_int21(&r);
}

WORD chrin(VOID)
{
	struct armregs r;
	do
	{
		memset(&r, 0, sizeof(r));
		r.r0 = 0x0600;
		r.r3 = 0xFF;
		_armdos_int21(&r);
	} while (r.cpsr & ARM_CPSR_Z);
	return r.r0 & 0xFF;
}

/*
 * The hooks that run FORMAT under the Desktop (do_format() in desksupp.c:
 * takedos/takekey/takevid, romerr() runs the program, give... undo them).
 * As DESKOSIF.A86 did: the screen output of the program is swallowed
 * (INT 10h, and the console functions of INT 21h), and its questions are
 * answered from here - Enter for "insert a disk and press ENTER" and the
 * volume label, N for "Format another (Y/N)?".  DESKOSIF answered from a
 * fixed list (CR, N, CR); here the answer looks at what the program has
 * just printed, because ARM-DOS's FORMAT asks for a volume label too.
 * After too many questions the program is ended (INT 21h AH=4Ch), as
 * DESKOSIF did after three buffered reads.
 */
static armdos_vect_t old10, old16, old21;
static char said[64];			/* the last characters printed */
static WORD nasked;			/* questions answered */

static void heard(int c)
{
	int i;
	for (i = 0; i < (int) sizeof(said) - 1; i++)
		said[i] = said[i + 1];
	said[sizeof(said) - 1] = (char) c;
}

static int asked_yn(void)
{
	int i;
	for (i = 0; i + 4 < (int) sizeof(said); i++)
		if (said[i] == 'Y' && said[i + 1] == '/' && said[i + 2] == 'N')
			return 1;
	return 0;
}

/* the answer to the question just asked: an ASCII code */
static int answer(void)
{
	int c = asked_yn() ? 'N' : '\r';
	nasked++;
	heard(' ');			/* one answer per question */
	memset(said, ' ', sizeof(said));
	return c;
}

static void vid_hook(struct armregs *f)
{
	if (((f->r0 >> 8) & 0xFF) == 0x0E)
		heard(f->r0 & 0xFF);
	/* everything else is swallowed too: the Desktop owns the screen */
}

static void key_hook(struct armregs *f)
{
	unsigned ah = (f->r0 >> 8) & 0xFF;
	if (ah == 0x00 || ah == 0x10 || ah == 0x01 || ah == 0x11)
	{
		int c = answer();
		f->r0 = (f->r0 & 0xFFFF0000u) | (c == 'N' ? 0x314E : 0x1C0D);
		f->cpsr &= ~ARM_CPSR_Z;		/* a key is there */
		return;
	}
	armdos_callold(old16, f);
}

static void dos_hook(struct armregs *f)
{
	unsigned ah = (f->r0 >> 8) & 0xFF, al = f->r0 & 0xFF;

	if (ah == 0x0C)				/* flush, then function AL */
	{
		if (al != 0x01 && al != 0x06 && al != 0x07 && al != 0x08 && al != 0x0A)
			return;
		ah = al;
	}
	if (nasked > 12)			/* it keeps asking: end it */
	{
		f->r0 = 0x4C00;
		armdos_callold(old21, f);
		return;
	}
	switch (ah)
	{
	case 0x01: case 0x07: case 0x08:
		f->r0 = (f->r0 & ~0xFFu) | answer();
		return;
	case 0x02:
		heard(f->r3 & 0xFF);
		return;
	case 0x06:
		if ((f->r3 & 0xFF) == 0xFF)
		{
			f->r0 = (f->r0 & ~0xFFu) | answer();
			f->cpsr &= ~ARM_CPSR_Z;
		}
		else
			heard(f->r3 & 0xFF);
		return;
	case 0x09:
	{
		const char *p = (const char *) f->r3;
		while (*p != '$')
			heard(*p++);
		return;
	}
	case 0x0A:
	{
		UBYTE *b = (UBYTE *) f->r3;
		int c = answer();
		if (c == '\r')
			b[1] = 0, b[2] = '\r';
		else
			b[1] = 1, b[2] = c, b[3] = '\r';
		return;
	}
	case 0x0B:
		f->r0 = (f->r0 & ~0xFFu) | 0xFF;
		return;
	case 0x40:				/* writes to the screen */
		if ((f->r1 & 0xFFFF) == 1 || (f->r1 & 0xFFFF) == 2)
		{
			const char *p = (const char *) f->r3;
			unsigned n = f->r2 & 0xFFFF, i;
			for (i = 0; i < n; i++)
				heard(p[i]);
			f->r0 = (f->r0 & 0xFFFF0000u) | n;
			f->cpsr &= ~ARM_CPSR_C;
			return;
		}
		break;
	}
	armdos_callold(old21, f);
}

VOID takedos(VOID)
{
	nasked = 0;
	memset(said, ' ', sizeof(said));
	old21 = armdos_getvect(0x21);
	armdos_setvect(0x21, dos_hook);
}

VOID givedos(VOID)
{
	armdos_setvect(0x21, old21);
}

VOID takekey(VOID)
{
	old16 = armdos_getvect(0x16);
	armdos_setvect(0x16, key_hook);
}

WORD givekey(VOID)
{
	armdos_setvect(0x16, old16);
	return nasked;
}

VOID takevid(VOID)
{
	old10 = armdos_getvect(0x10);
	armdos_setvect(0x10, vid_hook);
}

VOID givevid(VOID)
{
	armdos_setvect(0x10, old10);
}

int main(int argc, char **argv)
{
	(void)argc; (void)argv;
	gem_dbgx("DESK: main at ", (ULONG) main);
	if (!ARMDOS_IVT[0xEF])
	{
		static const char msg[] = "GEMAES not present in memory.\r\nExecution terminated.\r\n$";
		struct armregs r;
		memset(&r, 0, sizeof(r));
		r.r0 = 0x0900;
		r.r3 = (unsigned) msg;
		_armdos_int21(&r);
		return 1;
	}
	gemain();
	return 0;
}

/* DESKOSIF's far_draw: the G_USERDEF drawing routine of the text-mode
 * directory windows; the AES calls it as fcode(PARMBLK address) */
GLOBAL LONG	drawaddr = (LONG) dr_code;
