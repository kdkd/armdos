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
// in_armdos.c -- the mouse through the INT 33h driver (MOUSE.COM) and the
// joystick on the game port (201h), as in id's in_dos.c (the -control
// external driver interface is left out)

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <armdos.h>

#include "quakedef.h"

cvar_t  m_filter = {"m_filter","1"};

static qboolean mouse_avail;
static int      mouse_buttons;
static int      mouse_oldbuttonstate;
static float    mouse_x, mouse_y, old_mouse_x, old_mouse_y;

// ---- ARM-DOS joystick: in_dos.c's game port code (IBM game adapter, 201h) ----
cvar_t  in_joystick = {"joystick","1"};
cvar_t  joy_numbuttons = {"joybuttons","4", true};

static qboolean joy_avail;
static int      joy_oldbuttonstate;
static int      joy_buttonstate;
static int      joyxl, joyxh, joyyl, joyyh;
static int      joystickx, joysticky;

static void IN_StartupJoystick (void);
static qboolean IN_ReadJoystick (void);
static void IN_JoyMove (usercmd_t *cmd);
// ---- end ARM-DOS joystick declarations ----

static void Force_CenterView_f (void)
{
	cl.viewangles[PITCH] = 0;
}

static int mouse_call (int ax, int *bx, int *cx, int *dx)
{
	struct armregs r;

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
	int bx;

	if (COM_CheckParm ("-nomouse"))
		return;
	// no driver: the vector is 0 (DOS 4 leaves it pointing nowhere)
	if (armdos_getvect (0x33) == NULL)
	{
		Con_Printf ("No mouse found\n");
		return;
	}
	mouse_avail = mouse_call (0, &bx, NULL, NULL) != 0;   // reset driver
	if (!mouse_avail)
	{
		Con_Printf ("No mouse found\n");
		return;
	}
	mouse_buttons = bx == 0xFFFF ? 2 : bx;
	if (mouse_buttons > 3)
		mouse_buttons = 3;
	Con_Printf ("%d-button mouse available\n", mouse_buttons);
	mouse_call (11, NULL, NULL, NULL);       // clear the motion counters
}

void IN_Init (void)
{
	Cvar_RegisterVariable (&m_filter);
	Cmd_AddCommand ("force_centerview", Force_CenterView_f);
	IN_StartupMouse ();
	Cvar_RegisterVariable (&in_joystick);      // ARM-DOS joystick
	Cvar_RegisterVariable (&joy_numbuttons);
	IN_StartupJoystick ();
}

void IN_Shutdown (void)
{
}

void IN_Commands (void)
{
	int i, buttons;

	if (mouse_avail)
	{
		mouse_call (3, &buttons, NULL, NULL);    // read buttons
		for (i = 0; i < mouse_buttons; i++)
		{
			if ((buttons & (1<<i)) && !(mouse_oldbuttonstate & (1<<i)))
				Key_Event (K_MOUSE1 + i, true);
			if (!(buttons & (1<<i)) && (mouse_oldbuttonstate & (1<<i)))
				Key_Event (K_MOUSE1 + i, false);
		}
		mouse_oldbuttonstate = buttons;
	}

	if (joy_avail)                           // ARM-DOS joystick (in_dos.c)
	{
		joy_buttonstate = ((armdos_inb(0x201) >> 4)&15)^15;
		for (i=0 ; i<joy_numbuttons.value ; i++)
		{
			if ( (joy_buttonstate & (1<<i)) && !(joy_oldbuttonstate & (1<<i)) )
				Key_Event (K_JOY1 + i, true);
			if ( !(joy_buttonstate & (1<<i)) && (joy_oldbuttonstate & (1<<i)) )
				Key_Event (K_JOY1 + i, false);
		}
		joy_oldbuttonstate = joy_buttonstate;
	}
}

static void IN_MouseMove (usercmd_t *cmd);

void IN_Move (usercmd_t *cmd)
{
	IN_MouseMove (cmd);
	IN_JoyMove (cmd);                        // ARM-DOS joystick
}

static void IN_MouseMove (usercmd_t *cmd)
{
	int mx, my;

	if (!mouse_avail)
		return;

	mouse_call (11, NULL, &mx, &my);         // read motion counters (mickeys)

	if (m_filter.value)
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

	// add mouse X/Y movement to cmd
	if ((in_strafe.state & 1) || (lookstrafe.value && (in_mlook.state & 1)))
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
		if ((in_strafe.state & 1) && noclip_anglehack)
			cmd->upmove -= m_forward.value * mouse_y;
		else
			cmd->forwardmove -= m_forward.value * mouse_y;
	}
}

void IN_ClearStates (void)
{
}

/*
============================================================================

					JOYSTICK (ARM-DOS: id's in_dos.c code, the game port at 201h)

============================================================================
*/

// ARM-DOS: IN_Init runs before the video mode is set, so the console is not on the
// screen yet; the calibration prompts also go to the DOS text screen, where the
// startup messages are.
extern qboolean scr_initialized;

