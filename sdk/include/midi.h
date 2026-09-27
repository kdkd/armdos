/* midi.h - the MPU-401 MIDI interface and Standard MIDI Files for ARM-DOS programs.
 *
 * The ARM-PC's General MIDI synthesizer (a Sound Canvas-style SoundFont synth,
 * emu/dev/gmsynth.mjs) sits behind a Roland MPU-401 at 330h, IRQ 9 (ARCH.md
 * 4.3), and AUTOEXEC.BAT says so with the P field of BLASTER:
 *     SET BLASTER=A220 I7 D1 H5 P330 T6
 *
 *   if (mpu_detect(NULL) && mpu_uart()) {        reset (FFh -> ACK FEh), then UART mode (3Fh)
 *       mpu_msg(0xC0, 19, 0);                    program change: church organ on channel 1
 *       mpu_msg(0x90, 60, 100);                  note on
 *       ...
 *       mpu_msg(0x80, 60, 0);
 *   }
 *
 *   smf_file song;                               Standard MIDI Files (format 0 and 1):
 *   if (smf_load(&song, "C:\\MIDI\\BACH846.MID") == 0)
 *       mpu_play_smf(&song, NULL, NULL);         plays it (blocking), Esc stops
 *
 * Everything is safe without an MPU: mpu_detect() returns 0, the rest does
 * nothing. mpu_uart() registers an exit handler that silences all channels
 * and resets the MPU (back to intelligent mode), so a program that exits in
 * the middle of a song leaves no hanging notes.
 * (The OPL "General MIDI" helper midi_note_on()/... is in sb.h; this is the
 * real thing: the bytes go to a MIDI synthesizer.)
 */
#ifndef ARMDOS_MIDI_H
#define ARMDOS_MIDI_H

#ifdef __cplusplus
extern "C" {
#endif

/* ---- the MPU-401 ----------------------------------------------------------- */
typedef struct {
    unsigned port;          /* data port (330h); status/command = port + 1   */
    unsigned irq;           /* 9 on the ARM-PC (INT 71h)                      */
    int      present;       /* answered the reset                             */
    int      uart;          /* in UART mode                                   */
} mpu_card;

extern mpu_card mpu_info;

/* Port from BLASTER's P field (330h if BLASTER has none), reset the MPU and
 * wait for its ACK. Returns 1 if it answered. `card` may be NULL. */
int  mpu_detect(mpu_card *card);
/* Reset (command FFh). Returns 1 on the ACK (a UART-mode MPU leaves UART mode
 * without one; mpu_reset retries once, as programs of the era did). */
int  mpu_reset(void);
/* Enter UART mode (command 3Fh, ACK). Returns 1 on success. */
int  mpu_uart(void);
/* One raw MIDI byte / a buffer of them (waits while the MPU is not ready). */
void mpu_write(unsigned char b);
void mpu_send(const void *bytes, unsigned n);
/* A channel message: status (80h-EFh, the channel in the low nibble), data;
 * sends 2 or 3 bytes as the status needs. */
void mpu_msg(unsigned status, unsigned d1, unsigned d2);
/* All notes off, sustain off and reset controllers on all 16 channels. */
void mpu_all_notes_off(void);
/* Silence, reset, back to intelligent mode (also done at exit). */
void mpu_shutdown(void);

/* ---- a millisecond clock ---------------------------------------------------- *
 * PIT channel 0 at 1000 Hz with an INT 08h handler that keeps the BIOS tick
 * (18.2 Hz) running; restored by midi_timer_stop() and at exit. While it runs,
 * armdos_halt() (WFI) wakes at least every millisecond. */
int           midi_timer_start(void);
void          midi_timer_stop(void);
unsigned long midi_timer_us(void);        /* microseconds since midi_timer_start */

/* ---- Standard MIDI Files ----------------------------------------------------- */
#define SMF_MAX_TRACKS 64

typedef struct {
    unsigned long time_us;      /* absolute time of the event (tempo map applied) */
    unsigned long tick;         /* absolute tick                                   */
    unsigned char status;       /* 80h-EFh channel message; F0h/F7h SysEx; FFh meta */
    unsigned char d1, d2;       /* channel message data; meta: d1 = meta type      */
    unsigned char track;
    const unsigned char *data;  /* SysEx (after the F0) / meta payload             */
    unsigned long len;
} smf_event;

typedef struct {
    const unsigned char *pos, *end;
    unsigned long next_tick;
    unsigned char running;
    unsigned char done;
} smf_track;

typedef struct {
    unsigned char *mem;         /* the file (smf_load: malloc'd)                  */
    unsigned long size;
    unsigned format, ntracks, division;   /* division: ticks per quarter (or SMPTE) */
    smf_track tracks[SMF_MAX_TRACKS];
    unsigned long tempo;        /* current microseconds per quarter note          */
    unsigned long cur_tick;     /* tick/time of the last tempo reference          */
    unsigned long long cur_us;
    unsigned long ref_tick;
    unsigned long length_us;    /* whole song (from a scan at load)               */
    unsigned long length_ticks;
    unsigned long first_tempo;  /* tempo at tick 0                                */
    unsigned timesig_num, timesig_den;
    char title[64];             /* first track name / text meta event             */
    char copyright[80];
    unsigned long chunk_off;    /* where the track chunks start                   */
    int  owned;
} smf_file;

/* Load a file (0 = ok, -1 = can't open, -2 = not a MIDI file, -3 = no memory). */
int  smf_load(smf_file *f, const char *path);
/* Use a file already in memory (not copied, must stay valid). */
int  smf_open_mem(smf_file *f, const void *data, unsigned long len);
void smf_free(smf_file *f);
/* Back to the start. */
void smf_rewind(smf_file *f);
/* The next event in time order (tracks merged). Returns 0 at the end. */
int  smf_next(smf_file *f, smf_event *ev);
/* Send one event to the MPU (channel messages and SysEx; meta events are not sent). */
void mpu_send_event(const smf_event *ev);

/* Play a whole file on the MPU (blocking): GM System On, then every event at
 * its time, sleeping in WFI in between. poll() (may be NULL) is called about
 * every millisecond with the song position; a nonzero return stops the song
 * (with NULL, Esc stops it). Returns 0 at the end, 1 if stopped, -1 without MPU. */
typedef int (*smf_poll_fn)(unsigned long now_us, void *user);
int  mpu_play_smf(smf_file *f, smf_poll_fn poll, void *user);

#ifdef __cplusplus
}
#endif
#endif
