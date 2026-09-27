/* float.c - soft-float maths and %f (linked with $(ARMDOS_PRINTF_FLOAT)). */
#include <stdio.h>
#include <math.h>

int main(void)
{
    double x = 2.0;
    printf("sqrt(2) = %.10f, pi = %g\n", sqrt(x), 4 * atan(1.0));
    return 0;
}
