/* soft-float code generation checks (compared against GCC's output) */
#include <stdio.h>

static double add(double a, double b) { return a + b; }
static float addf(float a, float b) { return a + b; }
static double mix(int i, double d, float f, long long ll, unsigned u)
{
    return i + d * f - ll / 2 + u;
}
static int cmp(double a, double b)
{
    return (a < b) * 1 + (a <= b) * 2 + (a > b) * 4 + (a >= b) * 8 + (a == b) * 16 + (a != b) * 32;
}
static int cmpf(float a, float b)
{
    return (a < b) * 1 + (a <= b) * 2 + (a > b) * 4 + (a >= b) * 8 + (a == b) * 16 + (a != b) * 32;
}
struct S { float f; double d; char c; };

int main(void)
{
    double d = 1.5, e = -2.25, z = 0.0;
    float f = 3.25f, g = 0.5f;
    int i, n;
    unsigned u = 4000000000u;
    long long ll = -1234567890123LL;
    unsigned long long ull = 18000000000000000000ULL;
    struct S s = { 1.25f, 2.5, 'x' };
    double arr[4] = { 1, 2, 3, 4 };

    printf("add %d %d\n", (int)(add(d, e) * 100), (int)(addf(f, g) * 100));
    printf("ops %d %d %d %d\n", (int)((d + e) * 1000), (int)((d - e) * 1000), (int)(d * e * 1000), (int)(d / e * 1000));
    printf("fops %d %d %d %d\n", (int)((f + g) * 1000), (int)((f - g) * 1000), (int)(f * g * 1000), (int)(f / g * 1000));
    printf("neg %d %d\n", (int)(-d * 10), (int)(-f * 10));
    printf("cmp %d %d %d %d\n", cmp(d, e), cmp(e, d), cmp(d, d), cmpf(f, g));
    printf("conv %d %u %d %u\n", (int)e, (unsigned)d, (int)-3.99, (unsigned)(double)u);
    printf("conv2 %d %d\n", (int)(double)ll == (int)ll, (int)((double)ll / 1e6));
    printf("conv3 %d %d\n", (int)((float)ull / 1e15f), (int)((double)ull / 1e15));
    printf("ll %d %d\n", (int)(long long)(d * 1e12 / 1e9), (int)((unsigned long long)(d * 4e18) >> 40));
    printf("mix %d\n", (int)(mix(3, d, f, ll, u) / 1000));
    printf("struct %d %d %c\n", (int)(s.f * 100), (int)(s.d * 100), s.c);
    for (i = 0, n = 0; i < 4; i++) n += (int)arr[i] * (int)arr[i];
    printf("arr %d\n", n);
    d = 0.1; for (i = 0; i < 10; i++) z += d;
    printf("sum %d %d\n", z == 1.0, (int)(z * 1e15) );
    printf("zero %d %d %d\n", -0.0 == 0.0, 1 / -0.0 < 0, !z);
    f = 1; for (i = 0; i < 20; i++) f *= 1.5f;
    printf("pow %d\n", (int)f);
    d = 1e300; d *= 1e10;
    printf("inf %d %d\n", d > 1e308, d == d + 1);
    d = d - d;
    printf("nan %d %d %d\n", d == d, d != d, d < 1);
    printf("printf %.3f %e %g\n", 3.14159, 12345.678, 0.0001234);
    {
        double x = 2.0, y = 1.0;
        for (i = 0; i < 30; i++) y = (y + x / y) / 2;
        printf("sqrt2 %.10f\n", y);
    }
    {
        int k; double t = 1;
        for (k = 1, z = 1; k < 18; k++) { t /= k; z += t; }
        printf("e %.12f\n", z);
    }
    {
        char c = 'A'; short sh = -300; unsigned char uc = 200;
        double a = c, b = sh, cc = uc;
        printf("small %d %d %d\n", (int)a, (int)b, (int)cc);
        c = (char)(a + 1); sh = (short)(b * 2); uc = (unsigned char)(cc + 50);
        printf("small2 %d %d %d\n", c, sh, uc);
    }
    return 0;
}
