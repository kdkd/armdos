/* sys_armdos.c -- the ARM-DOS system driver for Hexen II (H2.EXE)
 * after uHexen2's hexen2/sys_dos.c and the ARM-DOS Quake's sys_armdos.c.
 *
 * Copyright (C) 1996-1997  Id Software, Inc.
 * Copyright (C) 1997-1998  Raven Software Corp.
 * Copyright (C) 2005-2026  O.Sezer and the uHexen2 contributors
 * Copyright (C) 2026 the ARM-DOS project.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 *
 * H2DOS.EXE (uHexen2's DJGPP build) ran under CWSDPMI with everything in
 * extended memory. H2.EXE is the same thing on the ARM-PC, talking to DOS
 * and the hardware directly:
 *
 *  * files:    newlib stdio/open -> INT 21h (binary mode).
 *  * memory:   like H2DOS.EXE under its DOS extender, the whole program
 *              (530 KB of code, 2.2 MB of tables, a 1 MB stack) runs in
 *              extended memory: the stub in front of it (loader/h2load.c)
 *              loads it into an XMS block (HIMEM.SYS). The hunk (-mem N,
 *              default: as much as there is) comes from the C runtime's
 *              heap, which then lives in XMS too.
 *  * time:     PIT channel 0 in mode 2 at ~1 kHz with an INT 08h handler;
 *              the fine time is ticks * divisor + the latched count. The
 *              BIOS tick is chained every 65536 PIT counts.
 *  * keyboard: an INT 09h handler puts scan codes (port 60h) in a ring
 *              buffer; Sys_SendKeyEvents turns them into Key_Event()s.
 *  * idle:     WFI while the game is ahead of its 72 fps frame clock.
 */

#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>

#include <armdos.h>

#include "quakedef.h"
#include "debuglog.h"
#include "sys_armdos.h"

/* <dos.h> defines "interrupt" etc.; only the findfirst part is needed */
struct find_t {
	char reserved[21];
	char attrib;
	unsigned short wr_time;
	unsigned short wr_date;
	unsigned long size;
	char name[13];
} __attribute__((packed));
unsigned _dos_findfirst(const char *path, unsigned attrib, struct find_t *f);
unsigned _dos_findnext(struct find_t *f);

#define outb(p, v)	armdos_outb((p), (uint8_t)(v))
#define inb(p)		armdos_inb(p)

/* Without HIMEM.SYS, let the heap use raw extended memory (INT 15h AH=88h). */
unsigned _armdos_raw_extmem = 1;

static quakeparms_t	quakeparms;
qboolean		isDedicated;

cvar_t	sys_nostdout = {"sys_nostdout", "0", CVAR_NONE};
cvar_t	sys_throttle = {"sys_throttle", "0.02", CVAR_ARCHIVE};

/* ---------------------------------------------------------------- timer -- */

#define PIT_CLOCK	1193182.0
#define PIT_DIVISOR	1193u		/* 1000.15 Hz */

static armdos_vect_t	old_int08, old_int09;
static int	timer_installed, keyboard_installed;
static volatile unsigned	timer_ticks;
static unsigned	bios_acc;

static void int08_handler (struct armregs *f)
{
	timer_ticks++;
	bios_acc += PIT_DIVISOR;
	if (bios_acc >= 65536u)
	{
		bios_acc -= 65536u;
		if (old_int08)
		{
			old_int08 (f);		/* the BIOS sends the EOI */
			return;
		}
	}
	outb (0x20, 0x20);
}

static void timer_startup (void)
{
	old_int08 = armdos_getvect (0x08);
	armdos_disable ();
	armdos_setvect (0x08, int08_handler);
	outb (0x43, 0x34);		/* channel 0, lo/hi, mode 2 */
	outb (0x40, PIT_DIVISOR & 0xFF);
	outb (0x40, PIT_DIVISOR >> 8);
	timer_installed = 1;
	armdos_enable ();
}

static void timer_shutdown (void)
{
	if (!timer_installed)
		return;
	armdos_disable ();
	outb (0x43, 0x36);		/* mode 3, divisor 65536: 18.2 Hz */
	outb (0x40, 0);
	outb (0x40, 0);
	armdos_setvect (0x08, old_int08);
	timer_installed = 0;
	armdos_enable ();
}

