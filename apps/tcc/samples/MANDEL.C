/* MANDEL.C - the Mandelbrot set in text mode, in floating point.
 *
 * The ARM926 in this machine has no floating point unit: TCC compiles
 * every double operation into a call to the run-time library's
 * software floating point (__aeabi_dadd, __aeabi_dmul, ...).
 */
#include <stdio.h>

int main(void)
{
    static const char shade[] = " .:-=+*#%@";
    int row, col;
    double sum = 0;

    for (row = 0; row < 22; row++) {
        double ci = -1.1 + row * 0.1;
        for (col = 0; col < 78; col++) {
            double cr = -2.1 + col * 0.037, zr = 0, zi = 0;
            int n = 0;
            while (n < 99 && zr * zr + zi * zi < 4.0) {
                double t = zr * zr - zi * zi + cr;
                zi = 2 * zr * zi + ci;
                zr = t;
                n++;
            }
            sum += n;
            putchar(n == 99 ? '@' : shade[n % 9]);
        }
        putchar('\n');
    }
    printf("average iterations per point: %.3f\n", sum / (22 * 78));
    return 0;
}
