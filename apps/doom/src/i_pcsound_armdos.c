//
// i_pcsound_armdos.c - DOOM's PC speaker sound effects on ARM-DOS.
//
// Copyright(C) 2005-2014 Simon Howard (chocolate-doom src/i_pcsound.c,
// whose tone table and lump handling this follows)
// Copyright(C) 2026 the ARM-DOS project
// GPL-2 or later (see LICENSE).
//
// The DP* lumps are what DOOM's DMX library played through the PC speaker:
// a 4-byte header (0x0000, u16 count) and one byte per 1/140 s, 0 = silence,
// otherwise an index into DMX's table of PIT divisors. The 140 Hz timer
// interrupt (doomgeneric_armdos.c) calls I_ArmdosSpeakerTick(), which
// programs PIT channel 2 (mode 3, square wave) and gates the speaker through
// port 0x61 bits 0-1 - exactly what the real hardware needs.
//
// Music is the Sound Blaster's OPL3 (i_oplmus_armdos.c); with the PC speaker
// there is none (music device "none", the null module below).
//

#include <string.h>
#include <stdint.h>

#include <armdos.h>

#include "doomtype.h"
#include "deh_str.h"
#include "i_sound.h"
#include "m_misc.h"
#include "w_wad.h"
#include "z_zone.h"
#include "i_armdos.h"

static const uint16_t divisors[] = {
    0,
    6818, 6628, 6449, 6279, 6087, 5906, 5736, 5575,
    5423, 5279, 5120, 4971, 4830, 4697, 4554, 4435,
    4307, 4186, 4058, 3950, 3836, 3728, 3615, 3519,
    3418, 3323, 3224, 3131, 3043, 2960, 2875, 2794,
    2711, 2633, 2560, 2485, 2415, 2348, 2281, 2213,
    2153, 2089, 2032, 1975, 1918, 1864, 1810, 1757,
    1709, 1659, 1612, 1565, 1521, 1478, 1435, 1395,
    1355, 1316, 1280, 1242, 1207, 1173, 1140, 1107,
    1075, 1045, 1015,  986,  959,  931,  905,  879,
     854,  829,  806,  783,  760,  739,  718,  697,
     677,  658,  640,  621,  604,  586,  570,  553,
     538,  522,  507,  493,  479,  465,  452,  439,
     427,  415,  403,  391,  380,  369,  359,  348,
     339,  329,  319,  310,  302,  293,  285,  276,
     269,  261,  253,  246,  239,  232,  226,  219,
     213,  207,  201,  195,  190,  184,  179,
};

static boolean pcs_initialized;
static boolean use_prefix;

// Shared with the timer interrupt: only changed with IRQs disabled.
static const uint8_t *volatile snd_pos;
static volatile unsigned int snd_remaining;
static unsigned int spk_divisor;          // what channel 2 is playing, 0 = off

static uint8_t *snd_lump;
static int snd_lump_num = -1;
static int snd_handle = -1;

// ---------------------------------------------------- interrupt side ---

static void speaker_tone(unsigned int divisor)
{
    uint8_t b;

    if (divisor == spk_divisor)
        return;
    b = armdos_inb(0x61);
    if (divisor == 0)
    {
        armdos_outb(0x61, b & ~3);
    }
    else
    {
        armdos_outb(0x43, 0xB6);          // channel 2, lo/hi, mode 3
        armdos_outb(0x42, divisor & 0xFF);
        armdos_outb(0x42, divisor >> 8);
        armdos_outb(0x61, b | 3);         // gate + speaker data
    }
    spk_divisor = divisor;
}

void I_ArmdosSpeakerTick(void)
{
    unsigned int divisor = 0;

    if (snd_remaining > 0)
    {
        uint8_t tone = *snd_pos++;

        snd_remaining--;
        if (tone < arrlen(divisors))
            divisor = divisors[tone];
    }
    speaker_tone(divisor);
}

void I_ArmdosSpeakerOff(void)
{
    armdos_disable();
    snd_remaining = 0;
    speaker_tone(0);
    armdos_enable();
}

// -------------------------------------------------------- DOOM's side ---

// DOOM never played these through the speaker (see chocolate-doom).
static boolean IsDisabledSound(sfxinfo_t *sfxinfo)
{
    static const char *const disabled[] = {
        "posact", "bgact", "dmact", "dmpain", "popain", "sawidl",
    };
    unsigned int i;

    for (i = 0; i < arrlen(disabled); ++i)
        if (!strcmp(sfxinfo->name, disabled[i]))
            return true;
    return false;
}

