/* in_armdos.c -- the mouse through the INT 33h driver (MOUSE.COM), as in
 * uHexen2's in_dos.c (the joystick and the -control external driver
 * interface are left out: the ARM-PC has no game port).
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
 */

#include <string.h>
#include <armdos.h>

#include "quakedef.h"

static cvar_t	m_filter = {"m_filter", "1", CVAR_NONE};

static qboolean	mouse_avail;
static qboolean	mouseactive;
static int	mouse_buttons;
static int	mouse_oldbuttonstate;
static float	mouse_x, mouse_y, old_mouse_x, old_mouse_y;

static void Force_CenterView_f (void)
{
	cl.viewangles[PITCH] = 0;
}

static int mouse_call (int ax, int *bx, int *cx, int *dx)
{
	struct armregs	r;

	memset (&r, 0, sizeof(r));
	r.r0 = ax;
	_armdos_int33 (&r);
	if (bx) *bx = r.r1 & 0xFFFF;
	if (cx) *cx = (short)(r.r2 & 0xFFFF);
	if (dx) *dx = (short)(r.r3 & 0xFFFF);
	return r.r0 & 0xFFFF;
}

static void IN_StartupMouse (void)
{
	int	bx;

	if (safemode || COM_CheckParm ("-nomouse"))
		return;
	/* no driver: the vector is 0 */
	if (armdos_getvect (0x33) == NULL)
	{
		Con_Printf ("No mouse found\n");
		return;
	}
	mouse_avail = mouse_call (0, &bx, NULL, NULL) != 0;	/* reset driver */
	if (!mouse_avail)
	{
		Con_Printf ("No mouse found\n");
		return;
	}
	mouse_buttons = (bx == 0xFFFF) ? 2 : bx;
	if (mouse_buttons > 3)
		mouse_buttons = 3;
	Con_Printf ("%d-button mouse available\n", mouse_buttons);
	mouseactive = true;
	mouse_call (11, NULL, NULL, NULL);	/* clear the motion counters */
}

void IN_ActivateMouse (void)
{
	if (mouse_avail)
	{
		old_mouse_x = old_mouse_y = 0;
		mouse_call (11, NULL, NULL, NULL);
		mouseactive = true;
	}
}

void IN_DeactivateMouse (void)
{
	mouseactive = false;
}

void IN_ShowMouse (void)
{
}

void IN_HideMouse (void)
{
}

void IN_ClearStates (void)
{
}

void IN_Init (void)
{
	Cvar_RegisterVariable (&m_filter);
	Cmd_AddCommand ("force_centerview", Force_CenterView_f);
	IN_StartupMouse ();
}

void IN_ReInit (void)
{
}

void IN_Shutdown (void)
{
}

void IN_SendKeyEvents (void)
{
	Sys_SendKeyEvents ();
}

void IN_Commands (void)
{
	int	i, buttons;

	if (!mouse_avail)
		return;
	mouse_call (3, &buttons, NULL, NULL);	/* read buttons */
	for (i = 0; i < mouse_buttons; i++)
	{
		if ((buttons & (1<<i)) && !(mouse_oldbuttonstate & (1<<i)))
			Key_Event (K_MOUSE1 + i, true);
		if (!(buttons & (1<<i)) && (mouse_oldbuttonstate & (1<<i)))
			Key_Event (K_MOUSE1 + i, false);
	}
	mouse_oldbuttonstate = buttons;
}

void IN_Move (usercmd_t *cmd)
{
	int	mx, my;

	if (!mouse_avail || !mouseactive)
		return;

	mouse_call (11, NULL, &mx, &my);	/* motion counters (mickeys) */

	if (m_filter.integer)
	{
		mouse_x = (mx + old_mouse_x) * 0.5;
		mouse_y = (my + old_mouse_y) * 0.5;
	}
	else
	{
		mouse_x = mx;
		mouse_y = my;
	}
	old_mouse_x = mx;
	old_mouse_y = my;

	mouse_x *= sensitivity.value;
	mouse_y *= sensitivity.value;

	if ((in_strafe.state & 1) || (lookstrafe.integer && (in_mlook.state & 1)))
		cmd->sidemove += m_side.value * mouse_x;
	else
		cl.viewangles[YAW] -= m_yaw.value * mouse_x;

	if (in_mlook.state & 1)
		V_StopPitchDrift ();

	if ((in_mlook.state & 1) && !(in_strafe.state & 1))
	{
		cl.viewangles[PITCH] += m_pitch.value * mouse_y;
		if (cl.viewangles[PITCH] > 80)
			cl.viewangles[PITCH] = 80;
		if (cl.viewangles[PITCH] < -70)
			cl.viewangles[PITCH] = -70;
	}
	else
	{
		if ((in_strafe.state & 1) && (cl.v.movetype == MOVETYPE_NOCLIP))
			cmd->upmove -= m_forward.value * mouse_y;
		else
			cmd->forwardmove -= m_forward.value * mouse_y;
	}

	if (cl.idealroll == 0)	/* did the keyboard set it already? */
	{
		if (cl.v.movetype == MOVETYPE_FLY)
		{
			if (mouse_x < 0)
				cl.idealroll = -10;
			else if (mouse_x > 0)
				cl.idealroll = 10;
		}
	}
}
