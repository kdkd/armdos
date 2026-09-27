/*
 * SPIN.EXE - test helper for PRINT: burn N timer ticks without calling DOS
 * (InDOS stays 0, so only PRINT's INT 1Ch path can print meanwhile).
 * Writes "SPIN<" / "SPIN>" to the debug port (E9h) at the start and end.
 */
#include "u4.h"
int main(void)
{
    const char *p = u4_cmdline();
    unsigned n = 0;
    while (*p == ' ') p++;
    while (*p >= '0' && *p <= '9') n = n * 10 + (*p++ - '0');
    armdos_debug("SPIN<");
    uint32_t t0 = ARMDOS_BIOS_TICKS;
    while (ARMDOS_BIOS_TICKS - t0 < n) ;
    armdos_debug("SPIN>");
    return 0;
}
