//
// i_armdos.h - hooks between the doomgeneric sources and the ARM-DOS
// platform layer (doomgeneric_armdos.c, i_pcsound_armdos.c).
//

#ifndef I_ARMDOS_H
#define I_ARMDOS_H

struct color;

// Video: set mode 13h (from I_InitGraphics), program the DAC.
void I_ArmdosSetGraphicsMode(void);
void I_ArmdosSetPalette(const struct color *colors);

// Restore the timer, keyboard and text mode. Idempotent; called on quit,
// on I_Error and from atexit().
void I_ArmdosShutdown(void);

// Mouse through the INT 33h driver, as DOOM.EXE: detect (from I_InitGraphics,
// while the text screen is up) and read once per tic (I_StartTic).
void I_ArmdosStartupMouse(void);
void I_ArmdosReadMouse(void);

// Back to text mode and show the ENDOOM screen.
void I_ArmdosEndoom(const unsigned char *endoom);

// PC speaker: called from the 140 Hz timer interrupt.
void I_ArmdosSpeakerTick(void);
void I_ArmdosSpeakerOff(void);

// OPL music (i_oplmus_armdos.c): one MUS tick, from the 140 Hz interrupt.
void I_ArmdosMusicTick(void);
// General MIDI music on the MPU-401 (i_mpumus_armdos.c): the same, for the MPU module.
void I_ArmdosMpuMusicTick(void);

// The Sound Blaster 16 sound effects and OPL music modules (i_sb_armdos.c,
// i_oplmus_armdos.c) are declared in i_sound.h; I_InitSound picks them when
// BLASTER is set.

#endif
