/*
Copyright (C) 1996-1997 Id Software, Inc.
Copyright (C) 2026 the ARM-DOS project.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.
*/
// sys_armdos.c -- the ARM-DOS system driver (after id's sys_dos.c)
//
// QUAKE.EXE on a 1996 PC was a DJGPP program under CWSDPMI; this is the same
// thing on the ARM-PC, talking to DOS and the hardware directly:
//
//  * files:    newlib open/read/lseek -> INT 21h, binary mode.
//  * memory:   the hunk (-mem N, default: as much as there is, 8-16 MB) comes
//              from the C runtime's heap, which lives in XMS (HIMEM.SYS). The
//              C stack is moved there too: Quake keeps ~200 KB of edges,
//              surfaces and the warp buffer on the stack.
//  * time:     PIT channel 0 in mode 2 at ~1 kHz with an INT 08h handler; the
//              fine time is ticks * divisor + the latched count (like
//              sys_dos.c's Sys_FloatTime, which read the counter of the
//              18.2 Hz tick). The BIOS tick is chained every 65536 PIT counts.
//  * keyboard: an INT 09h handler puts scan codes (port 60h) in a ring
//              buffer; Sys_SendKeyEvents turns them into Key_Event()s.
//  * idle:     WFI while Quake is ahead of its 72 fps frame clock.
//  * exit:     text mode and the END1.BIN "sell screen" at B800:0000.
//

#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <armdos.h>

#include "quakedef.h"
#include "sys_armdos.h"

#define outb(p, v) armdos_outb((p), (uint8_t)(v))
#define inb(p)     armdos_inb(p)

// Without HIMEM.SYS, let the heap use raw extended memory (INT 15h AH=88h).
unsigned _armdos_raw_extmem = 1;

static quakeparms_t quakeparms;
qboolean            isDedicated;

// ---------------------------------------------------------------- timer ---

#define PIT_CLOCK   1193182.0
#define PIT_DIVISOR 1193u                 // 1000.15 Hz

static armdos_vect_t old_int08, old_int09;
static int timer_installed, keyboard_installed;
static volatile unsigned timer_ticks;
static unsigned bios_acc;

static void int08_handler(struct armregs *f)
{
	timer_ticks++;
	bios_acc += PIT_DIVISOR;
	if (bios_acc >= 65536u)
	{
		bios_acc -= 65536u;
		if (old_int08)
		{
			old_int08(f);                 // the BIOS sends the EOI
			return;
		}
	}
	outb(0x20, 0x20);
}

static void timer_startup(void)
{
	old_int08 = armdos_getvect(0x08);
	armdos_disable();
	armdos_setvect(0x08, int08_handler);
	outb(0x43, 0x34);                     // channel 0, lo/hi, mode 2
	outb(0x40, PIT_DIVISOR & 0xFF);
	outb(0x40, PIT_DIVISOR >> 8);
	timer_installed = 1;
	armdos_enable();
}

static void timer_shutdown(void)
{
	if (!timer_installed)
		return;
	armdos_disable();
	outb(0x43, 0x36);                     // mode 3, divisor 65536: 18.2 Hz
	outb(0x40, 0);
	outb(0x40, 0);
	armdos_setvect(0x08, old_int08);
	timer_installed = 0;
	armdos_enable();
}

// PIT counts since the timer was started.
static unsigned long long pit_counts(void)
{
	unsigned t1, t2, count;

	do
	{
		t1 = timer_ticks;
		armdos_disable();
		outb(0x43, 0x00);                 // latch channel 0
		count = inb(0x40);
		count |= inb(0x40) << 8;
		armdos_enable();                  // a pending tick is taken here
		t2 = timer_ticks;
	} while (t1 != t2);

	if (count == 0 || count > PIT_DIVISOR)
		count = PIT_DIVISOR;
	return (unsigned long long)t1 * PIT_DIVISOR + (PIT_DIVISOR - count);
}

static double curtime, oldtime_base;
static unsigned long long pit_base;

double Sys_FloatTime (void)
{
	if (!timer_installed)
		return curtime;
	return oldtime_base + (double)(pit_counts() - pit_base) / PIT_CLOCK;
}

static void Sys_InitFloatTime (void)
{
	int j;

	pit_base = pit_counts();
	j = COM_CheckParm("-starttime");
	oldtime_base = j ? (double)Q_atof(com_argv[j+1]) : 0.0;
}

// Wait for the next interrupt (a timer tick at the latest).
void Sys_Sleep (void)
{
	if (timer_installed)
		armdos_halt();
}

// ------------------------------------------------------------ keyboard ---

