//
// Copyright(C) 1993-1996 Id Software, Inc.
// Copyright(C) 2005-2014 Simon Howard
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:  none
//

#include <stdio.h>
#include <stdlib.h>

#if defined(FEATURE_SOUND) && !defined(__DJGPP__) && !defined(ARMDOS)
#include <SDL_mixer.h>
#endif

#include "config.h"
#include "doomfeatures.h"
#include "doomtype.h"

#ifdef ORIGCODE
#include "gusconf.h"
#endif
#include "i_sound.h"
#ifdef ARMDOS
#include <stdlib.h>
#include <sb.h>
#include <midi.h>
#include <string.h>
#include "i_armdos.h"
#endif
#include "i_video.h"
#include "m_argv.h"
#include "m_config.h"

// Sound sample rate to use for digital output (Hz)

int snd_samplerate = 44100;

// Maximum number of bytes to dedicate to allocated sound effects.
// (Default: 64MB)

int snd_cachesize = 64 * 1024 * 1024;

// Config variable that controls the sound buffer size.
// We default to 28ms (1000 / 35fps = 1 buffer per tic).

int snd_maxslicetime_ms = 28;

// External command to invoke to play back music.

char *snd_musiccmd = "";

// Low-level sound and music modules we are using

static sound_module_t *sound_module = NULL;
static music_module_t *music_module = NULL;

#ifdef ARMDOS
// ARM-DOS: chosen in I_InitSound - the Sound Blaster 16 (sound effects) and
// its OPL3 (music) when BLASTER is set and the card answers, else the PC
// speaker and no music.
int snd_musicdevice = SNDDEVICE_NONE;
int snd_sfxdevice = SNDDEVICE_PCSPEAKER;
#else
int snd_musicdevice = SNDDEVICE_SB;
int snd_sfxdevice = SNDDEVICE_SB;
#endif

// DOS-specific options: These are unused but should be maintained
// so that the config file can be shared between chocolate
// doom and doom.exe

static int snd_sbport = 0;
static int snd_sbirq = 0;
static int snd_sbdma = 0;
static int snd_mport = 0;

// Compiled-in sound modules:

static sound_module_t *sound_modules[] = 
{
    #ifdef FEATURE_SOUND
#ifdef ARMDOS
    &I_ArmdosSbSoundModule,
#endif
    &DG_sound_module,
    #endif
    NULL,
};

// Check if a sound device is in the given list of devices

static boolean SndDeviceInList(snddevice_t device, snddevice_t *list,
                               int len)
{
    int i;

    for (i=0; i<len; ++i)
    {
        if (device == list[i])
        {
            return true;
        }
    }

    return false;
}

// Find and initialize a sound_module_t appropriate for the setting
// in snd_sfxdevice.

static void InitSfxModule(boolean use_sfx_prefix)
{
    int i;

    sound_module = NULL;

    for (i=0; sound_modules[i] != NULL; ++i)
    {
        // Is the sfx device in the list of devices supported by
        // this module?

        if (SndDeviceInList(snd_sfxdevice, 
                            sound_modules[i]->sound_devices,
                            sound_modules[i]->num_sound_devices))
        {
            // Initialize the module

            if (sound_modules[i]->Init(use_sfx_prefix))
            {
                sound_module = sound_modules[i];
                return;
            }
        }
    }
}

// Initialize music according to snd_musicdevice.

static void InitMusicModule(void)
{
#ifdef FEATURE_SOUND
#ifdef ARMDOS
    if (snd_musicdevice == SNDDEVICE_GENMIDI)
    {
        music_module = &I_ArmdosMpuMusicModule;
        return;
    }
    if (snd_musicdevice == SNDDEVICE_SB || snd_musicdevice == SNDDEVICE_ADLIB)
    {
        music_module = &I_ArmdosOplMusicModule;
        return;
    }
#endif
    music_module = &DG_music_module;
#endif /* FEATURE_SOUND */
}

