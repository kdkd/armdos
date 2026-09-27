/* TURBO.COM - the ARM PC's turbo switch from the command line (ARM-DOS).
 *
 *   TURBO              show the clock
 *   TURBO ON | OFF     the same as pressing the TURBO button
 *   TURBO MAX          no clock limit: as fast as the computer running the
 *                      emulator can go (remembers the setting it replaces)
 *   TURBO RESTORE      back to the setting TURBO MAX replaced
 *
 * The system board's port F2h: bit 0 = turbo, bit 1 = clock limiter off
 * (ARCH.md section 4). TURBO MAX keeps the old value in the BIOS data area's
 * intra-application communications area (0040:00F0h, 16 bytes set aside by
 * IBM for programs to pass notes to each other), so a batch file can bracket
 * one heavy program with TURBO MAX ... TURBO RESTORE. A reset also puts the
 * limiter back.
 */
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <armdos.h>

#define ICA ((volatile unsigned char *)0x4F0)

static void show(void)
{
    unsigned v = armdos_inb(0xF2), mhz = armdos_inb(0xF1);
    if (v & 2) printf("Turbo MAX: no clock limit, now %s%u MHz\n", mhz >= 255 ? "over " : "", mhz);
    else printf("Turbo %s: %u MHz\n", (v & 1) ? "on" : "off", mhz);
}

int main(int argc, char **argv)
{
    char a[16];
    unsigned i, v = armdos_inb(0xF2);

    if (argc < 2) { show(); return 0; }
    for (i = 0; argv[1][i] && i < sizeof a - 1; i++) a[i] = toupper((unsigned char)argv[1][i]);
    a[i] = 0;

    if (!strcmp(a, "ON")) armdos_outb(0xF2, 1);
    else if (!strcmp(a, "OFF")) armdos_outb(0xF2, 0);
    else if (!strcmp(a, "MAX")) {
        if (!(ICA[0] == 'T' && ICA[1] == 'B')) { ICA[0] = 'T'; ICA[1] = 'B'; ICA[2] = v; }
        armdos_outb(0xF2, (v & 1) | 2);
    } else if (!strcmp(a, "RESTORE")) {
        if (ICA[0] == 'T' && ICA[1] == 'B') { armdos_outb(0xF2, ICA[2] & 3); ICA[0] = ICA[1] = 0; }
        else armdos_outb(0xF2, v & 1);
    } else {
        puts("Sets the processor's clock.\n\n"
             "TURBO [ON | OFF | MAX | RESTORE]\n\n"
             "  ON       full clock, like the TURBO button\n"
             "  OFF      slow clock (12 MHz)\n"
             "  MAX      no clock limit: as fast as the host computer allows\n"
             "  RESTORE  back to the setting before TURBO MAX");
        return a[0] == '/' && a[1] == '?' ? 0 : 1;
    }
    show();
    return 0;
}
