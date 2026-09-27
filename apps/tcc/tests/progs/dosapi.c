/* the DOS/BIOS interfaces of the SDK headers, called from TCC-compiled code */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dos.h>
#include <bios.h>
#include <io.h>
#include <fcntl.h>
#include <direct.h>
#include <conio.h>

int main(int argc, char **argv)
{
    union REGS r;
    struct find_t f;
    struct dosdate_t d;
    char cwd[80];
    int fd, n = 0;
    unsigned drive;

    r.h.ah = 0x30;                       /* DOS version */
    intdos(&r, &r);
    printf("DOS %d.%02d\n", r.h.al, r.h.ah);
    int86(0x11, &r, &r);                 /* equipment list */
    printf("equipment %04X\n", r.x.ax & 0xFFFF);
    printf("memory %u KB\n", _bios_memsize());
    _dos_getdate(&d);
    printf("year %u\n", d.year);
    _dos_getdrive(&drive);
    printf("drive %c:\n", 'A' + drive - 1);
    getcwd(cwd, sizeof cwd);
    printf("cwd %s\n", cwd);
    if (_dos_findfirst("C:\\TC\\INCLUDE\\S*.H", _A_NORMAL, &f) == 0) {
        do n++; while (_dos_findnext(&f) == 0);
    }
    printf("S*.H files: %d\n", n);
    if (_dos_findfirst("C:\\TC\\COPYING.TXT", _A_NORMAL, &f) == 0)
        printf("found %s, %lu bytes, attr %02X\n", f.name, f.size, f.attrib);
    fd = open("TEST.TMP", O_CREAT | O_TRUNC | O_WRONLY | O_TEXT, 0666);
    write(fd, "line1\nline2\n", 12);
    close(fd);
    fd = open("TEST.TMP", O_RDONLY | O_BINARY);
    printf("text mode file: %ld bytes on disk\n", filelength(fd));
    close(fd);
    unlink("TEST.TMP");
    printf("PATH=%s\n", getenv("PATH") ? getenv("PATH") : "(none)");
    printf("peek BIOS columns: %d\n", peekb(0x40, 0x4A));
    printf("argc %d argv[1] %s\n", argc, argc > 1 ? argv[1] : "-");
    _disable();
    _enable();
    printf("kbhit %d\n", kbhit());
    return 0;
}
