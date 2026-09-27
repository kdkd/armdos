/* tests/ttshost.c - the speech engine on the host: text -> 8-bit 11025 Hz WAV.
 *   ttshost [-p pitch] [-s speed] [-t tone] [-v vol] [-spell] [-nodict] [-ph] out.wav "text" */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../tts.h"

int main(int argc, char **argv)
{
    int p = 5, s = 5, t = 5, v = 5, i = 1, phon = 0;
    static unsigned char buf[11025 * 120];
    long n = 0; int k;
    FILE *f;
    for (; i < argc && argv[i][0] == '-'; i++) {
        if (!strcmp(argv[i], "-p")) p = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-s")) s = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-t")) t = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-v")) v = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-spell")) tts_spell_mode = 1;
        else if (!strcmp(argv[i], "-nodict")) tts_no_dict = 1;
        else if (!strcmp(argv[i], "-ph")) phon = 1;
    }
    if (argc - i < 2) { fprintf(stderr, "usage\n"); return 2; }
    tts_settings(p, s, t, v);
    if (phon) { char o[4000]; char nm[2000]; tts_normalise(argv[i + 1], nm, sizeof nm); tts_text_phonemes(argv[i + 1], o, sizeof o); printf("%s\n%s\n", nm, o); }
    tts_begin(argv[i + 1]);
#ifdef TTS_DEBUG
    { void tts_dump_segments(void); if (phon) tts_dump_segments(); }
#endif
    while ((k = tts_generate(buf + n, 4096)) > 0 && n < (long)sizeof buf - 4096) n += k;
    f = fopen(argv[i], "wb");
    {
        unsigned char h[44]; unsigned r = 11025, d = (unsigned)n;
        memcpy(h, "RIFF", 4); *(unsigned *)(h + 4) = 36 + d; memcpy(h + 8, "WAVEfmt ", 8);
        *(unsigned *)(h + 16) = 16; *(unsigned short *)(h + 20) = 1; *(unsigned short *)(h + 22) = 1;
        *(unsigned *)(h + 24) = r; *(unsigned *)(h + 28) = r; *(unsigned short *)(h + 32) = 1; *(unsigned short *)(h + 34) = 8;
        memcpy(h + 36, "data", 4); *(unsigned *)(h + 40) = d;
        fwrite(h, 1, 44, f); fwrite(buf, 1, n, f); fclose(f);
    }
    for (k = 0; k < tts_word_count(); k++) if (phon) printf("word %d at %.3f s (src %d)\n", k, tts_word_sample(k) / 11025.0, tts_word_offset(k));
    fprintf(stderr, "%.2f s\n", n / 11025.0);
    return 0;
}
