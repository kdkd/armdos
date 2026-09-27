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
// snd_armdos.c -- Sound Blaster 16 sound for ARM-DOS (after id's snd_dos.c)
//
// id's snd_dos.c programmed the SB and the 8237 itself and let the mixer
// write into the auto-init DMA buffer, taking the play position from the DMA
// controller's count register. Here the SDK's sb.h runs the auto-init DMA
// stream (BLASTER=A220 I7 D1 H5 T6; the IRQ is 7): Quake mixes into a ring
// buffer as before, and the SB's IRQ handler copies the next block of the
// ring into the half of the DMA buffer that just finished and advances the
// "DMA position" that SNDDMA_GetDMAPos reports. Like QUAKE.EXE: 11025 Hz
// (-sspeed N), 16-bit stereo on an SB16 (DSP 4.xx), 8-bit stereo on an SB
// Pro, 8-bit mono otherwise.

#include <string.h>
#include <stdlib.h>

#include <armdos.h>
#include <sb.h>

#include "quakedef.h"

#define RING_BYTES  8192            // Quake's mixing buffer (power of two)
#define BLOCK_BYTES 512             // one SB IRQ = 128 frames of 16-bit stereo

static byte *ring;
static volatile unsigned ring_pos;  // bytes played (copied to the card)
static int snd_started;

static void SB_Fill (void *buf, unsigned bytes, void *user)
{
	unsigned pos = ring_pos, n;
	byte *dst = buf;

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

qboolean SNDDMA_Init (void)
{
	int rc;
	unsigned format;

	if (!sb_detect (NULL))
		return false;

	shm = &sn;
	shm->speed = 11025;
	rc = COM_CheckParm ("-sspeed");
	if (rc)
		shm->speed = Q_atoi (com_argv[rc+1]);

	if (sb_info.dsp_major >= 4)
	{
		shm->channels = 2;
		shm->samplebits = 16;
		format = SB_16BIT | SB_STEREO | SB_SIGNED;
	}
	else if (sb_info.dsp_major == 3)
	{
		shm->channels = 2;
		shm->samplebits = 8;
		format = SB_8BIT | SB_STEREO;
	}
	else
	{
		shm->channels = 1;
		shm->samplebits = 8;
		format = SB_8BIT;
	}

	Cmd_AddCommand ("sbinfo", SB_Info_f);

	ring = malloc (RING_BYTES);
	if (!ring)
	{
		Con_Printf ("Couldn't allocate sound dma buffer");
		return false;
	}
	// silence: 0 for signed 16-bit, 80h for unsigned 8-bit
	memset (ring, shm->samplebits == 8 ? 0x80 : 0, RING_BYTES);
	ring_pos = 0;

	shm->soundalive = true;
	shm->splitbuffer = false;
	shm->samples = RING_BYTES / (shm->samplebits / 8);
	shm->samplepos = 0;
	shm->submission_chunk = 1;
	shm->buffer = ring;

	if (!sb_start (shm->speed, format, BLOCK_BYTES, SB_Fill, NULL))
	{
		Con_Printf ("Sound Blaster: couldn't start DMA\n");
		free (ring);
		ring = NULL;
		return false;
	}
	sb_speaker (1);
	snd_started = 1;
	Con_Printf ("%s at %Xh, IRQ %u\n", sb_name (), sb_info.port, sb_info.irq);
	return true;
}

int SNDDMA_GetDMAPos (void)
{
	if (!snd_started)
		return 0;
	shm->samplepos = (ring_pos / (shm->samplebits / 8)) & (shm->samples - 1);
	return shm->samplepos;
}

void SNDDMA_Shutdown (void)
{
	if (!snd_started)
		return;
	sb_stop ();
	snd_started = 0;
}

void SNDDMA_Submit (void)
{
}