//
// Initializes sound stuff, including volume
// Sets channels, SFX and music volume,
//  allocates channel buffer, sets S_sfx lookup.
//

void I_InitSound(boolean use_sfx_prefix)
{  
    boolean nosound, nosfx, nomusic;

    //!
    // @vanilla
    //
    // Disable all sound output.
    //

    nosound = M_CheckParm("-nosound") > 0;

    //!
    // @vanilla
    //
    // Disable sound effects. 
    //

    nosfx = M_CheckParm("-nosfx") > 0;

    //!
    // @vanilla
    //
    // Disable music.
    //

    nomusic = M_CheckParm("-nomusic") > 0;

#ifdef ARMDOS
    // What DMX's SETUP would have chosen: a Sound Blaster when BLASTER says
    // there is one (and it answers), else the PC speaker.
    if (getenv("BLASTER") != NULL && sb_detect(NULL))
    {
        snd_sfxdevice = SNDDEVICE_SB;
        snd_musicdevice = SNDDEVICE_SB;
        sb_set_volume(31, 31, 31);      // the mixer at 0 dB; DOOM's own sliders set the levels
        printf("I_InitSound: %s at %Xh, IRQ %u, DMA %u/%u, DSP %u.%02u\n",
               sb_name(), sb_info.port, sb_info.irq, sb_info.dma8,
               sb_info.dma16, sb_info.dsp_major, sb_info.dsp_minor);
    }
    else
    {
        snd_sfxdevice = SNDDEVICE_PCSPEAKER;
        snd_musicdevice = SNDDEVICE_NONE;
        printf("I_InitSound: PC speaker\n");
        // BLASTER set but no DSP answers - an AdLib alone: DMX played the music on its OPL,
        // the effects on the speaker (without BLASTER DOOM stays silent as before)
        if (getenv("BLASTER") != NULL && opl_detect())
        {
            snd_musicdevice = SNDDEVICE_ADLIB;
            printf("I_InitMusic: AdLib (FM) at 388h\n");
        }
    }
    // Music: General MIDI when an MPU-401 answers (BLASTER's P, else 330h) -
    // DMX's "General MIDI" device - unless DMXOPTION says -fm (the OPL3, as
    // before); the OPL3 when there is only the Sound Blaster.
    {
        const char *opt = getenv("DMXOPTION");
        if ((opt == NULL || strstr(opt, "-fm") == NULL) && mpu_detect(NULL))
        {
            snd_musicdevice = SNDDEVICE_GENMIDI;
            printf("I_InitMusic: General MIDI, MPU-401 at %Xh\n", mpu_info.port);
        }
    }
#endif

    // Initialize the sound and music subsystems.

    if (!nosound && !screensaver_mode)
    {
        // This is kind of a hack. If native MIDI is enabled, set up
        // the TIMIDITY_CFG environment variable here before SDL_mixer
        // is opened.

        if (!nomusic
         && (snd_musicdevice == SNDDEVICE_GENMIDI
          || snd_musicdevice == SNDDEVICE_GUS))
        {
            //I_InitTimidityConfig();
        }

        if (!nosfx)
        {
            InitSfxModule(use_sfx_prefix);
        }

        if (!nomusic)
        {
            InitMusicModule();
        }
    }

}

void I_ShutdownSound(void)
{
    if (sound_module != NULL)
    {
        sound_module->Shutdown();
    }

    if (music_module != NULL)
    {
        music_module->Shutdown();
    }
}

int I_GetSfxLumpNum(sfxinfo_t *sfxinfo)
{
    if (sound_module != NULL) 
    {
        return sound_module->GetSfxLumpNum(sfxinfo);
    }
    else
    {
        return 0;
    }
}

void I_UpdateSound(void)
{
    if (sound_module != NULL)
    {
        sound_module->Update();
    }

    if (music_module != NULL && music_module->Poll != NULL)
    {
        music_module->Poll();
    }
}

