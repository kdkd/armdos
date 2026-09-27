/*
 * demoarm.c - ARM-DOS machine layer of the GEM Programmer's Toolkit DEMO
 * sample: replaces PROSTART/TCS (start-up), GEMASM (the AES call), VDIASM
 * (the VDI call), FARDRAW (the PROGDEF drawing entry), LONGASM, DOSASM and
 * DOSBIND (the DOS calls) of the 8086 toolkit.
 *
 * Copyright 2026 Europa Micro Systems; GNU GPL v2 like the rest of apps/gem.
 */
#include "demoapp.h"
#include <armdos.h>
#include <string.h>

/* resident under nothing, but keep the heap out of the XMS anyway */
unsigned _armdos_xms_kb = ~0u;

/* the toolkit's VDI arrays (DEMO.C defines contrl etc. itself) */

/* the AES: svc #0xEF with CX (r2) = 200 and BX (r1) = the parameter block */
WORD gem(LONG pb)
{
	register LONG r1 __asm__("r1") = pb;
	register unsigned r2 __asm__("r2") = 200;
	__asm__ volatile("svc #0xEF" : : "r"(r1), "r"(r2) : "memory", "cc");
	return 0;
}

/* the VDI: svc #0xEF with CX = 0473h and DX (r3) = the parameter block */
extern struct gsx_parameters pblock;
VOID vdi(VOID)
{
	register void *r3 __asm__("r3") = &pblock;
	register unsigned r2 __asm__("r2") = 0x473;
	__asm__ volatile("svc #0xEF" : : "r"(r2), "r"(r3) : "memory", "cc");
}

/* FARDRAW.ASM: the AES calls a PROGDEF's ab_code as fcode(PARMBLK address) */
GLOBAL LONG	drawaddr = (LONG) dr_code;

/* LONGASM.ASM */
VOID LBCOPY(LONG d, LONG s, WORD n)
{
	memmove((void *) d, (const void *) s, (UWORD) n);
}

VOID LWCOPY(LONG d, LONG s, WORD n)
{
	memmove((void *) d, (const void *) s, (ULONG) (UWORD) n * 2);
}

WORD LSTRLEN(LONG s)
{
	return (WORD) strlen((const char *) s);
}

WORD UMUL_DIV(UWORD m1, UWORD m2, UWORD d1)
{
	if (!d1)
		return 0;
	return (WORD) (((ULONG) m1 * m2) / d1);
}

/* ------------------------------------------------------------ DOS ---- */

GLOBAL UWORD	DOS_ERR;

static ULONG int21(struct armregs *r)
{
	DOS_ERR = _armdos_int21(r) ? TRUE : FALSE;
	return r->r0 & 0xFFFF;
}

WORD dos_gdrv(VOID)
{
	struct armregs r = { 0 };
	r.r0 = 0x1900;
	return (WORD) (int21(&r) & 0xFF);
}

VOID dos_gdir(WORD drive, LONG pdrvpath)
{
	struct armregs r = { 0 };
	r.r0 = 0x4700;
	r.r3 = drive;
	r.r4 = pdrvpath;			/* DS:SI */
	int21(&r);
}

WORD dos_open(LONG pname, WORD access)
{
	struct armregs r = { 0 };
	r.r0 = 0x3D00 | (access & 0xFF);
	r.r3 = pname;
	return (WORD) int21(&r);
}

WORD dos_create(LONG pname, WORD attr)
{
	struct armregs r = { 0 };
	r.r0 = 0x3C00;
	r.r2 = attr;
	r.r3 = pname;
	return (WORD) int21(&r);
}

WORD dos_close(WORD handle)
{
	struct armregs r = { 0 };
	r.r0 = 0x3E00;
	r.r1 = handle;
	return (WORD) int21(&r);
}

/* reads and writes of more than 64 KB (a 640x480 picture is 150 KB) go in
 * pieces: CX is 16 bits */
static LONG rw(unsigned ah, WORD handle, LONG cnt, LONG buf)
{
	LONG done = 0;
	while (cnt > 0)
	{
		struct armregs r = { 0 };
		ULONG n = cnt > 0x8000 ? 0x8000 : cnt, got;
		r.r0 = ah << 8;
		r.r1 = handle;
		r.r2 = n;
		r.r3 = buf + done;
		got = int21(&r);
		if (DOS_ERR)
			return done;
		done += got;
		cnt -= got;
		if (got < n)
			break;
	}
	return done;
}

LONG dos_read(WORD handle, LONG cnt, LONG pbuffer) { return rw(0x3F, handle, cnt, pbuffer); }
LONG dos_write(WORD handle, LONG cnt, LONG pbuffer) { return rw(0x40, handle, cnt, pbuffer); }

LONG dos_alloc(LONG nbytes)
{
	struct armregs r = { 0 };
	ULONG seg;
	r.r0 = 0x4800;
	r.r1 = (nbytes + 15) >> 4;
	seg = int21(&r);
	return DOS_ERR ? 0L : (LONG) (seg << 4);
}

WORD dos_free(LONG maddr)
{
	struct armregs r = { 0 };
	r.r0 = 0x4900;
	r.r8 = (ULONG) maddr >> 4;		/* ES */
	return (WORD) int21(&r);
}

/* PROSTART: give memory back to DOS (done by the ARM-DOS start-up code),
 * then the program */
int main(int argc, char **argv)
{
	(void) argc; (void) argv;
	GEMAIN();
	return 0;
}
