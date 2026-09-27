/* SBMIX.EXE - the Sound Blaster 16 mixer from the command line (ARM-DOS).
 *
 *   SBMIX                         show the levels
 *   SBMIX /INIT                   the standard levels (what AUTOEXEC.BAT does at boot)
 *   SBMIX MASTER=28 FM=24 ...     set levels, 0-31 (31 = 0 dB, 2 dB steps)
 *   SBMIX /Q ...                  quietly
 *
 * The CT1745 mixer powers up with master, voice and FM at 24 (-14 dB); on a
 * real PC the card's setup utility (DIAGNOSE / MIXERSET in AUTOEXEC.BAT) set
 * them to the owner's levels at every boot. SBMIX /INIT is that step here.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sb.h>

static const struct { const char *name; unsigned reg; int init; } ctl[] = {
    { "MASTER",  0x30, 31 },
    { "VOICE",   0x32, 31 },
    { "FM",      0x34, 31 },
    { "CD",      0x36, 24 },
    { "LINE",    0x38, 24 },
};
#define NCTL (sizeof ctl / sizeof ctl[0])

static void set(unsigned i, int v)
{
    if (v < 0) v = 0;
    if (v > 31) v = 31;
    sb_mixer(ctl[i].reg, v << 3);
    sb_mixer(ctl[i].reg + 1, v << 3);
}

static void show(void)
{
    unsigned i;
    int k;
    printf("%s mixer at %Xh (DSP %u.%02u)\n\n", sb_name(), sb_info.port, sb_info.dsp_major, sb_info.dsp_minor);
    for (i = 0; i < NCTL; i++) {
        int l = sb_mixer(ctl[i].reg, -1) >> 3, r = sb_mixer(ctl[i].reg + 1, -1) >> 3;
        printf("  %-7s ", ctl[i].name);
        for (k = 0; k < 31; k++) putchar(k < (l + r) / 2 ? 0xDB : 0xB0);
        if (l != r) printf("  %2d/%-2d\n", l, r);
        else if (l) printf("  %2d  %3d dB\n", l, (l - 31) * 2);
        else printf("  %2d     off\n", l);
    }
}

int main(int argc, char **argv)
{
    int quiet = 0, init = 0, bad = 0, i;
    unsigned c;

    for (i = 1; i < argc; i++) {
        if (!strcasecmp(argv[i], "/Q")) quiet = 1;
        else if (!strcasecmp(argv[i], "/INIT")) init = 1;
        else if (!strcmp(argv[i], "/?")) {
            printf("Sets the Sound Blaster 16 mixer levels.\n\n"
                   "SBMIX [/INIT] [/Q] [MASTER=n] [VOICE=n] [FM=n] [CD=n] [LINE=n]\n\n"
                   "  /INIT   Standard levels: master, voice and FM 31 (0 dB), CD and line 24.\n"
                   "  /Q      Quiet: no messages.\n"
                   "  n       0 (off) to 31 (0 dB), in 2 dB steps.\n\n"
                   "With no options, SBMIX shows the levels.\n");
            return 0;
        }
    }
    if (!sb_detect(NULL)) {
        if (!quiet) printf("Sound Blaster not found (BLASTER=%s)\n", getenv("BLASTER") ? getenv("BLASTER") : "not set");
        return 1;
    }
    if (sb_info.dsp_major < 4) {
        if (!quiet) printf("%s: no SB16 mixer\n", sb_name());
        return 1;
    }
    if (init)
        for (c = 0; c < NCTL; c++) set(c, ctl[c].init);
    for (i = 1; i < argc; i++) {
        const char *eq = strchr(argv[i], '=');
        if (argv[i][0] == '/') continue;
        for (c = 0; c < NCTL; c++)
            if (eq && (size_t)(eq - argv[i]) == strlen(ctl[c].name) && !strncasecmp(argv[i], ctl[c].name, eq - argv[i]))
                break;
        if (c == NCTL || !isdigit((unsigned char)eq[1])) { if (!quiet) printf("Invalid parameter - %s\n", argv[i]); bad = 1; continue; }
        set(c, atoi(eq + 1));
    }
    if (!quiet) show();
    return bad ? 1 : 0;
}
