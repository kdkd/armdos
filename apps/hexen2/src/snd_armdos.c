/* snd_armdos.c -- Sound Blaster 16 sound for ARM-DOS (the "snddrv_blaster"
 * driver of uHexen2's snd_sb.c, rewritten on the SDK's sb.h like the ARM-DOS
 * Quake's snd_armdos.c)
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
 *
 * snd_sb.c programmed the SB and the 8237 itself and let the mixer write
 * into the auto-init DMA buffer, taking the play position from the DMA
 * controller. Here sb.h runs the auto-init stream (BLASTER=A220 I7 D1 H5 T6):
 * the engine mixes into a ring buffer as before, and the SB's IRQ handler
 * copies the next block of the ring into the half of the DMA buffer that
 * just finished and advances the position S_BLASTER_GetDMAPos reports.
 * 11025 Hz by default (Hexen II's sounds are 11 kHz; -sndspeed 22050 for
 * more), 16-bit stereo on an SB16 (DSP 4.xx), 8-bit stereo on an SB Pro,
 * 8-bit mono otherwise; -sndbits 8 and -sndmono as in uHexen2.
 */

#include <string.h>
#include <stdlib.h>

#include <armdos.h>
#include <sb.h>

#include "quakedef.h"
#include "snd_sys.h"
#include "snd_sb.h"

#define RING_BYTES	8192		/* the mixing buffer (power of two) */
#define BLOCK_BYTES	512		/* one SB IRQ = 128 frames of 16-bit stereo */

static char	s_sb_driver[] = "SoundBlaster";

static dma_t	*sb_shm;
static byte	*ring;
static volatile unsigned	ring_pos;	/* bytes played (copied to the card) */
static int	snd_started;

static void SB_Fill (void *buf, unsigned bytes, void *user)
{
	unsigned	pos = ring_pos, n;
	byte		*dst = (byte *) buf;

	(void)user;
	while (bytes)
	{
		n = RING_BYTES - (pos & (RING_BYTES - 1));
		if (n > bytes)
			n = bytes;
		memcpy (dst, ring + (pos & (RING_BYTES - 1)), n);
		dst += n;
		pos += n;
		bytes -= n;
	}
	ring_pos = pos;
}

static void SB_Info_f (void)
{
	Con_Printf ("%s at %Xh, IRQ %u, DMA %u/%u, DSP %u.%02u\n", sb_name (),
		    sb_info.port, sb_info.irq, sb_info.dma8, sb_info.dma16,
		    sb_info.dsp_major, sb_info.dsp_minor);
}

static qboolean S_BLASTER_Init (dma_t *dma)
{
	unsigned	format;
	int		i;

	if (!sb_detect (NULL))
		return false;

	sb_shm = dma;
	memset (dma, 0, sizeof(*dma));
	dma->speed = 11025;
	i = COM_CheckParm ("-sndspeed");
	if (i && i < com_argc - 1)
		dma->speed = desired_speed;	/* validated by snd_dma.c */

	if (sb_info.dsp_major >= 4 && desired_bits == 16)
	{
		dma->channels = desired_channels;
		dma->samplebits = 16;
		format = SB_16BIT | SB_SIGNED | (dma->channels == 2 ? SB_STEREO : 0);
	}
	else if (sb_info.dsp_major >= 3)
	{
		dma->channels = desired_channels;
		dma->samplebits = 8;
		format = SB_8BIT | (dma->channels == 2 ? SB_STEREO : 0);
	}
	else
	{
		dma->channels = 1;
		dma->samplebits = 8;
		format = SB_8BIT;
	}

	ring = (byte *) malloc (RING_BYTES);
	if (!ring)
	{
		Con_Printf ("Couldn't allocate sound dma buffer\n");
		return false;
	}
	/* silence: 0 for signed 16-bit, 80h for unsigned 8-bit */
	memset (ring, dma->samplebits == 8 ? 0x80 : 0, RING_BYTES);
	ring_pos = 0;

	dma->samples = RING_BYTES / (dma->samplebits / 8);
	dma->samplepos = 0;
	dma->submission_chunk = 1;
	dma->signed8 = 0;
	dma->buffer = ring;

	if (!sb_start (dma->speed, format, BLOCK_BYTES, SB_Fill, NULL))
	{
		Con_Printf ("Sound Blaster: couldn't start DMA\n");
		free (ring);
		ring = NULL;
		return false;
	}
	sb_speaker (1);
	snd_started = 1;
	shm = dma;
	Cmd_AddCommand ("sbinfo", SB_Info_f);
	Con_Printf ("%s at %Xh, IRQ %u\n", sb_name (), sb_info.port, sb_info.irq);
	return true;
}

static int S_BLASTER_GetDMAPos (void)
{
	if (!snd_started)
		return 0;
	sb_shm->samplepos = (ring_pos / (sb_shm->samplebits / 8)) & (sb_shm->samples - 1);
	return sb_shm->samplepos;
}

static void S_BLASTER_Shutdown (void)
{
	if (!snd_started)
		return;
	sb_stop ();
	snd_started = 0;
	free (ring);
	ring = NULL;
	shm = NULL;
}

static void S_BLASTER_LockBuffer (void)
{
}

static void S_BLASTER_Submit (void)
{
}

static void S_BLASTER_BlockSound (void)
{
}

static void S_BLASTER_UnblockSound (void)
{
}

snd_driver_t snddrv_blaster =
{
	S_BLASTER_Init,
	S_BLASTER_Shutdown,
	S_BLASTER_GetDMAPos,
	S_BLASTER_LockBuffer,
	S_BLASTER_Submit,
	S_BLASTER_BlockSound,
	S_BLASTER_UnblockSound,
	s_sb_driver,
	SNDDRV_ID_SB_DOS,
	false,
	NULL
};