#define KEYBUF_SIZE 256
static volatile unsigned char keybuf[KEYBUF_SIZE];
static volatile unsigned keybuf_head;
static unsigned keybuf_tail;

static void int09_handler(struct armregs *f)
{
	unsigned char sc = inb(0x60);
	unsigned h = keybuf_head;

	if (h - keybuf_tail < KEYBUF_SIZE)
	{
		keybuf[h % KEYBUF_SIZE] = sc;
		keybuf_head = h + 1;
	}
	outb(0x20, 0x20);
	(void)f;
}

static void keyboard_startup(void)
{
	old_int09 = armdos_getvect(0x09);
	armdos_disable();
	armdos_setvect(0x09, int09_handler);
	keyboard_installed = 1;
	armdos_enable();
}

static void keyboard_shutdown(void)
{
	if (!keyboard_installed)
		return;
	armdos_disable();
	armdos_setvect(0x09, old_int09);
	keyboard_installed = 0;
	armdos_enable();
	// we ate the break codes: no stuck Ctrl/Shift/Alt for DOS
	ARMDOS_BDA[0x17] &= 0xF0;
	ARMDOS_BDA[0x18] &= 0xFC;
}

// id's table from sys_dos.c (scan code set 1 -> Quake key)
static const byte scantokey[128] =
{
//  0           1       2       3       4       5       6       7
//  8           9       A       B       C       D       E       F
	0  ,    27,     '1',    '2',    '3',    '4',    '5',    '6',
	'7',    '8',    '9',    '0',    '-',    '=',    K_BACKSPACE, 9, // 0
	'q',    'w',    'e',    'r',    't',    'y',    'u',    'i',
	'o',    'p',    '[',    ']',    13 ,    K_CTRL,'a',  's',      // 1
	'd',    'f',    'g',    'h',    'j',    'k',    'l',    ';',
	'\'' ,    '`',    K_SHIFT,'\\',  'z',    'x',    'c',    'v',      // 2
	'b',    'n',    'm',    ',',    '.',    '/',    K_SHIFT,'*',
	K_ALT,' ',   0  ,    K_F1, K_F2, K_F3, K_F4, K_F5,   // 3
	K_F6, K_F7, K_F8, K_F9, K_F10,0  ,    0  , K_HOME,
	K_UPARROW,K_PGUP,'-',K_LEFTARROW,'5',K_RIGHTARROW,'+',K_END, //4
	K_DOWNARROW,K_PGDN,K_INS,K_DEL,0,0,             0,              K_F11,
	K_F12,0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0,        // 5
};

void Sys_SendKeyEvents (void)
{
	static int prefix;      // 0xE0, or E1 bytes still to skip

	while (keybuf_tail != keybuf_head)
	{
		int k = keybuf[keybuf_tail % KEYBUF_SIZE];
		int key;

		keybuf_tail++;

		if (prefix > 0 && prefix < 0xE0)
		{
			// Pause: E1 1D 45 E1 9D C5, reported on the make half
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
			// the fake shifts around grey keys
			if ((k & 0x7F) == 0x2A || (k & 0x7F) == 0x36)
				continue;
			if ((k & 0x7F) == 0x35)
			{
				Key_Event ('/', !(k & 0x80));
				continue;
			}
			if ((k & 0x7F) == 0x37)     // Print Screen
				continue;
		}
		key = scantokey[k & 0x7F];
		if (key)
			Key_Event (key, !(k & 0x80));
	}
}

// --------------------------------------------------------------- files ---

int Sys_FileTime (char *path)
{
	struct stat buf;

	if (stat (path, &buf) == -1)
		return -1;
	return buf.st_mtime;
}

void Sys_mkdir (char *path)
{
	mkdir (path, 0777);
}

int Sys_FileOpenRead (char *path, int *handle)
{
	int h;
	struct stat fileinfo;

	h = open (path, O_RDONLY | O_BINARY, 0666);
	*handle = h;
	if (h == -1)
		return -1;
	if (fstat (h, &fileinfo) == -1)
		Sys_Error ("Error fstating %s", path);
	return fileinfo.st_size;
}

int Sys_FileOpenWrite (char *path)
{
	int handle;

	handle = open (path, O_RDWR | O_BINARY | O_CREAT | O_TRUNC, 0666);
	if (handle == -1)
		Sys_Error ("Error opening %s: %s", path, strerror(errno));
	return handle;
}

void Sys_FileClose (int handle)
{
	close (handle);
}

void Sys_FileSeek (int handle, int position)
{
	lseek (handle, position, SEEK_SET);
}