/* PIT counts since the timer was started */
static unsigned long long pit_counts (void)
{
	unsigned	t1, t2, count;

	do
	{
		t1 = timer_ticks;
		armdos_disable ();
		outb (0x43, 0x00);	/* latch channel 0 */
		count = inb (0x40);
		count |= inb (0x40) << 8;
		armdos_enable ();	/* a pending tick is taken here */
		t2 = timer_ticks;
	} while (t1 != t2);

	if (count == 0 || count > PIT_DIVISOR)
		count = PIT_DIVISOR;
	return (unsigned long long)t1 * PIT_DIVISOR + (PIT_DIVISOR - count);
}

static unsigned long long	pit_base;
static double	time_base;

double Sys_DoubleTime (void)
{
	if (!timer_installed)
		return time_base;
	return time_base + (double)(pit_counts() - pit_base) / PIT_CLOCK;
}

static void Sys_InitTime (void)
{
	int	j;

	pit_base = pit_counts ();
	j = COM_CheckParm ("-starttime");
	time_base = (j && j < com_argc - 1) ? atof (com_argv[j+1]) : 0.0;
}

/* wait for the next interrupt (a timer tick at the latest) */
static void Sys_Halt (void)
{
	if (timer_installed)
		armdos_halt ();
}

void Sys_Sleep (unsigned long msecs)
{
	double	end = Sys_DoubleTime () + msecs / 1000.0;

	while (Sys_DoubleTime () < end)
		Sys_Halt ();
}

/* ------------------------------------------------------------ keyboard -- */

#define KEYBUF_SIZE	256
static volatile unsigned char	keybuf[KEYBUF_SIZE];
static volatile unsigned	keybuf_head;
static unsigned	keybuf_tail;

static void int09_handler (struct armregs *f)
{
	unsigned char	sc = inb (0x60);
	unsigned	h = keybuf_head;

	if (h - keybuf_tail < KEYBUF_SIZE)
	{
		keybuf[h % KEYBUF_SIZE] = sc;
		keybuf_head = h + 1;
	}
	outb (0x20, 0x20);
	(void)f;
}

static void keyboard_startup (void)
{
	old_int09 = armdos_getvect (0x09);
	armdos_disable ();
	armdos_setvect (0x09, int09_handler);
	keyboard_installed = 1;
	armdos_enable ();
}

static void keyboard_shutdown (void)
{
	if (!keyboard_installed)
		return;
	armdos_disable ();
	armdos_setvect (0x09, old_int09);
	keyboard_installed = 0;
	armdos_enable ();
	/* we ate the break codes: no stuck Ctrl/Shift/Alt for DOS */
	ARMDOS_BDA[0x17] &= 0xF0;
	ARMDOS_BDA[0x18] &= 0xFC;
}

/* sys_dos.c's table (scan code set 1 -> Hexen II key) */
static const byte scantokey[128] =
{
/*	0        1       2       3       4       5       6       7	*/
/*	8        9       A       B       C       D       E       F	*/
	0  ,    27,     '1',    '2',    '3',    '4',    '5',    '6',
	'7',    '8',    '9',    '0',    '-',    '=', K_BACKSPACE, 9,	/* 0 */
	'q',    'w',    'e',    'r',    't',    'y',    'u',    'i',
	'o',    'p',    '[',    ']',     13,   K_CTRL,  'a',    's',	/* 1 */
	'd',    'f',    'g',    'h',    'j',    'k',    'l',    ';',
	'\'',   '`',  K_SHIFT,  '\\',   'z',    'x',    'c',    'v',	/* 2 */
	'b',    'n',    'm',    ',',    '.',    '/',  K_SHIFT,  '*',
	K_ALT,  ' ',     0 ,    K_F1,   K_F2,   K_F3,   K_F4,  K_F5,	/* 3 */
	K_F6,  K_F7,   K_F8,    K_F9,  K_F10,    0 ,     0 , K_HOME,
	K_UPARROW,K_PGUP,'-',K_LEFTARROW,'5',K_RIGHTARROW,'+',K_END,	/* 4 */
	K_DOWNARROW,K_PGDN,K_INS,K_DEL,   0 ,    0 ,     0 ,  K_F11,
	K_F12,   0 ,     0 ,     0 ,      0 ,    0 ,     0 ,     0 ,	/* 5 */
};