static void JoyPrintf (char *fmt, ...)
{
	va_list argptr;
	char    text[256];

	va_start (argptr, fmt);
	vsnprintf (text, sizeof(text), fmt, argptr);
	va_end (argptr);
	Con_Printf ("%s", text);
	if (!scr_initialized)
	{
		fputs (text, stdout);
		fflush (stdout);
	}
}

static void IN_JoyMove (usercmd_t *cmd)
{
	float	speed, aspeed;

	if (!joy_avail || !in_joystick.value)
		return;

	IN_ReadJoystick ();
	if (joysticky > joyyh*2 || joystickx > joyxh*2)
		return;		// assume something jumped in and messed up the joystick
					// reading time (win 95)

	if (in_speed.state & 1)
		speed = cl_movespeedkey.value;
	else
		speed = 1;
	aspeed = speed*host_frametime;

	if (in_strafe.state & 1)
	{
		if (joystickx < joyxl)
			cmd->sidemove -= speed*cl_sidespeed.value;
		else if (joystickx > joyxh)
			cmd->sidemove += speed*cl_sidespeed.value;
	}
	else
	{
		if (joystickx < joyxl)
			cl.viewangles[YAW] += aspeed*cl_yawspeed.value;
		else if (joystickx > joyxh)
			cl.viewangles[YAW] -= aspeed*cl_yawspeed.value;
		cl.viewangles[YAW] = anglemod(cl.viewangles[YAW]);
	}

	if (in_mlook.state & 1)
	{
		if (m_pitch.value < 0)
			speed *= -1;

		if (joysticky < joyyl)
			cl.viewangles[PITCH] += aspeed*cl_pitchspeed.value;
		else if (joysticky > joyyh)
			cl.viewangles[PITCH] -= aspeed*cl_pitchspeed.value;
	}
	else
	{
		if (joysticky < joyyl)
			cmd->forwardmove += speed*cl_forwardspeed.value;
		else if (joysticky > joyyh)
			cmd->forwardmove -= speed*cl_backspeed.value;
	}
}

static qboolean IN_ReadJoystick (void)
{
	int		b;
	int		count;

	joystickx = 0;
	joysticky = 0;

	count = 0;

	b = armdos_inb(0x201);
	armdos_outb(0x201, b);

// clear counters
	while (++count < 10000)
	{
		b = armdos_inb(0x201);

		joystickx += b&1;
		joysticky += (b&2)>>1;
		if ( !(b&3) )
			return true;
	}

	Con_Printf ("IN_ReadJoystick: no response\n");
	joy_avail = false;
	return false;
}

static qboolean WaitJoyButton (void)
{
	int             oldbuttons, buttons;

	oldbuttons = 0;
	do
	{
		key_count = -1;
		Sys_SendKeyEvents ();
		key_count = 0;
		if (key_lastpress == K_ESCAPE)
		{
			JoyPrintf ("aborted.\n");
			return false;
		}
		key_lastpress = 0;
		SCR_UpdateScreen ();
		buttons =  ((armdos_inb(0x201) >> 4)&1)^1;
		if (buttons != oldbuttons)
		{
			oldbuttons = buttons;
			continue;
		}
	} while ( !buttons);

	do
	{
		key_count = -1;
		Sys_SendKeyEvents ();
		key_count = 0;
		if (key_lastpress == K_ESCAPE)
		{
			JoyPrintf ("aborted.\n");
			return false;
		}
		key_lastpress = 0;
		SCR_UpdateScreen ();
		buttons =  ((armdos_inb(0x201) >> 4)&1)^1;
		if (buttons != oldbuttons)
		{
			oldbuttons = buttons;
			continue;
		}
	} while ( buttons);

	return true;
}

static void IN_StartupJoystick (void)
{
	int     centerx, centery;

	JoyPrintf ("\n");

	joy_avail = false;
	if ( COM_CheckParm ("-nojoy") )
		return;

	if (!IN_ReadJoystick ())
	{
		joy_avail = false;
		JoyPrintf ("joystick not found\n");
		return;
	}

	JoyPrintf ("joystick found\n");

	JoyPrintf ("CENTER the joystick\nand press button 1 (ESC to skip):\n");
	if (!WaitJoyButton ())
		return;
	IN_ReadJoystick ();
	centerx = joystickx;
	centery = joysticky;

	JoyPrintf ("Push the joystick to the UPPER LEFT\nand press button 1 (ESC to skip):\n");
	if (!WaitJoyButton ())
		return;
	IN_ReadJoystick ();
	joyxl = (centerx + joystickx)/2;
	joyyl = (centerx + joysticky)/2;	// (sic: id's in_dos.c uses centerx here)

	JoyPrintf ("Push the joystick to the LOWER RIGHT\nand press button 1 (ESC to skip):\n");
	if (!WaitJoyButton ())
		return;
	IN_ReadJoystick ();
	joyxh = (centerx + joystickx)/2;
	joyyh = (centery + joysticky)/2;

	joy_avail = true;
	JoyPrintf ("joystick configured.\n");

	JoyPrintf ("\n");
}
