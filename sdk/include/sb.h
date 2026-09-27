/* sb.h - Sound Blaster 16 and OPL3 (AdLib) sound for ARM-DOS programs.
 *
 * The ARM-PC's standard sound card is a Sound Blaster 16 at 220h, IRQ 7,
 * 8-bit DMA 1, 16-bit DMA 5, with an OPL3 FM chip at 388h (ARCH.md 4.3), and
 * AUTOEXEC.BAT says so:  SET BLASTER=A220 I7 D1 H5 T6
 *
 *   sb_card sb;
 *   if (sb_detect(&sb))                              parses BLASTER, resets the DSP
 *       printf("%s at %Xh, IRQ %u\n", sb_name(), sb.port, sb.irq);
 *   sb_play_pcm(chime, sizeof chime, 11025, SB_8BIT);  plays in the background
 *   sb_start(22050, SB_16BIT | SB_STEREO | SB_SIGNED, 4096, my_mixer, NULL);
 *                                                    double-buffered stream: my_mixer()
 *                                                    fills each 4 KB half from the SB IRQ
 *   sb_stop();
 *
 *   if (opl_detect()) { midi_init(NULL); midi_program(0, 0); midi_note_on(0, 60, 100); }
 *
 * Everything is safe to call when there is no card: sb_detect()/opl_detect()
 * return 0 and the other calls do nothing. The IRQ handler is installed with
 * INT 21h AH=25h on INT 08h+irq (IRQ 8-15: INT 70h+irq-8), sends the EOI,
 * chains interrupts that are not the card's, and is removed by sb_stop() or
 * automatically at exit. Buffers come from malloc() (the heap may be in XMS:
 * all ARM-PC RAM is below 16 MB, so ISA DMA reaches it).
 */
#ifndef ARMDOS_SB_H
#define ARMDOS_SB_H

#ifdef __cplusplus
extern "C" {
#endif

/* ---- the card ------------------------------------------------------------- */
typedef struct {
    unsigned port;          /* A: base port (220h)                         */
    unsigned irq;           /* I: IRQ (7)                                  */
    unsigned dma8;          /* D: 8-bit DMA channel (1)                    */
    unsigned dma16;         /* H: 16-bit DMA channel (5); = dma8 if none   */
    unsigned type;          /* T: 6 = SB16, 4 = SB Pro 2, 3 = SB 2.0, 1 = SB */
    unsigned dsp_major;     /* DSP version, e.g. 4.05                      */
    unsigned dsp_minor;
    int      present;       /* the DSP answered                            */
} sb_card;

extern sb_card sb_info;     /* what sb_detect() found */

/* Parse BLASTER (defaults A220 I7 D1 H5 T6 when it is not set), reset the DSP
 * and read its version. Returns 1 if a card answered. `card` may be NULL. */
int  sb_detect(sb_card *card);
/* "Sound Blaster 16", "Sound Blaster Pro", ... from the DSP version. */
const char *sb_name(void);
/* Reset the DSP (returns 1 on the 0AAh answer). */
int  sb_reset_dsp(void);
/* Raw DSP access (with busy-waits and timeouts); sb_dsp_read returns -1 on timeout. */
void sb_dsp_write(unsigned char v);
int  sb_dsp_read(void);
/* Speaker (DAC output) on/off - needed by pre-SB16 cards, harmless on an SB16. */
void sb_speaker(int on);
/* Mixer volumes 0-31 (2 dB steps, 31 = 0 dB), -1 = leave unchanged. */
void sb_set_volume(int master, int voice, int fm);
/* Raw mixer register access: writes `value` if >= 0, returns the register. */
unsigned sb_mixer(unsigned reg, int value);

/* ---- digitised sound ----------------------------------------------------- */
#define SB_8BIT    0x00     /* 8-bit samples (unsigned unless SB_SIGNED)      */
#define SB_16BIT   0x01     /* 16-bit samples (give SB_SIGNED for normal PCM) */
#define SB_STEREO  0x02     /* interleaved left, right                        */
#define SB_SIGNED  0x04

/* Fill `bytes` bytes of the buffer half that just finished playing. Called
 * from the SB's IRQ handler (SVC mode, IRQs disabled): keep it short, no DOS
 * calls. */
typedef void (*sb_fill_fn)(void *buf, unsigned bytes, void *user);

/* Start a double-buffered auto-init DMA stream: `block` bytes per half (the
 * IRQ comes every block), both halves are filled before it starts. Returns 1
 * on success. Stops whatever was playing. */
int  sb_start(unsigned rate, unsigned format, unsigned block, sb_fill_fn fill, void *user);
/* Change the sample rate of the running stream (SB16: immediately). */
void sb_set_rate(unsigned rate);
/* Stop the stream / sound, reset the DSP, remove the IRQ handler. */
void sb_stop(void);
/* Play a sound in the background (copied block by block from `data`, which
 * must stay valid until it has played). Returns 1 if it started. */
int  sb_play_pcm(const void *data, unsigned long bytes, unsigned rate, unsigned format);
/* 1 while a stream or an sb_play_pcm() sound is playing. */
int  sb_busy(void);
/* IRQs taken so far (blocks played). */
extern volatile unsigned long sb_irq_count;

/* ---- OPL2/OPL3 FM -------------------------------------------------------- */
/* Look for an AdLib-compatible chip at 388h (timer 1 overflow test).
 * Returns 0 = none, 2 = OPL2, 3 = OPL3. */
int  opl_detect(void);
/* Write a register: 000h-0FFh first array, 100h-1FFh second (OPL3). */
void opl_write(unsigned reg, unsigned val);
/* Silence and clear all registers (both arrays on an OPL3; OPL3 mode off).
 * Sounding notes are released at the fastest rate first: takes ~4 ms. */
void opl_reset(void);

/* ---- a tiny General MIDI on the OPL -------------------------------------- *
 * Instruments come from a GENMIDI bank (the DMX format of DOOM's GENMIDI lump:
 * "#OPL_II#", 128 melodic + 47 percussion instruments, 36 bytes each). NULL
 * selects the built-in bank: Freedoom's GENMIDI (BSD licence, see sdk/README).
 * Channel 9 is percussion (notes 35-81). On an OPL3 the helper uses all 18
 * voices and stereo panning; on an OPL2, 9 voices. */
int  midi_init(const void *genmidi);        /* returns the voice count, 0 = no OPL */
void midi_shutdown(void);
void midi_program(int ch, int program);
void midi_note_on(int ch, int note, int velocity);   /* velocity 0 = note off */
void midi_note_off(int ch, int note);
void midi_control(int ch, int controller, int value);  /* 7 volume, 10 pan, 11 expression,
                                                          120/123 all off, 121 reset */
void midi_pitch_bend(int ch, int value);    /* 0-16383, 8192 = centre, +-2 semitones */
void midi_all_off(void);
void midi_set_volume(int volume);           /* master music volume 0-127 */

#ifdef __cplusplus
}
#endif
#endif
