/* net_armdos.c -- the network driver list for ARM-DOS: loopback only (the
 * single-player game talks to its own server). After uHexen2's
 * dos/net_dos.c without the IPX, serial and WatTCP drivers.
 *
 * Copyright (C) 1996-1997  Id Software, Inc.
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

#include "q_stdinc.h"
#include "arch_def.h"
#include "net_sys.h"
#include "quakedef.h"

#include "net_defs.h"
#include "net_loop.h"

net_driver_t net_drivers[] =
{
	{	"Loopback",
		false,
		Loop_Init,
		Loop_Listen,
		Loop_SearchForHosts,
		Loop_Connect,
		Loop_CheckNewConnections,
		Loop_GetMessage,
		Loop_SendMessage,
		Loop_SendUnreliableMessage,
		Loop_CanSendMessage,
		Loop_CanSendUnreliableMessage,
		Loop_Close,
		Loop_Shutdown
	}
};

const int net_numdrivers = Q_COUNTOF(net_drivers);

net_landriver_t	net_landrivers[1];
const int net_numlandrivers = 0;
