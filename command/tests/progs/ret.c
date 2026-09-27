/* RET.COM: exit with the decimal code given in the command tail (twin of
 * tests/ref/x86/ret.asm, for ERRORLEVEL tests) */
#include <dos.h>

int main(void)
{
    const unsigned char *t = (const unsigned char *)_armdos_psp + 0x81;
    unsigned v = 0;
    while (*t == ' ' || *t == '\t') t++;
    while (*t >= '0' && *t <= '9') v = v * 10 + (*t++ - '0');
    return v & 0xFF;
}
