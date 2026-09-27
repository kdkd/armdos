/* Copyright (C) 2014-2026 NY00123
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* be_egaemu.c - the EGA planar memory emulation of ReflectionHLE's
 * backend (src/backend/video/be_video_emu.c), cut down to the EGA 16-colour
 * routines Keen Dreams uses, for ARM-DOS (whose VGA has no planar modes).
 * Taken unchanged apart from the names of the video memory and the "dirty"
 * flag (g_armdosEgaGfx, g_armdosGfxDirty). The video memory holds one
 * 64-bit word per EGA address: 8 pixels, one byte each, the bit n of every
 * byte being plane n - so the displayed picture is a plain byte copy away
 * from a mode 13h frame (see be_armdos.c, BEL_ST_Present).
 */

#include <string.h>

#include "be_st.h"
#include "be_st_egavga_lookup_tables.h"

extern uint64_t *g_armdosEgaGfx;
extern volatile int g_armdosGfxDirty;

static void BEL_ST_LinearToEGAPlane_MemCopy(uint16_t planeDstOff, const uint8_t *linearSrc, uint16_t num, uint16_t planeNum)
{
	uint64_t planeInvRepeatedMask = ~g_be_st_lookup_egaplane_repeat[1 << planeNum];
	uint64_t *planeDstPtr = &g_armdosEgaGfx[planeDstOff];
	uint16_t bytesToEnd = 0x10000-planeDstOff;
	if (num <= bytesToEnd)
	{
		for (int i = 0; i < num; ++i, ++planeDstPtr, ++linearSrc)
			*planeDstPtr = ((*planeDstPtr) & planeInvRepeatedMask) | (g_be_st_lookup_linear_to_egaplane[*linearSrc] << planeNum);
	}
	else
	{
		for (int i = 0; i < bytesToEnd; ++i, ++planeDstPtr, ++linearSrc)
			*planeDstPtr = ((*planeDstPtr) & planeInvRepeatedMask) | (g_be_st_lookup_linear_to_egaplane[*linearSrc] << planeNum);
		planeDstPtr = g_armdosEgaGfx;
		for (int i = 0; i < num-bytesToEnd; ++i, ++planeDstPtr, ++linearSrc)
			*planeDstPtr = ((*planeDstPtr) & planeInvRepeatedMask) | (g_be_st_lookup_linear_to_egaplane[*linearSrc] << planeNum);
	}
	g_armdosGfxDirty = 1;
}

static void BEL_ST_EGAPlaneToLinear_MemCopy(uint8_t *linearDst, uint16_t planeSrcOff, uint16_t num, uint16_t planeNum)
{
	const uint64_t *planeSrcPtr = &g_armdosEgaGfx[planeSrcOff];
	uint16_t bytesToEnd = 0x10000-planeSrcOff;
	if (num <= bytesToEnd)
	{
		for (int i = 0; i < num; ++i, ++linearDst, ++planeSrcPtr)
			*linearDst = BEL_ST_Lookup_EGAPlaneToLinear((*planeSrcPtr)>>planeNum);
	}
	else
	{
		for (int i = 0; i < bytesToEnd; ++i, ++linearDst, ++planeSrcPtr)
			*linearDst = BEL_ST_Lookup_EGAPlaneToLinear((*planeSrcPtr)>>planeNum);
		planeSrcPtr = g_armdosEgaGfx;
		for (int i = 0; i < num-bytesToEnd; ++i, ++linearDst, ++planeSrcPtr)
			*linearDst = BEL_ST_Lookup_EGAPlaneToLinear((*planeSrcPtr)>>planeNum);
	}
	//No need to since we just read screen data
	//g_armdosGfxDirty = 1;
}

