/* SBTEST.EXE - find the Sound Blaster and the OPL, play a PCM chime and an FM
 * chord (sdk/include/sb.h). Part of ARM-DOS; see README.md. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <armdos.h>
#include <sb.h>

#define RATE   22050
#define NOTE_S 0.55

/* wait n BIOS ticks (18.2 Hz), sleeping in WFI */
static void wait_ticks(unsigned n)
{
    uint32_t t0 = ARMDOS_BIOS_TICKS;
    while (ARMDOS_BIOS_TICKS - t0 < n) armdos_halt();
}

/* "ding-dong": two bell tones (fundamental + inharmonic partials, decaying) */
static int16_t *make_chime(unsigned *frames)
{
    static const double hz[2] = { 1318.51, 1046.50 };   /* E6, C6 */
    unsigned per = (unsigned)(RATE * NOTE_S), n = per * 2, i, k;
    int16_t *s = malloc(n * sizeof *s);
    if (!s) return NULL;
    for (k = 0; k < 2; k++)
        for (i = 0; i < per; i++) {
            double t = (double)i / RATE, env = exp(-t * 5.0) * (i < 40 ? i / 40.0 : 1.0);
            double v = sin(2 * M_PI * hz[k] * t) + 0.35 * sin(2 * M_PI * hz[k] * 2.76 * t) * exp(-t * 9.0);
            s[k * per + i] = (int16_t)(v * env * 12000);
        }
    *frames = n;
    return s;
}

int main(int argc, char **argv)
{
    sb_card sb;
    int opl;
    const char *blaster = getenv("BLASTER");
    (void)argc; (void)argv;

    printf("Sound Blaster test - ARM-DOS\n\n");
    if (!blaster) printf("BLASTER is not set: trying A220 I7 D1 H5 T6\n");
    if (sb_detect(&sb)) {
        printf("%s found at %Xh, IRQ %u, DMA %u/%u, DSP %u.%02u\n",
               sb_name(), sb.port, sb.irq, sb.dma8, sb.dma16, sb.dsp_major, sb.dsp_minor);
    } else {
        printf("No Sound Blaster found at %Xh.\n", sb.port);
    }
    opl = opl_detect();
    if (opl) printf("%s FM synthesizer found at 388h\n", opl == 3 ? "OPL3 (YMF262)" : "OPL2 (YM3812)");
    else printf("No AdLib-compatible FM synthesizer found at 388h.\n");

    if (sb.present) {
        unsigned frames;
        int16_t *chime = make_chime(&frames);
        unsigned long irqs0 = sb_irq_count;
        sb_set_volume(28, 31, -1);
        printf("Playing a PCM chime (16-bit, %u Hz, DMA %u)...\n", RATE, sb.dma16);
        if (chime && sb_play_pcm(chime, frames * 2UL, RATE, SB_16BIT | SB_SIGNED)) {
            while (sb_busy()) armdos_halt();
            wait_ticks(4);
            printf("  %lu blocks played\n", sb_irq_count - irqs0);
        } else printf("  could not start the sound\n");
        sb_stop();
        free(chime);
    }
    if (opl) {
        static const int chord[3] = { 60, 64, 67 };      /* C major */
        int i;
        if (sb.present) sb_set_volume(28, -1, 31);
        midi_init(NULL);
        midi_program(0, 19);                             /* Church Organ */
        printf("Playing an FM chord (C4 E4 G4, General MIDI program 20)...\n");
        for (i = 0; i < 3; i++) midi_note_on(0, chord[i], 110);
        wait_ticks(27);                                  /* ~1.5 s */
        for (i = 0; i < 3; i++) midi_note_off(0, chord[i]);
        wait_ticks(9);
        midi_shutdown();
    }
    printf("Done.\n");
    return sb.present && opl ? 0 : 1;
}
