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
// vid_armdos.c -- VGA mode 13h video driver for ARM-DOS (after vid_dos.c
// and vid_vga.c)
//
// QUAKE.EXE's mode 0 was "320x200" = BIOS mode 13h, drawn into a system
// memory buffer and copied to A000:0000 in VID_Update; the palette goes to
// the DAC through ports 3C8h/3C9h. The ARM-PC has no VESA BIOS, so that is
// the only mode (vid_describemodes lists it).
//

#include <string.h>
#include <stdlib.h>

#include <armdos.h>

#include "quakedef.h"
#include "d_local.h"
#include "sys_armdos.h"

#define outb(p, v) armdos_outb((p), (uint8_t)(v))

#define BASEWIDTH   320
#define BASEHEIGHT  200
#define VGA_MEM     ((byte *)0xA0000)

viddef_t    vid;                // global video state

unsigned short  d_8to16table[256];
unsigned        d_8to24table[256];

cvar_t      vid_mode = {"vid_mode","0", false};
cvar_t      _vid_default_mode = {"_vid_default_mode","0", true};
cvar_t      vid_wait = {"vid_wait","0"};
cvar_t      _vid_wait_override = {"_vid_wait_override", "0", true};

static byte *vid_buffer;
static short *zbuffer;
static byte *surfcache;
static int  video_set;
static byte backingbuf[24*24];

void M_Print (int cx, int cy, char *str);
void M_DrawPic (int x, int y, qpic_t *pic);
void M_Menu_Options_f (void);
static void VID_MenuInit (void);

static void bios_set_mode (int mode)
{
	struct armregs r;

	memset (&r, 0, sizeof(r));
	r.r0 = mode & 0xFF;          // AH=00h set video mode
	_armdos_int10 (&r);
}

void VID_SetPalette (unsigned char *palette)
{
	int i;

	if (!video_set)
		return;
	outb (0x3C8, 0);
	for (i = 0; i < 768; i++)
		outb (0x3C9, palette[i] >> 2);
}

void VID_ShiftPalette (unsigned char *palette)
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
	Con_Printf ("320x200\n");
}

void VID_Init (unsigned char *palette)
{
	int cachesize;

	Cvar_RegisterVariable (&vid_mode);
	Cvar_RegisterVariable (&vid_wait);
	Cvar_RegisterVariable (&_vid_wait_override);
	Cvar_RegisterVariable (&_vid_default_mode);
	Cmd_AddCommand ("vid_describemodes", VID_DescribeModes_f);
	Cmd_AddCommand ("vid_nummodes", VID_NumModes_f);
	Cmd_AddCommand ("vid_describecurrentmode", VID_DescribeCurrentMode_f);

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

	cachesize = D_SurfaceCacheForRes (BASEWIDTH, BASEHEIGHT);
	vid_buffer = Hunk_AllocName (BASEWIDTH * BASEHEIGHT, "video");
	zbuffer = Hunk_AllocName (BASEWIDTH * BASEHEIGHT * sizeof(short), "zbuffer");
	surfcache = Hunk_AllocName (cachesize, "surfcache");
	vid.buffer = vid.conbuffer = vid_buffer;
	d_pzbuffer = zbuffer;
	D_InitCaches (surfcache, cachesize);

	bios_set_mode (0x13);
	video_set = 1;
	VID_SetPalette (palette);
	vid.recalc_refdef = 1;
	VID_MenuInit ();
}

// Back to 80x25 text (Sys_Quit, Sys_Error).
void VID_ArmdosTextMode (void)
{
	if (!video_set)
		return;
	video_set = 0;
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
			memcpy (VGA_MEM + y * BASEWIDTH, vid.buffer + y * vid.rowbytes,
			        w * h);
		else
		{
			byte *src = vid.buffer + y * vid.rowbytes + x;
			byte *dst = VGA_MEM + y * BASEWIDTH + x;
			for ( ; h > 0; h--, src += vid.rowbytes, dst += BASEWIDTH)
				memcpy (dst, src, w);
		}
	}
}

int VID_SetMode (int modenum, unsigned char *palette)
{
	if (modenum != 0)
		Con_Printf ("No such video mode: %d\n", modenum);
	Cvar_SetValue ("vid_mode", 0);
	return modenum == 0;
}

void VID_HandlePause (qboolean pause)
{
}

// The "disc" icon while loading: drawn straight into video memory, what
// was under it saved and put back (vid_vga.c's VGA_BeginDirectRect).
void D_BeginDirectRect (int x, int y, byte *pbitmap, int width, int height)
{
	int i;

	if (!video_set || width > 24 || height > 24 || width < 1 || height < 1)
		return;
	for (i = 0; i < height; i++)
	{
		memcpy (backingbuf + i * 24, VGA_MEM + (y + i) * BASEWIDTH + x, width);
		memcpy (VGA_MEM + (y + i) * BASEWIDTH + x, pbitmap + i * width, width);
	}
}

void D_EndDirectRect (int x, int y, int width, int height)
{
	int i;

	if (!video_set || width > 24 || height > 24 || width < 1 || height < 1)
		return;
	for (i = 0; i < height; i++)
		memcpy (VGA_MEM + (y + i) * BASEWIDTH + x, backingbuf + i * 24, width);
}

// The video options menu (menu.c's M_Video_* call these through
// vid_menudrawfn/vid_menukeyfn).
static void VID_MenuDraw (void)
{
	qpic_t *p = Draw_CachePic ("gfx/vidmodes.lmp");

	M_DrawPic ((320 - p->width) / 2, 4, p);
	M_Print (3*8, 36, "      320x200  (VGA mode 13h)");
	M_Print (3*8, 36 + 8*4, "The ARM-PC's VGA has no other");
	M_Print (3*8, 36 + 8*5, "mode that Quake can use.");
}

static void VID_MenuKey (int key)
{
	switch (key)
	{
	case K_ESCAPE:
		S_LocalSound ("misc/menu1.wav");
		M_Menu_Options_f ();
		break;
	default:
		break;
	}
}

static void VID_MenuInit (void)
{
	vid_menudrawfn = VID_MenuDraw;
	vid_menukeyfn = VID_MenuKey;
}
