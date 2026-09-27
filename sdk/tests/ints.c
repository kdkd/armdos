/* ints.c - dos.h / conio.h / bios.h / int86 and an interrupt handler. */
#include <stdio.h>
#include <dos.h>
#include <conio.h>
#include <bios.h>
#include <direct.h>
#include <armdos.h>

static volatile unsigned long ticks;
static armdos_vect_t old1c;

/* INT 1Ch (timer tick hook): a plain C function taking the frame. */
static void tick_handler(struct armregs *f)
{
    ticks++;
    armdos_callold(old1c, f);
}

int main(void)
{
    union REGS in, out;
    struct find_t ft;
    struct dosdate_t d;
    struct dostime_t t;
    char cwd[80];

    in.h.ah = 0x30;                 /* DOS version */
    intdos(&in, &out);
    printf("DOS %u.%02u (_osmajor %u)\n", out.h.al, out.h.ah, _osmajor);

    in.x.ax = 0x0F00;               /* INT 10h AH=0Fh: video mode */
    int86(0x10, &in, &out);
    printf("video mode %02Xh, %u columns\n", out.h.al, out.h.ah);

    printf("equipment %04X, memory %u KB\n", _bios_equiplist(), _bios_memsize());

    _dos_getdate(&d);
    _dos_gettime(&t);
    printf("%04u-%02u-%02u %02u:%02u:%02u\n", d.year, d.month, d.day, t.hour, t.minute, t.second);

    if (getcwd(cwd, sizeof cwd)) printf("cwd %s (drive %d)\n", cwd, _getdrive());

    if (_dos_findfirst("*.*", _A_NORMAL | _A_SUBDIR, &ft) == 0) {
        do printf("  %-12s %8lu %c\n", ft.name, ft.size, (ft.attrib & _A_SUBDIR) ? 'D' : ' ');
        while (_dos_findnext(&ft) == 0);
    }

    old1c = _dos_getvect(0x1C);
    _dos_setvect(0x1C, tick_handler);
    cputs("waiting for 18 ticks...\r\n");
    while (ticks < 18) armdos_halt();
    _dos_setvect(0x1C, old1c);

    outp(0x61, inp(0x61) & ~3);     /* speaker off */
    printf("PIC mask %02X, text vram at %p: %04X\n", inp(0x21), (void *)MK_FP(0xB800, 0), peek(0xB800, 0));

    cprintf("press a key: ");
    int k = getch();
    cprintf("%02X\r\n", k);
    return 0;
}