void Sys_SendKeyEvents (void)
{
	static int	prefix;	/* 0xE0, or E1 bytes still to skip */

	while (keybuf_tail != keybuf_head)
	{
		int	k = keybuf[keybuf_tail % KEYBUF_SIZE];
		int	key;

		keybuf_tail++;

		if (prefix > 0 && prefix < 0xE0)
		{
			/* Pause: E1 1D 45 E1 9D C5, reported on the make half */
			if (--prefix == 0)
				Key_Event (K_PAUSE, true);
			continue;
		}
		if (k == 0xE0)
		{
			prefix = 0xE0;
			continue;
		}
		if (k == 0xE1)
		{
			prefix = 5;
			continue;
		}
		if (k == 0xFA || k == 0xFE || k == 0x00 || k == 0xFF)
		{
			prefix = 0;
			continue;
		}
		if (prefix == 0xE0)
		{
			prefix = 0;
			/* the fake shifts around grey keys */
			if ((k & 0x7F) == 0x2A || (k & 0x7F) == 0x36)
				continue;
			if ((k & 0x7F) == 0x35)
			{
				Key_Event ('/', !(k & 0x80));
				continue;
			}
			if ((k & 0x7F) == 0x37)	/* Print Screen */
				continue;
		}
		key = scantokey[k & 0x7F];
		if (key)
			Key_Event (key, !(k & 0x80));
	}
}

char *Sys_GetClipboardData (void)
{
	return NULL;
}

/* --------------------------------------------------------------- files -- */

int Sys_mkdir (const char *path, qboolean crash)
{
	int rc = mkdir (path, 0777);
	if (rc != 0 && (errno == EEXIST || errno == EACCES) &&
			Sys_FileType (path) == FS_ENT_DIRECTORY)
		rc = 0;
	if (rc != 0 && crash)
		Sys_Error ("Unable to create directory %s", path);
	return rc;
}

int Sys_rmdir (const char *path)
{
	return rmdir (path);
}

int Sys_unlink (const char *path)
{
	return remove (path);
}

int Sys_rename (const char *oldp, const char *newp)
{
	return rename (oldp, newp);
}

long Sys_filesize (const char *path)
{
	struct stat	st;

	if (stat (path, &st) == -1 || S_ISDIR(st.st_mode))
		return -1;
	return (long) st.st_size;
}

int Sys_FileType (const char *path)
{
	struct stat	st;

	if (stat (path, &st) == -1)
		return FS_ENT_NONE;
	if (S_ISDIR(st.st_mode))
		return FS_ENT_DIRECTORY;
	return FS_ENT_FILE;
}

int Sys_CopyFile (const char *frompath, const char *topath)
{
	static char	buf[8192];
	int	in, out, n;
	long	remaining;

	in = open (frompath, O_RDONLY | O_BINARY);
	if (in < 0)
	{
		Con_Printf ("%s: unable to open %s\n", __thisfunc__, frompath);
		return 1;
	}
	remaining = lseek (in, 0, SEEK_END);
	lseek (in, 0, SEEK_SET);
	out = open (topath, O_CREAT | O_WRONLY | O_TRUNC | O_BINARY, 0666);
	if (out < 0)
	{
		Con_Printf ("%s: unable to create %s\n", __thisfunc__, topath);
		close (in);
		return 1;
	}
	while (remaining > 0)
	{
		n = read (in, buf, remaining < (long)sizeof(buf) ? (int)remaining : (int)sizeof(buf));
		if (n <= 0)
			break;
		if (write (out, buf, n) != n)
			break;
		remaining -= n;
	}
	close (in);
	close (out);
	return remaining != 0;
}

/* simplified findfirst/findnext: file names only (INT 21h AH=4Eh/4Fh) */
static struct find_t	finddata;
static int	findhandle = -1;
static char	findstr[MAX_OSPATH];

const char *Sys_FindFirstFile (const char *path, const char *pattern)
{
	if (findhandle == 0)
		Sys_Error ("Sys_FindFirst without FindClose");

	q_snprintf (findstr, sizeof(findstr), "%s\\%s", path, pattern);
	memset (&finddata, 0, sizeof(finddata));
	findhandle = _dos_findfirst (findstr, 0x21, &finddata) ? -1 : 0;
	if (findhandle == 0)
		return finddata.name;
	return NULL;
}

const char *Sys_FindNextFile (void)
{
	if (findhandle != 0)
		return NULL;
	if (_dos_findnext (&finddata) == 0)
		return finddata.name;
	return NULL;
}

void Sys_FindClose (void)
{
	findhandle = -1;
}

void Sys_MakeCodeWriteable (unsigned long startaddr, unsigned long length)
{
	/* it's always writeable */
}

