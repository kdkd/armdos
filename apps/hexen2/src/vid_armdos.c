/* vid_armdos.c -- VGA mode 13h video driver for ARM-DOS (after uHexen2's
 * vid_dos.c / vid_vga.c and the ARM-DOS Quake's vid_armdos.c)
 *
 * Copyright (C) 1996-1997  Id Software, Inc.
 * Copyright (C) 1997-1998  Raven Software Corp.
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
 * H2DOS.EXE's mode 0 is "320x200" = BIOS mode 13h, drawn into a buffer in
 * system memory and copied to A000:0000 in VID_Update; the palette goes to
 * the DAC through ports 3C8h/3C9h. The ARM-PC has no VESA BIOS and its VGA
 * no Mode X, so this is the only mode (the Options menu leaves "Video
 * Modes" out, as uHexen2 does when a driver has no mode menu).
 */

#include <string.h>
#include <stdlib.h>

#include <armdos.h>

#include "quakedef.h"
#include "d_local.h"
#include "sys_armdos.h"

#define outb(p, v)	armdos_outb((p), (uint8_t)(v))

#define BASEWIDTH	320
#define BASEHEIGHT	200
#define VGA_MEM		((byte *)0xA0000)

viddef_t	vid;		/* global video state */
modestate_t	modestate = MS_UNINIT;
qboolean	in_mode_set;

unsigned short	d_8to16table[256];	/* not used in 8 bpp mode */
unsigned int	d_8to24table[256];	/* not used in 8 bpp mode */

byte		globalcolormap[VID_GRADES*256], lastglobalcolor = 0;
byte		*lastsourcecolormap = NULL;

static cvar_t	vid_mode = {"vid_mode", "0", CVAR_NONE};
cvar_t		_enable_mouse = {"_enable_mouse", "1", CVAR_ARCHIVE};

static byte	*vid_buffer;
static int	video_set;
static byte	backingbuf[48*24];

static void bios_set_mode (int mode)
{
	struct armregs	r;

	memset (&r, 0, sizeof(r));
	r.r0 = mode & 0xFF;		/* AH=00h set video mode */
	_armdos_int10 (&r);
}

int VID_ArmdosGraphics (void)
{
	return video_set;
}

void VID_SetPalette (const unsigned char *palette)
{
	int	i;

	if (!video_set)
		return;
	outb (0x3C8, 0);
	for (i = 0; i < 768; i++)
		outb (0x3C9, palette[i] >> 2);
}

void VID_ShiftPalette (const unsigned char *palette)
{
	VID_SetPalette (palette);
}

static void VID_DescribeModes_f (void)
{
	Con_Printf ("0: 320x200\n");
}

static void VID_NumModes_f (void)
{
	Con_Printf ("1 video mode is available\n");
}

static void VID_DescribeCurrentMode_f (void)
{
	Con_Printf ("320x200 (VGA mode 13h)\n");
}

