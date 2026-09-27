/* TRASH.COM: fill the top 96 KB of conventional memory below the top given
 * in our PSP (PSP:0002h), where COMMAND's transient part lives while no
 * program runs (twin of ref/x86/trash.asm).  (The C start-up has shrunk our
 * block by then; that memory is free, as a big program would have it.) */
#include <dos.h>
#include <string.h>

int main(void)
{
    unsigned char *top = (unsigned char *)((unsigned long)_armdos_psp->memtop << 4);
    unsigned long n = 96UL * 1024;
    memset(top - n, 0xAA, n);
    return 0;
}