char *Sys_DateTimeString (char *buf)
{
	static char	strbuf[24];
	time_t	t = time (NULL);
	struct tm	*tm = localtime (&t);

	if (!buf)
		buf = strbuf;
	strftime (buf, 20, "%m/%d/%Y %H:%M:%S", tm);
	return buf;
}

/* ---------------------------------------------------------- console IO -- */

const char *Sys_ConsoleInput (void)
{
	return NULL;
}

/* Console text goes to the debug port (E9h, for the test harness) always,
 * and to DOS's standard output only while the screen is in text mode
 * (before VID_Init and after VID_Shutdown) - H2DOS.EXE wrote it to stdout
 * while the game was on the screen as well, where it was overdrawn. */
void Sys_PrintTerm (const char *msgtxt)
{
	armdos_debug (msgtxt);
	if (sys_nostdout.integer || VID_ArmdosGraphics ())
		return;
	fputs (msgtxt, stdout);
}

/* ---------------------------------------------------------------- exit -- */

static int	sys_shutdown_done;

static void Sys_Shutdown (void)
{
	if (sys_shutdown_done)
		return;
	sys_shutdown_done = 1;
	keyboard_shutdown ();
	timer_shutdown ();
	VID_ArmdosTextMode ();
	fflush (stdout);
}

static void Sys_AtExit (void)
{
	Sys_Shutdown ();
}

void Sys_Quit (void)
{
	Cvar_SetROM ("sys_nostdout", "0");
	Host_Shutdown ();
	Sys_Shutdown ();
	exit (0);
}

#define ERROR_PREFIX	"\nFATAL ERROR: "
void Sys_Error (const char *error, ...)
{
	va_list		argptr;
	char		text[MAX_PRINTMSG];
	static int	in_error;

	host_parms->errstate++;

	va_start (argptr, error);
	q_vsnprintf (text, sizeof(text), error, argptr);
	va_end (argptr);

	armdos_debug (ERROR_PREFIX);
	armdos_debug (text);
	armdos_debug ("\n");
	if (con_debuglog)
	{
		LOG_Print (ERROR_PREFIX);
		LOG_Print (text);
		LOG_Print ("\n\n");
	}

	Cvar_SetROM ("sys_nostdout", "0");
	if (!in_error)
	{
		in_error = 1;
		Host_Shutdown ();
	}
	Sys_Shutdown ();
	fprintf (stderr, ERROR_PREFIX "%s\n\n", text);
	exit (1);
}

/* ---------------------------------------------------------------- main -- */

/* The XMS block holding our own image (loader/h2load.c): the stub leaves
 * its handle in the PSP's reserved bytes 58h-5Bh ("DX" + handle). */
extern struct psp	*_armdos_psp;
static uint16_t	image_handle;

static void free_image_block (void)
{
	struct armregs	r;
	void	*entry = armdos_xms_entry ();

	if (!image_handle || !entry)
		return;
	/* we are running from this block: DOS gets control back (INT 21h
	 * AH=4Ch) before anything can reuse the memory */
	memset (&r, 0, sizeof(r));
	r.r0 = 0x0D00;	r.r3 = image_handle;	/* unlock */
	_armdos_farcall (entry, &r);
	memset (&r, 0, sizeof(r));
	r.r0 = 0x0A00;	r.r3 = image_handle;	/* free */
	_armdos_farcall (entry, &r);
	image_handle = 0;
}

static void Sys_TakeImageBlock (void)
{
	uint8_t	*p = (uint8_t *) _armdos_psp;

	if (p[0x58] == 'D' && p[0x59] == 'X')
	{
		image_handle = p[0x5A] | (p[0x5B] << 8);
		p[0x58] = p[0x59] = 0;
		atexit (free_image_block);
	}
}

/* what malloc keeps for itself after the hunk: the sound ring and DMA
 * buffers, the stdio buffers, config and savegame files */
#define MALLOC_RESERVE	(64*1024)
#define MIN_MEM		(8*1024*1024)
#define MAX_MEM		(32*1024*1024)