static void BEL_ST_EGAVGAPlaneToAllPlanes_MemCopy(
	uint16_t planeDstOff, uint16_t planeSrcOff, uint16_t num, int pixelsPerAddr)
{
	uint8_t *vidMemPtr = ((uint8_t *)g_armdosEgaGfx);
	uint16_t srcBytesToEnd = 0x10000-planeSrcOff;
	uint16_t dstBytesToEnd = 0x10000-planeDstOff;
	if (num <= srcBytesToEnd)
	{
		// Source is linear: Same as BE_Cross_LinearToWrapped_MemCopy here
		if (num <= dstBytesToEnd)
			memcpy(
				vidMemPtr + pixelsPerAddr*planeDstOff,
				vidMemPtr + pixelsPerAddr*planeSrcOff,
				pixelsPerAddr*num);
		else
		{
			memcpy(
				vidMemPtr + pixelsPerAddr*planeDstOff,
				vidMemPtr + pixelsPerAddr*planeSrcOff,
				pixelsPerAddr*dstBytesToEnd);
			memcpy(
				vidMemPtr,
				vidMemPtr + pixelsPerAddr*(planeSrcOff+dstBytesToEnd),
				pixelsPerAddr*(num-dstBytesToEnd));
		}
	}
	// Otherwise, check if at least the destination is linear
	else if (num <= dstBytesToEnd)
	{
		// Destination is linear: Same as
		// BE_Cross_WrappedToLinear_MemCopy, non-linear source
		memcpy(
			vidMemPtr + pixelsPerAddr*planeDstOff,
			vidMemPtr + pixelsPerAddr*planeSrcOff,
			pixelsPerAddr*srcBytesToEnd);
		memcpy(
			vidMemPtr + pixelsPerAddr*(planeDstOff+srcBytesToEnd),
			vidMemPtr,
			pixelsPerAddr*(num-srcBytesToEnd));
	}
	// BOTH buffers have wrapping. We don't check separately if
	// srcBytesToEnd==dstBytesToEnd (in such a case planeDstOff==planeSrcOff...)
	else if (srcBytesToEnd <= dstBytesToEnd)
	{
		memcpy(
			vidMemPtr + pixelsPerAddr*planeDstOff,
			vidMemPtr + pixelsPerAddr*planeSrcOff,
			pixelsPerAddr*srcBytesToEnd);
		memcpy(
			vidMemPtr + pixelsPerAddr*(planeDstOff+srcBytesToEnd),
			vidMemPtr,
			pixelsPerAddr*(dstBytesToEnd-srcBytesToEnd));
		memcpy(
			vidMemPtr,
			vidMemPtr + pixelsPerAddr*(dstBytesToEnd-srcBytesToEnd),
			pixelsPerAddr*(num-dstBytesToEnd));
	}
	else // srcBytesToEnd > dstBytesToEnd
	{
		memcpy(
			vidMemPtr + pixelsPerAddr*planeDstOff,
			vidMemPtr + pixelsPerAddr*planeSrcOff,
			pixelsPerAddr*dstBytesToEnd);
		memcpy(
			vidMemPtr,
			vidMemPtr + pixelsPerAddr*(planeSrcOff+dstBytesToEnd),
			pixelsPerAddr*(srcBytesToEnd-dstBytesToEnd));
		memcpy(
			vidMemPtr + pixelsPerAddr*(srcBytesToEnd-dstBytesToEnd),
			vidMemPtr,
			pixelsPerAddr*(num-srcBytesToEnd));
	}
	g_armdosGfxDirty = 1;
}

static void BEL_ST_EGAVGAPlane_MemSet(uint16_t planeDstOff, uint8_t value, uint32_t num, int pixelsPerAddr)
{
	uint8_t *vidMemPtr = ((uint8_t *)g_armdosEgaGfx);
	uint16_t bytesToEnd = 0x10000-planeDstOff;
	if (num <= bytesToEnd)
	{
		memset(vidMemPtr + pixelsPerAddr*planeDstOff, value, pixelsPerAddr*num);
	}
	else
	{
		memset(vidMemPtr + pixelsPerAddr*planeDstOff, value, pixelsPerAddr*bytesToEnd);
		memset(vidMemPtr, value, pixelsPerAddr*(num-bytesToEnd));
	}
	g_armdosGfxDirty = 1;
}

void BE_ST_EGAUpdateGFXByteInPlane(uint16_t destOff, uint8_t srcVal, uint16_t planeNum)
{
	g_armdosEgaGfx[destOff] = (g_armdosEgaGfx[destOff] & ~g_be_st_lookup_egaplane_repeat[1 << planeNum]) | (g_be_st_lookup_linear_to_egaplane[srcVal] << planeNum);
	g_armdosGfxDirty = 1;
}