static int PCS_StartSound(sfxinfo_t *sfxinfo, int channel, int vol, int sep)
{
    uint8_t *lump;
    int lumplen, count;

    if (!pcs_initialized || IsDisabledSound(sfxinfo) || sfxinfo->lumpnum < 0)
        return -1;

    lump = W_CacheLumpNum(sfxinfo->lumpnum, PU_STATIC);
    lumplen = W_LumpLength(sfxinfo->lumpnum);
    count = lump[2] | (lump[3] << 8);
    if (lump[0] != 0 || lump[1] != 0 || count > lumplen - 4)
    {
        if (sfxinfo->lumpnum != snd_lump_num)
            W_ReleaseLumpNum(sfxinfo->lumpnum);
        return -1;
    }

    // One sound at a time, the newest wins (as DMX's PC speaker driver).
    armdos_disable();
    snd_pos = lump + 4;
    snd_remaining = count;
    armdos_enable();

    // Hand the previous lump back to the cache (unless it is this one, which
    // must stay PU_STATIC while the interrupt plays it).
    if (snd_lump != NULL && snd_lump_num != sfxinfo->lumpnum)
        W_ReleaseLumpNum(snd_lump_num);
    snd_lump = lump;
    snd_lump_num = sfxinfo->lumpnum;
    snd_handle = channel;

    return channel;
}

static void PCS_StopSound(int handle)
{
    if (pcs_initialized && handle == snd_handle)
    {
        armdos_disable();
        snd_remaining = 0;
        armdos_enable();
    }
}

static boolean PCS_SoundIsPlaying(int handle)
{
    return pcs_initialized && handle == snd_handle && snd_remaining > 0;
}

static int PCS_GetSfxLumpNum(sfxinfo_t *sfx)
{
    char namebuf[9];

    if (use_prefix)
        M_snprintf(namebuf, sizeof(namebuf), "dp%s", DEH_String(sfx->name));
    else
        M_StringCopy(namebuf, DEH_String(sfx->name), sizeof(namebuf));
    return W_CheckNumForName(namebuf);
}

static boolean PCS_Init(boolean use_sfx_prefix)
{
    use_prefix = use_sfx_prefix;
    pcs_initialized = true;
    return true;
}

static void PCS_Shutdown(void)
{
    if (pcs_initialized)
        I_ArmdosSpeakerOff();
    pcs_initialized = false;
}

static void PCS_Update(void) { }
static void PCS_UpdateSoundParams(int channel, int vol, int sep) { }

static snddevice_t pcs_devices[] = { SNDDEVICE_PCSPEAKER };

sound_module_t DG_sound_module =
{
    pcs_devices,
    arrlen(pcs_devices),
    PCS_Init,
    PCS_Shutdown,
    PCS_GetSfxLumpNum,
    PCS_Update,
    PCS_UpdateSoundParams,
    PCS_StartSound,
    PCS_StopSound,
    PCS_SoundIsPlaying,
    NULL,
};

// ------------------------------------------------------------- music ---

static boolean MUS_Init(void) { return false; }
static void MUS_Shutdown(void) { }
static void MUS_SetVolume(int volume) { }
static void MUS_Pause(void) { }
static void MUS_Resume(void) { }
static void *MUS_Register(void *data, int len) { return NULL; }
static void MUS_Unregister(void *handle) { }
static void MUS_Play(void *handle, boolean looping) { }
static void MUS_Stop(void) { }
static boolean MUS_IsPlaying(void) { return false; }

static snddevice_t mus_devices[] = { SNDDEVICE_NONE };

music_module_t DG_music_module =
{
    mus_devices,
    arrlen(mus_devices),
    MUS_Init,
    MUS_Shutdown,
    MUS_SetVolume,
    MUS_Pause,
    MUS_Resume,
    MUS_Register,
    MUS_Unregister,
    MUS_Play,
    MUS_Stop,
    MUS_IsPlaying,
    NULL,
};

// Referenced by i_sound.c's config bindings (i_sdlsound.c defines them
// upstream); unused here.
int use_libsamplerate = 0;
float libsamplerate_scale = 0.65f;