// INT 21h moves at most 64 KB - 1 per call (CX): loop, as DJGPP's read did.
int Sys_FileRead (int handle, void *dest, int count)
{
	int done = 0, n;

	while (done < count)
	{
		n = read (handle, (byte *)dest + done, count - done);
		if (n <= 0)
			return done ? done : n;
		done += n;
	}
	return done;
}

int Sys_FileWrite (int handle, void *data, int count)
{
	int done = 0, n;

	while (done < count)
	{
		n = write (handle, (byte *)data + done, count - done);
		if (n <= 0)
			return done ? done : n;
		done += n;
	}
	return done;
}

void Sys_MakeCodeWriteable (unsigned long startaddr, unsigned long length)
{
	// it's always writeable
}

void Sys_DebugLog (char *file, char *fmt, ...)
{
}

// ---------------------------------------------------------- console IO ---

char *Sys_ConsoleInput (void)
{
	return NULL;
}

void Sys_Printf (char *fmt, ...)
{
	va_list argptr;
	char    text[1024];

	va_start (argptr, fmt);
	vsnprintf (text, sizeof(text), fmt, argptr);
	va_end (argptr);

	// the console text also goes to the debug port (E9h) for the test harness
	armdos_debug (text);
	if (cls.state == ca_dedicated)
		fputs (text, stderr);
}

void Sys_HighFPPrecision (void) {}
void Sys_LowFPPrecision (void) {}
void Sys_SetFPCW (void) {}

// ---------------------------------------------------------------- exit ---

static int sys_shutdown_done;

static void Sys_Shutdown (void)
{
	if (sys_shutdown_done)
		return;
	sys_shutdown_done = 1;
	keyboard_shutdown ();
	timer_shutdown ();
	VID_ArmdosTextMode ();
}

static void Sys_AtExit (void)
{
	Sys_Shutdown ();
}

void Sys_Quit (void)
{
	static byte screen[80*25*2];
	byte   *d;
	char    ver[8];
	int     i;
	struct armregs r;

	// load the sell screen before shutting everything down
	if (registered.value)
		d = COM_LoadHunkFile ("end2.bin");
	else
		d = COM_LoadHunkFile ("end1.bin");
	if (d)
		memcpy (screen, d, sizeof(screen));

	// write the version number directly to the end screen
	sprintf (ver, " v%4.2f", VERSION);
	for (i = 0; i < 6; i++)
		screen[0*80*2 + 72*2 + i*2] = ver[i];

	Host_Shutdown ();
	Sys_Shutdown ();

	// the text mode sell screen
	if (d)
	{
		memcpy ((void *)0xB8000, screen, 80*25*2);
		memset (&r, 0, sizeof(r));
		r.r0 = 0x0200;                // set cursor position
		r.r1 = 0;                     // page 0
		r.r3 = 22 << 8;               // DH=22, DL=0
		_armdos_int10 (&r);
	}
	else
		printf ("couldn't load endscreen.\n");

	exit (0);
}

void Sys_Error (char *error, ...)
{
	va_list     argptr;
	char        string[1024];
	static int  in_error;

	va_start (argptr, error);
	vsnprintf (string, sizeof(string), error, argptr);
	va_end (argptr);

	armdos_debug ("Error: ");
	armdos_debug (string);
	armdos_debug ("\n");
	if (!in_error)
	{
		in_error = 1;
		Host_Shutdown ();
	}
	Sys_Shutdown ();
	fprintf (stderr, "Error: %s\n", string);
	exit (1);
}

// ---------------------------------------------------------------- main ---

// The big static arrays (sys_armdos.h), zeroed like bss.
// (these four are private to model.c, snd_mix.c and r_sky.c)
extern model_t (*mod_known_p)[256];         // MAX_MOD_KNOWN
extern int     (*snd_scaletable_p)[32][256];
extern byte    (*newsky_p)[128*256];
extern byte    (*bottomsky_p)[128*131];
extern byte    (*bottommask_p)[128*131];