void VID_Init (const unsigned char *palette)
{
	int	cachesize, zsize;
	byte	*buf;

	Cvar_RegisterVariable (&vid_mode);
	Cvar_RegisterVariable (&_enable_mouse);
	Cmd_AddCommand ("vid_describemodes", VID_DescribeModes_f);
	Cmd_AddCommand ("vid_nummodes", VID_NumModes_f);
	Cmd_AddCommand ("vid_describecurrentmode", VID_DescribeCurrentMode_f);
	Cmd_AddCommand ("sys_stack", Sys_Stack_f);

	vid.maxwarpwidth = WARP_WIDTH;
	vid.maxwarpheight = WARP_HEIGHT;
	vid.width = vid.conwidth = BASEWIDTH;
	vid.height = vid.conheight = BASEHEIGHT;
	vid.aspect = ((float)vid.height / (float)vid.width) * (320.0 / 240.0);
	vid.numpages = 1;
	vid.colormap = host_colormap;
	vid.fullbright = 256 - LittleLong (*((int *)vid.colormap + 2048));
	vid.rowbytes = vid.conrowbytes = BASEWIDTH;
	vid.direct = VGA_MEM;

	/* vid_vga.c's layout: z-buffer, surface cache, then the frame buffer
	 * (with an extra line), all from the high hunk */
	zsize = BASEWIDTH * BASEHEIGHT * sizeof(*d_pzbuffer);
	cachesize = D_SurfaceCacheForRes (BASEWIDTH, BASEHEIGHT);
	buf = (byte *) Hunk_HighAllocName (zsize + cachesize +
					   BASEWIDTH * (BASEHEIGHT + 1), "video");
	d_pzbuffer = (short *) buf;
	vid_buffer = buf + zsize + cachesize;
	vid.buffer = vid.conbuffer = vid_buffer;
	D_InitCaches (buf + zsize, cachesize);

	bios_set_mode (0x13);
	video_set = 1;
	modestate = MS_FULLSCREEN;
	VID_SetPalette (palette);
	vid.recalc_refdef = 1;

	vid_menudrawfn = NULL;	/* no "Video Modes" entry in the options */
	vid_menukeyfn = NULL;
}

/* back to 80x25 text (Sys_Quit, Sys_Error) */
void VID_ArmdosTextMode (void)
{
	if (!video_set)
		return;
	video_set = 0;
	modestate = MS_UNINIT;
	bios_set_mode (0x03);
}

void VID_Shutdown (void)
{
	VID_ArmdosTextMode ();
}

void VID_Update (vrect_t *rects)
{
	if (!video_set)
		return;
	for ( ; rects; rects = rects->pnext)
	{
		int x = rects->x, y = rects->y, w = rects->width, h = rects->height;

		if (x < 0) { w += x; x = 0; }
		if (y < 0) { h += y; y = 0; }
		if (x + w > BASEWIDTH) w = BASEWIDTH - x;
		if (y + h > BASEHEIGHT) h = BASEHEIGHT - y;
		if (w <= 0 || h <= 0)
			continue;
		if (x == 0 && w == BASEWIDTH)
			memcpy (VGA_MEM + y * BASEWIDTH, vid.buffer + y * vid.rowbytes, w * h);
		else
		{
			byte *src = vid.buffer + y * vid.rowbytes + x;
			byte *dst = VGA_MEM + y * BASEWIDTH + x;
			for ( ; h > 0; h--, src += vid.rowbytes, dst += BASEWIDTH)
				memcpy (dst, src, w);
		}
	}
}

void VID_LockBuffer (void)
{
}

void VID_UnlockBuffer (void)
{
}

void VID_HandlePause (qboolean paused)
{
	if (paused)	IN_DeactivateMouse ();
	else		IN_ActivateMouse ();
}

void VID_ToggleFullscreen (void)
{
}

void D_ShowLoadingSize (void)
{
}

/* The "disc" icon while loading: drawn straight into video memory, what was
 * under it saved and put back (vid_vga.c's VGA_BeginDirectRect). */
void D_BeginDirectRect (int x, int y, byte *pbitmap, int width, int height)
{
	int	i;

	if (!video_set || width > 48 || height > 24 || width < 1 || height < 1)
		return;
	if (x < 0)
		x = BASEWIDTH + x - 1;
	for (i = 0; i < height; i++)
	{
		memcpy (backingbuf + i * 48, VGA_MEM + (y + i) * BASEWIDTH + x, width);
		memcpy (VGA_MEM + (y + i) * BASEWIDTH + x, pbitmap + i * width, width);
	}
}

void D_EndDirectRect (int x, int y, int width, int height)
{
	int	i;

	if (!video_set || width > 48 || height > 24 || width < 1 || height < 1)
		return;
	if (x < 0)
		x = BASEWIDTH + x - 1;
	for (i = 0; i < height; i++)
		memcpy (VGA_MEM + (y + i) * BASEWIDTH + x, backingbuf + i * 48, width);
}