static void Sys_GetMemory (void)
{
	int	j, size;

	j = COM_CheckParm ("-mem");
	if (j && j < com_argc - 1)
	{
		quakeparms.memsize = (int) (atof (com_argv[j+1]) * 1024 * 1024);
		quakeparms.membase = malloc (quakeparms.memsize);
		if (!quakeparms.membase)
			Sys_Error ("Not enough memory for -mem %s", com_argv[j+1]);
	}
	else
	{
		/* like dos_getmaxlockedmem: as much as there is */
		for (size = MAX_MEM; size >= MIN_MEM; size -= 64*1024)
		{
			void *volatile p = malloc (size + MALLOC_RESERVE);	// (volatile: GCC may drop an unused malloc/free pair and assume it succeeded)
			if (p)
			{
				free (p);
				quakeparms.membase = malloc (size);
				quakeparms.memsize = size;
				break;
			}
		}
		if (!quakeparms.membase)
			Sys_Error ("Not enough memory: Hexen II needs %d.%d Mb more "
				   "extended memory (XMS)",
				   (MIN_MEM + MALLOC_RESERVE) / 0x100000,
				   ((MIN_MEM + MALLOC_RESERVE) % 0x100000) * 10 / 0x100000);
	}

	printf ("Allocated %d.%d Mb data\n", quakeparms.memsize / 0x100000,
		(quakeparms.memsize % 0x100000) * 10 / 0x100000);

	j = COM_CheckParm ("-heapsize");
	if (j && j < com_argc - 1)
	{
		int tsize = atoi (com_argv[j+1]) * 1024;
		if (tsize < quakeparms.memsize)
			quakeparms.memsize = tsize;
	}
}

/* The directory with DATA1: the current one, as H2DOS.EXE had it, or - when
 * H2.EXE was started through PATH or as C:\GAMES\HEXEN2\H2 - the one the
 * program is in. */
static char	basedir[MAX_OSPATH];

static void Sys_GetBasedir (const char *argv0)
{
	char	test[MAX_OSPATH + 8];
	char	*slash;
	size_t	len;

	if (!getcwd (basedir, sizeof(basedir) - 1))
		strcpy (basedir, ".");
	len = strlen (basedir);
	if (len > 3 && (basedir[len-1] == '\\' || basedir[len-1] == '/'))
		basedir[--len] = 0;
	else if (len == 3)	/* C:\ */
		basedir[--len] = 0;

	q_snprintf (test, sizeof(test), "%s\\DATA1", basedir);
	if (Sys_FileType (test) == FS_ENT_DIRECTORY)
		return;
	if (argv0 && argv0[0])
	{
		q_strlcpy (test, argv0, sizeof(test));
		slash = strrchr (test, '\\');
		if (slash && slash > test)
		{
			*slash = 0;
			q_strlcpy (basedir, test, sizeof(basedir));
		}
	}
}

/* Conventional memory. The game image, its bss and the hunk take all of
 * extended memory, while the conventional memory under 640 KB - where
 * H2DOS.EXE's real-mode stub and DOS lived - is free apart from the 15 KB
 * stub. Two things go there, in DOS memory blocks (INT 21h AH=48h):
 *   - the main zone (384 KB, -zone N), which the engine would otherwise
 *     take from the hunk (zone.c, marked ARMDOS), and
 *   - the C stack: what is left, if that is at least 192 KB (the engine
 *     goes ~150 KB deep), else 256 KB of the XMS heap. The small stack
 *     app.mk gives (--stack) is only used until main() switches.
 * The stack is painted; "sys_stack" tells how deep the engine has gone. */
#define STACK_MAX	(512*1024)
#define STACK_MIN	(192*1024)
#define STACK_XMS	(256*1024)
#define STACK_PAINT	0x5AC4E5A5u
#define ZONE_DEFAULT	0x60000		/* zone.c's ZONE_DEFSIZE */

static unsigned	*stack_base, *stack_top;
static void	*zone_block;
static int	zone_size;

/* a DOS block of up to max bytes, at least min; NULL if there is none */
static void *Sys_DosAlloc (unsigned max, unsigned min, unsigned *got)
{
	struct armregs	r;
	unsigned	paras = (max + 15) >> 4;

	memset (&r, 0, sizeof(r));
	r.r0 = 0x4800;
	r.r1 = paras > 0xFFFF ? 0xFFFF : paras;
	if (_armdos_int21 (&r))
	{
		paras = r.r1 & 0xFFFF;		/* the largest free block */
		if ((paras << 4) < min)
			return NULL;
		memset (&r, 0, sizeof(r));
		r.r0 = 0x4800;
		r.r1 = paras;
		if (_armdos_int21 (&r))
			return NULL;
	}
	*got = paras << 4;
	return ARMDOS_SEG2PTR (r.r0 & 0xFFFF);
}

/* zone.c: the main zone's memory, if it could go to conventional memory */
void *Sys_ArmdosZoneBlock (int size)
{
	if (zone_block && size <= zone_size)
		return zone_block;
	return NULL;
}