void BE_ST_EGAUpdateGFXBufferInPlane(uint16_t destOff, const uint8_t *srcPtr, uint16_t num, uint16_t planeNum)
{
	BEL_ST_LinearToEGAPlane_MemCopy(destOff, srcPtr, num, planeNum);
	g_armdosGfxDirty = 1;
}

void BE_ST_EGAUpdateGFXByteInAllPlanesScrToScr(uint16_t destOff, uint16_t srcOff)
{
	g_armdosEgaGfx[destOff] = g_armdosEgaGfx[srcOff];
	g_armdosGfxDirty = 1;
}

void BE_ST_EGAUpdateGFXByteInPlaneScrToScr(uint16_t destOff, uint16_t srcOff, uint16_t planeNum)
{
	g_armdosEgaGfx[destOff] = (g_armdosEgaGfx[destOff] & ~g_be_st_lookup_egaplane_repeat[1<<planeNum]) | (g_armdosEgaGfx[srcOff] & g_be_st_lookup_egaplane_repeat[1<<planeNum]);
	g_armdosGfxDirty = 1;
}

void BE_ST_EGAUpdateGFXBitsInAllPlanesScrToScr(uint16_t destOff, uint16_t srcOff, uint8_t bitsMask)
{
	g_armdosEgaGfx[destOff] = (g_armdosEgaGfx[destOff] & ~g_be_st_lookup_egaplane_bitsmask[bitsMask]) | (g_armdosEgaGfx[srcOff] & g_be_st_lookup_egaplane_bitsmask[bitsMask]);
	g_armdosGfxDirty = 1;
}

void BE_ST_EGAUpdateGFXBufferInAllPlanesScrToScr(uint16_t destOff, uint16_t srcOff, uint16_t num)
{
	BEL_ST_EGAVGAPlaneToAllPlanes_MemCopy(destOff, srcOff, num, 8);
	g_armdosGfxDirty = 1;
}

uint8_t BE_ST_EGAFetchGFXByteFromPlane(uint16_t destOff, uint16_t planeNum)
{
	return BEL_ST_Lookup_EGAPlaneToLinear(g_armdosEgaGfx[destOff]>>planeNum);
}

void BE_ST_EGAFetchGFXBufferFromPlane(uint8_t *destPtr, uint16_t srcOff, uint16_t num, uint16_t planeNum)
{
	BEL_ST_EGAPlaneToLinear_MemCopy(destPtr, srcOff, num, planeNum);
}

void BE_ST_EGAUpdateGFXBitsFrom4bitsPixel(uint16_t destOff, uint8_t color, uint8_t bitsMask)
{
	color &= 0xF; // We may get a larger value in The Catacombs Armageddon (sky color)
	g_armdosEgaGfx[destOff] = (g_armdosEgaGfx[destOff] & ~g_be_st_lookup_egaplane_bitsmask[bitsMask]) | (g_be_st_lookup_egaplane_repeat[color] & g_be_st_lookup_egaplane_bitsmask[bitsMask]);
	g_armdosGfxDirty = 1;
}

void BE_ST_EGAUpdateGFXBufferFrom4bitsPixel(uint16_t destOff, uint8_t color, uint16_t count)
{
	color &= 0xF; // We may get a larger value in The Catacombs Armageddon (sky color)
	BEL_ST_EGAVGAPlane_MemSet(destOff, color, count, 8);
	g_armdosGfxDirty = 1;
}

void BE_ST_EGAUpdateGFXBufferFrom4bitsPixelInPairs(uint16_t destOff, uint8_t color, uint16_t pairsCount)
{
	color &= 0xF; // We may get a larger value in The Catacombs Armageddon (sky color)
	BEL_ST_EGAVGAPlane_MemSet(destOff, color, 2*pairsCount, 8);
	g_armdosGfxDirty = 1;
}

void BE_ST_EGAXorGFXByteByPlaneMask(uint16_t destOff, uint8_t srcVal, uint16_t planeMask)
{
	g_armdosEgaGfx[destOff] ^= ((g_be_st_lookup_egaplane_repeat[planeMask] & g_be_st_lookup_egaplane_bitsmask[srcVal]));
	g_armdosGfxDirty = 1;
}
