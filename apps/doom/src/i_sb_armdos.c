//
// i_sb_armdos.c - DOOM's sound effects on the ARM-PC's Sound Blaster 16.
//
// Copyright(C) 2026 the ARM-DOS project. GPL-2 or later (see LICENSE).
// The DS* lump handling (DMX header, 16 padding bytes at each end) and the
// volume/separation law follow chocolate-doom src/i_sdlsound.c
// (Copyright(C) 2005-2014 Simon Howard).
//
// Like DMX: the WAD's 8-bit DS* sounds are mixed in software, eight channels,
// into a 16-bit stereo 11025 Hz stream that the SB plays with auto-init DMA
// (DMA 5; sdk sb.h sb_start). The mixing happens in the SB's IRQ handler,
// one 256-frame half-buffer (23 ms) at a time.
//

#include <string.h>
#include <stdint.h>

#include <armdos.h>
#include <sb.h>

#include "doomtype.h"
#include "deh_str.h"
#include "i_sound.h"
#include "m_misc.h"
#include "w_wad.h"
#include "z_zone.h"
#include "i_armdos.h"

#define MIX_RATE    11025
#define MIX_FRAMES  256
#define NUM_CHANNELS 16

typedef struct
{
    const uint8_t *data;         // unsigned 8-bit samples
    uint32_t len;                // in 16.16
    uint32_t pos;                // 16.16
    uint32_t step;               // 16.16: sample rate / 11025
    int left, right;             // 0..255
    volatile int active;
} sb_channel_t;

static sb_channel_t chans[NUM_CHANNELS];
static boolean sb_initialized;
static boolean use_prefix;
static int32_t mixbuf[MIX_FRAMES * 2];

// ------------------------------------------------------------ the IRQ side

static void MixBlock(void *buf, unsigned bytes, void *user)
{
    int16_t *out = buf;
    unsigned frames = bytes / 4, i, c;

    (void)user;
    if (frames > MIX_FRAMES)
        frames = MIX_FRAMES;
    memset(mixbuf, 0, frames * 2 * sizeof(int32_t));
    for (c = 0; c < NUM_CHANNELS; c++)
    {
        sb_channel_t *ch = &chans[c];
        int32_t *m = mixbuf;
        uint32_t pos, step, len;
        int l, r;

        if (!ch->active)
            continue;
        pos = ch->pos; step = ch->step; len = ch->len; l = ch->left; r = ch->right;
        for (i = 0; i < frames && pos < len; i++)
        {
            int s = (int)ch->data[pos >> 16] - 128;
            m[0] += s * l;
            m[1] += s * r;
            m += 2;
            pos += step;
        }
        ch->pos = pos;
        if (pos >= len)
            ch->active = 0;
    }
    for (i = 0; i < frames * 2; i++)
    {
        int32_t v = mixbuf[i] >> 1;
        out[i] = v > 32767 ? 32767 : v < -32768 ? -32768 : (int16_t)v;
    }
}

// ------------------------------------------------------------ DOOM's side

static void SetParams(sb_channel_t *ch, int vol, int sep)
{
    int left = ((254 - sep) * vol) / 127, right = (sep * vol) / 127;

    ch->left = left < 0 ? 0 : left > 255 ? 255 : left;
    ch->right = right < 0 ? 0 : right > 255 ? 255 : right;
}

static int SB_StartSound(sfxinfo_t *sfxinfo, int channel, int vol, int sep)
{
    const uint8_t *lump;
    unsigned lumplen, rate, length;
    sb_channel_t *ch;

    if (!sb_initialized || channel < 0 || channel >= NUM_CHANNELS || sfxinfo->lumpnum < 0)
        return -1;
    // DMX sounds stay in memory once loaded (PU_STATIC: the IRQ reads them)
    lump = sfxinfo->driver_data;
    if (lump == NULL)
    {
        lump = W_CacheLumpNum(sfxinfo->lumpnum, PU_STATIC);
        sfxinfo->driver_data = (void *)lump;
    }
    lumplen = W_LumpLength(sfxinfo->lumpnum);
    if (lumplen < 8 || lump[0] != 0x03 || lump[1] != 0x00)
        return -1;
    rate = lump[2] | (lump[3] << 8);
    length = lump[4] | (lump[5] << 8) | (lump[6] << 16) | ((unsigned)lump[7] << 24);
    if (length > lumplen - 8 || length <= 48 || rate == 0)
        return -1;
    // DMX skips the first 16 and the last 16 bytes of the sound
    ch = &chans[channel];
    armdos_disable();
    ch->data = lump + 8 + 16;
    ch->len = (length - 32) << 16;
    ch->pos = 0;
    ch->step = (uint32_t)(((uint64_t)rate << 16) / MIX_RATE);
    SetParams(ch, vol, sep);
    ch->active = 1;
    armdos_enable();
    return channel;
}

static void SB_StopSound(int channel)
{
    if (sb_initialized && channel >= 0 && channel < NUM_CHANNELS)
        chans[channel].active = 0;
}

static boolean SB_SoundIsPlaying(int channel)
{
    return sb_initialized && channel >= 0 && channel < NUM_CHANNELS && chans[channel].active;
}

static void SB_UpdateSoundParams(int channel, int vol, int sep)
{
    if (!sb_initialized || channel < 0 || channel >= NUM_CHANNELS)
        return;
    armdos_disable();
    SetParams(&chans[channel], vol, sep);
    armdos_enable();
}

static int SB_GetSfxLumpNum(sfxinfo_t *sfx)
{
    char namebuf[9];

    if (use_prefix)
        M_snprintf(namebuf, sizeof(namebuf), "ds%s", DEH_String(sfx->name));
    else
        M_StringCopy(namebuf, DEH_String(sfx->name), sizeof(namebuf));
    return W_CheckNumForName(namebuf);
}

static boolean SB_Init(boolean use_sfx_prefix)
{
    use_prefix = use_sfx_prefix;
    if (!sb_info.present)
        return false;
    memset(chans, 0, sizeof(chans));
    if (!sb_start(MIX_RATE, SB_16BIT | SB_STEREO | SB_SIGNED, MIX_FRAMES * 4, MixBlock, NULL))
        return false;
    sb_initialized = true;
    return true;
}

static void SB_Shutdown(void)
{
    if (sb_initialized)
        sb_stop();
    sb_initialized = false;
}

static void SB_Update(void) { }

static snddevice_t sb_devices[] = { SNDDEVICE_SB };

sound_module_t I_ArmdosSbSoundModule =
{
    sb_devices,
    arrlen(sb_devices),
    SB_Init,
    SB_Shutdown,
    SB_GetSfxLumpNum,
    SB_Update,
    SB_UpdateSoundParams,
    SB_StartSound,
    SB_StopSound,
    SB_SoundIsPlaying,
    NULL,
};