static int Sys_AllocLowMem (void)
{
	unsigned	size = 0;
	unsigned	*p;
	int		j;

	zone_size = ZONE_DEFAULT;
	j = COM_CheckParm ("-zone");
	if (j && j < com_argc - 1)
		zone_size = atoi (com_argv[j+1]) * 1024;
	zone_block = Sys_DosAlloc (zone_size, zone_size, &size);

	stack_base = (unsigned *) Sys_DosAlloc (STACK_MAX, STACK_MIN, &size);
	if (!stack_base)
	{
		size = STACK_XMS;
		stack_base = (unsigned *) malloc (size);
		if (!stack_base)
			return 0;
	}
	stack_top = (unsigned *) (((unsigned) stack_base + size) & ~15u);
	for (p = stack_base; p < stack_top; p++)
		*p = STACK_PAINT;
	return 1;
}

void Sys_Stack_f (void)
{
	unsigned	*q = stack_base;

	while (q < stack_top && *q == STACK_PAINT)
		q++;
	Con_Printf ("zone: %s, stack: %s\n", zone_block ? "conventional" : "hunk",
		    (unsigned) stack_base < 0xA0000 ? "conventional" : "extended");
	Con_Printf ("stack: %u KB of %u KB used\n",
		    (unsigned)((byte *)stack_top - (byte *)q) / 1024,
		    (unsigned)((byte *)stack_top - (byte *)stack_base) / 1024);
}

static void PrintVersion (void)
{
	printf ("Hexen II for ARM-DOS - Hammer of Thyrion %s (%s)\n",
		HOT_VERSION_STR, HOT_VERSION_REL_DATE);
	printf ("running on %s engine %4.2f (ARM-DOS)\n", ENGINE_NAME, ENGINE_VERSION);
}

static void Sys_Main (void)
{
	double		time, oldtime, newtime;

	Sys_GetMemory ();

	atexit (Sys_AtExit);	/* in case we crash */

	isDedicated = (COM_CheckParm ("-dedicated") != 0);

	timer_startup ();
	Sys_InitTime ();
	if (!isDedicated)
		keyboard_startup ();

	Host_Init ();

	oldtime = Sys_DoubleTime ();
	for (;;)
	{
		newtime = Sys_DoubleTime ();
		time = newtime - oldtime;

		if (isDedicated && time < sys_ticrate.value)
		{
			Sys_Halt ();
			continue;
		}
		/* Host_Frame would do nothing: we are ahead of the 72 fps clock */
		if (!cls.timedemo && time < 1.0/72.0)
		{
			Sys_Halt ();
			continue;
		}

		Host_Frame (time);
		oldtime = newtime;
	}
}

static int Sys_HaveFPU (void)
{
	unsigned	fpexc;

	/* FPEXC is readable in SYS mode (DOS programs run privileged);
	 * the ROM BIOS sets EN when the VFP9-S is present. */
	__asm__ volatile ("mrc p10, 7, %0, c8, c0, 0" : "=r"(fpexc));	/* fmrx fpexc */
	return (fpexc & 0x40000000u) != 0;
}

int main (int argc, char **argv)
{
	Sys_TakeImageBlock ();
	PrintVersion ();

	if (!armdos_vga_present ())		/* the Hercules card option */
	{
		printf ("\nError: Hexen II requires a VGA\n");
		exit (1);
	}

	if (!Sys_HaveFPU ())
	{
		printf ("\nError: Hexen II requires a floating-point processor\n");
		exit (1);
	}

	memset (&quakeparms, 0, sizeof(quakeparms));
	Sys_GetBasedir (argv[0]);
	quakeparms.basedir = basedir;
	quakeparms.userdir = basedir;
	quakeparms.argc = argc;
	quakeparms.argv = argv;
	quakeparms.errstate = 0;
	host_parms = &quakeparms;

	LOG_Init (&quakeparms);
	COM_ValidateByteorder ();

	if (!Sys_AllocLowMem ())
	{
		printf ("Error: not enough memory for the stack\n");
		exit (1);
	}
	{
		void *top = stack_top;
		void (*fn)(void) = Sys_Main;
		__asm__ volatile (
			"mov  r4, sp\n\t"
			"mov  sp, %1\n\t"
			"blx  %0\n\t"
			"mov  sp, r4\n\t"
			: : "r"(fn), "r"(top)
			: "r0", "r1", "r2", "r3", "r4", "r12", "lr", "memory", "cc");
	}
	return 0;
}
