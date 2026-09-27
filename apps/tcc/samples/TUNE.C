/* TUNE.C - a tune on the PC speaker.
 *
 * The speaker is driven the classic way: PIT channel 2 (ports 42h/43h)
 * makes the square wave, port 61h connects it to the speaker. The
 * notes are timed with INT 15h AH=86h (wait CX:DX microseconds).
 */
#include <dos.h>
#include <stdio.h>

static void sound(unsigned hz)
{
    unsigned divisor = 1193182 / hz;
    outp(0x43, 0xB6);                   /* channel 2, lo/hi byte, square wave */
    outp(0x42, divisor & 0xFF);
    outp(0x42, divisor >> 8);
    outp(0x61, inp(0x61) | 3);          /* gate on, speaker on */
}

static void nosound(void)
{
    outp(0x61, inp(0x61) & ~3);
}

static void delay_ms(unsigned long ms)
{
    union REGS r;
    unsigned long us = ms * 1000;
    r.h.ah = 0x86;
    r.x.cx = us >> 16;
    r.x.dx = us & 0xFFFF;
    int86(0x15, &r, &r);
}

/* Beethoven, "Ode to Joy": note names and lengths in eighths */
static const char *tune[] = {
    "E4", "E4", "F4", "G4", "G4", "F4", "E4", "D4", "C4", "C4", "D4", "E4", "E4", "D4", "D4",
    "E4", "E4", "F4", "G4", "G4", "F4", "E4", "D4", "C4", "C4", "D4", "E4", "D4", "C4", "C4",
};
static const int len[] = {
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 1, 4,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 1, 4,
};

/* equal temperament from A4 = 440 Hz, in integer arithmetic */
static unsigned frequency(const char *note)
{
    static const int semitone[7] = { 9, 11, 0, 2, 4, 5, 7 };   /* A B C D E F G */
    static const unsigned c4x100[12] = {                        /* C4..B4, Hz*100 */
        26163, 27718, 29366, 31113, 32963, 34923, 36999, 39200, 41530, 44000, 46616, 49388 };
    int n = semitone[note[0] - 'A'], octave = note[1] - '0';
    unsigned long f = c4x100[n];
    while (octave > 4) f *= 2, octave--;
    while (octave < 4) f /= 2, octave++;
    return (unsigned)((f + 50) / 100);
}

int main(void)
{
    int i;
    printf("Ode to Joy on the PC speaker\n");
    for (i = 0; i < (int)(sizeof tune / sizeof tune[0]); i++) {
        unsigned hz = frequency(tune[i]);
        printf("%s %3u Hz%s", tune[i], hz, (i % 5 == 4) ? "\n" : "   ");
        sound(hz);
        delay_ms(len[i] * 150 - 20);
        nosound();
        delay_ms(20);
    }
    printf("\n");
    return 0;
}