static int Sys_ArmdosAllocBig (void)
{
#define ALLOC_BIG(n) \
	if (!(n##_p = calloc (1, sizeof(*n##_p)))) return 0;
	ARMDOS_BIG(ALLOC_BIG)
#undef ALLOC_BIG
	return 1;
}

// The C stack in extended memory (see the top of the file).
#define STACK_SIZE (1024*1024)

static void Sys_GetMemory (void)
{
	int j, size;

	j = COM_CheckParm ("-mem");
	if (j)
	{
		quakeparms.memsize = (int)(Q_atof (com_argv[j+1]) * 1024 * 1024);
		quakeparms.membase = malloc (quakeparms.memsize);
		if (!quakeparms.membase)
			Sys_Error ("Not enough memory for -mem %s", com_argv[j+1]);
	}
	else
	{
		// like dos_getmaxlockedmem: as much as there is (up to 16 MB),
		// leaving 512 KB for malloc (sound, the video buffers)
		for (size = 16*1024*1024; size >= 4*1024*1024; size -= 256*1024)
		{
			void *volatile p = malloc (size + 512*1024);	// (volatile: GCC may drop an unused malloc/free pair and assume it succeeded)
			if (p)
			{
				free (p);
				quakeparms.membase = malloc (size);
				quakeparms.memsize = size;
				break;
			}
		}
		if (!quakeparms.membase)
			Sys_Error ("Not enough memory: Quake needs at least 4.5 Mb "
			           "of extended memory (XMS)");
	}

	printf ("Allocated %d.%d Mb data\n", quakeparms.memsize / 0x100000,
	        (quakeparms.memsize % 0x100000) * 10 / 0x100000);

	if (COM_CheckParm ("-heapsize"))
	{
		int tsize = Q_atoi (com_argv[COM_CheckParm("-heapsize") + 1]) * 1024;
		if (tsize < quakeparms.memsize)
			quakeparms.memsize = tsize;
	}
}

static void Sys_Main (void)
{
	double time, oldtime, newtime;
	static char cwd[256];

	Sys_GetMemory ();

	atexit (Sys_AtExit);    // in case we crash

	if (!getcwd (cwd, sizeof(cwd)))
		strcpy (cwd, ".");
	if (cwd[strlen(cwd)-1] == '\\' || cwd[strlen(cwd)-1] == '/')
		cwd[strlen(cwd)-1] = 0;
	// QUAKE.EXE used the current directory. If there is no ID1 there (QUAKE
	// started through PATH or as C:\GAMES\QUAKE\QUAKE), use the directory
	// QUAKE.EXE is in, like the ARM-DOS DOOM.
	{
		static char exedir[256];
		char test[300];
		struct stat st;
		char *slash;

		snprintf (test, sizeof(test), "%s\\ID1", cwd);
		if (stat (test, &st) == -1 && com_argv[0] && com_argv[0][0])
		{
			Q_strncpy (exedir, com_argv[0], sizeof(exedir) - 1);
			slash = strrchr (exedir, '\\');
			if (slash && slash > exedir)
			{
				*slash = 0;
				quakeparms.basedir = exedir;
			}
		}
		if (!quakeparms.basedir)
			quakeparms.basedir = cwd;
	}

	isDedicated = (COM_CheckParm ("-dedicated") != 0);

	timer_startup ();
	Sys_InitFloatTime ();
	if (!isDedicated)
		keyboard_startup ();

	Host_Init (&quakeparms);

	oldtime = Sys_FloatTime ();
	for (;;)
	{
		newtime = Sys_FloatTime ();
		time = newtime - oldtime;

		if (cls.state == ca_dedicated && time < sys_ticrate.value)
		{
			Sys_Sleep ();
			continue;
		}
		// Host_Frame would do nothing: we are ahead of the 72 fps clock
		if (!cls.timedemo && time < 1.0/72.0)
		{
			Sys_Sleep ();
			continue;
		}

		Host_Frame (time);
		oldtime = newtime;
	}
}

static int Sys_HaveFPU (void)
{
	unsigned fpexc;

	// FPEXC is readable in SYS mode (DOS programs run privileged); the
	// ROM BIOS sets EN when the VFP9-S is present.
	__asm__ volatile ("mrc p10, 7, %0, c8, c0, 0" : "=r"(fpexc));   // fmrx fpexc
	return (fpexc & 0x40000000u) != 0;
}

int main (int c, char **v)
{
	void *stack;

	printf ("Quake v%4.2f\n", VERSION);

	if (!armdos_vga_present ())		/* the Hercules card option */
	{
		printf ("\nError: Quake requires a VGA\n");
		return 1;
	}

	if (!Sys_HaveFPU ())
	{
		printf ("\nError: Quake requires a floating-point processor\n");
		exit (0);
	}

	COM_InitArgv (c, v);
	quakeparms.argc = com_argc;
	quakeparms.argv = com_argv;

	// Move the stack into extended memory, as a DOS extender would have it.
	stack = malloc (STACK_SIZE);
	if (!stack || !Sys_ArmdosAllocBig ())
	{
		printf ("Error: not enough extended memory (XMS)\n");
		exit (1);
	}
	{
		void *top = (byte *)stack + STACK_SIZE - 16;
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