static void CheckVolumeSeparation(int *vol, int *sep)
{
    if (*sep < 0)
    {
        *sep = 0;
    }
    else if (*sep > 254)
    {
        *sep = 254;
    }

    if (*vol < 0)
    {
        *vol = 0;
    }
    else if (*vol > 127)
    {
        *vol = 127;
    }
}

void I_UpdateSoundParams(int channel, int vol, int sep)
{
    if (sound_module != NULL)
    {
        CheckVolumeSeparation(&vol, &sep);
        sound_module->UpdateSoundParams(channel, vol, sep);
    }
}

int I_StartSound(sfxinfo_t *sfxinfo, int channel, int vol, int sep)
{
    if (sound_module != NULL)
    {
        CheckVolumeSeparation(&vol, &sep);
        return sound_module->StartSound(sfxinfo, channel, vol, sep);
    }
    else
    {
        return 0;
    }
}

void I_StopSound(int channel)
{
    if (sound_module != NULL)
    {
        sound_module->StopSound(channel);
    }
}

boolean I_SoundIsPlaying(int channel)
{
    if (sound_module != NULL)
    {
        return sound_module->SoundIsPlaying(channel);
    }
    else
    {
        return false;
    }
}

void I_PrecacheSounds(sfxinfo_t *sounds, int num_sounds)
{
    if (sound_module != NULL && sound_module->CacheSounds != NULL)
    {
	sound_module->CacheSounds(sounds, num_sounds);
    }
}

void I_InitMusic(void)
{
    if(music_module != NULL)
    {
#ifdef ARMDOS
        if (!music_module->Init())
        {
            music_module = NULL;
        }
#else
        music_module->Init();
#endif
    }
}

void I_ShutdownMusic(void)
{

}

void I_SetMusicVolume(int volume)
{
    if (music_module != NULL)
    {
        music_module->SetMusicVolume(volume);
    }
}

void I_PauseSong(void)
{
    if (music_module != NULL)
    {
        music_module->PauseMusic();
    }
}

void I_ResumeSong(void)
{
    if (music_module != NULL)
    {
        music_module->ResumeMusic();
    }
}

void *I_RegisterSong(void *data, int len)
{
    if (music_module != NULL)
    {
        return music_module->RegisterSong(data, len);
    }
    else
    {
        return NULL;
    }
}

void I_UnRegisterSong(void *handle)
{
    if (music_module != NULL)
    {
        music_module->UnRegisterSong(handle);
    }
}

void I_PlaySong(void *handle, boolean looping)
{
    if (music_module != NULL)
    {
        music_module->PlaySong(handle, looping);
    }
}

void I_StopSong(void)
{
    if (music_module != NULL)
    {
        music_module->StopSong();
    }
}

boolean I_MusicIsPlaying(void)
{
    if (music_module != NULL)
    {
        return music_module->MusicIsPlaying();
    }
    else
    {
        return false;
    }
    
}

void I_BindSoundVariables(void)
{
    extern int use_libsamplerate;
    extern float libsamplerate_scale;

    M_BindVariable("snd_musicdevice",   &snd_musicdevice);
    M_BindVariable("snd_sfxdevice",     &snd_sfxdevice);
    M_BindVariable("snd_sbport",        &snd_sbport);
    M_BindVariable("snd_sbirq",         &snd_sbirq);
    M_BindVariable("snd_sbdma",         &snd_sbdma);
    M_BindVariable("snd_mport",         &snd_mport);
    M_BindVariable("snd_maxslicetime_ms", &snd_maxslicetime_ms);
    M_BindVariable("snd_musiccmd",      &snd_musiccmd);
    M_BindVariable("snd_samplerate",    &snd_samplerate);
    M_BindVariable("snd_cachesize",     &snd_cachesize);

#ifdef FEATURE_SOUND
    M_BindVariable("use_libsamplerate",   &use_libsamplerate);
    M_BindVariable("libsamplerate_scale", &libsamplerate_scale);
#endif

    // Before SDL_mixer version 1.2.11, MIDI music caused the game
    // to crash when it looped.  If this is an old SDL_mixer version,
    // disable MIDI.
}

