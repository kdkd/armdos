/*
 * nocards.c - the Apogee Sound System's other sound cards, absent.
 *
 * The ARM-PC has one sound card, a Sound Blaster 16 (ARCH.md 4.3): no Pro
 * Audio Spectrum, Gravis UltraSound, Ensoniq SoundScape or Disney Sound
 * Source. MULTIVOC.C and FX_MAN.C are kept as Jim Dose wrote them, so their
 * calls for those cards land here and fail as "card not found" would.
 * GPL-2 or later, like the Apogee Sound System release.
 */
#include <stddef.h>
#include "pas16.h"
#include "guswave.h"
#include "sndscape.h"
#include "sndsrc.h"

static char notfound[] = "Sound card not found.";

unsigned int PAS_DMAChannel = -1;
int SOUNDSCAPE_DMAChannel = -1;

char *PAS_ErrorString( int e ) { return notfound; }
unsigned PAS_GetPlaybackRate( void ) { return 0; }
int  PAS_SetMixMode( int mode ) { return 0; }
void PAS_StopPlayback( void ) { }
int  PAS_GetCurrentPos( void ) { return -1; }
int  PAS_BeginBufferedPlayback( char *b, int s, int n, unsigned r, int m, void ( *cb )( void ) ) { return -1; }
int  PAS_BeginBufferedRecord( char *b, int s, int n, unsigned r, int m, void ( *cb )( void ) ) { return -1; }
int  PAS_SetPCMVolume( int volume ) { return -1; }
int  PAS_Init( void ) { return -1; }
void PAS_Shutdown( void ) { }

char *GUSWAVE_ErrorString( int e ) { return notfound; }
int  GUSWAVE_KillAllVoices( void ) { return 0; }
void GUSWAVE_SetVolume( int volume ) { }
int  GUSWAVE_StartDemandFeedPlayback( void ( *function )( char **ptr, unsigned long *length ),
        int channels, int bits, int rate, int pitchoffset, int angle,
        int volume, int priority, unsigned long callbackval ) { return -1; }
int  GUSWAVE_Init( int numvoices ) { return -1; }
void GUSWAVE_Shutdown( void ) { }

char *SOUNDSCAPE_ErrorString( int e ) { return notfound; }
unsigned SOUNDSCAPE_GetPlaybackRate( void ) { return 0; }
int  SOUNDSCAPE_SetMixMode( int mode ) { return 0; }
void SOUNDSCAPE_StopPlayback( void ) { }
int  SOUNDSCAPE_GetCurrentPos( void ) { return -1; }
int  SOUNDSCAPE_BeginBufferedPlayback( char *b, int s, int n, unsigned r, int m, void ( *cb )( void ) ) { return -1; }
int  SOUNDSCAPE_Init( void ) { return -1; }
void SOUNDSCAPE_Shutdown( void ) { }

char *SS_ErrorString( int e ) { return notfound; }
void SS_StopPlayback( void ) { }
int  SS_GetCurrentPos( void ) { return -1; }
int  SS_BeginBufferedPlayback( char *b, int s, int n, void ( *cb )( void ) ) { return -1; }
int  SS_GetPlaybackRate( void ) { return 0; }
int  SS_SetMixMode( int mode ) { return 0; }
int  SS_Init( int soundcard ) { return -1; }
void SS_Shutdown( void ) { }
