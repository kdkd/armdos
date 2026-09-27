/* bios.h - ROM BIOS services (Microsoft C style). Buffers are flat pointers. */
#ifndef _ARMDOS_BIOS_H
#define _ARMDOS_BIOS_H
#ifdef __cplusplus
extern "C" {
#endif

#define _KEYBRD_READ         0x00
#define _KEYBRD_READY        0x01
#define _KEYBRD_SHIFTSTATUS  0x02
#define _NKEYBRD_READ        0x10
#define _NKEYBRD_READY       0x11
#define _NKEYBRD_SHIFTSTATUS 0x12

#define _DISK_RESET   0
#define _DISK_STATUS  1
#define _DISK_READ    2
#define _DISK_WRITE   3
#define _DISK_VERIFY  4
#define _DISK_FORMAT  5

#define _TIME_GETCLOCK 0
#define _TIME_SETCLOCK 1

#define _PRINTER_WRITE  0
#define _PRINTER_INIT   1
#define _PRINTER_STATUS 2

#define _COM_INIT    0
#define _COM_SEND    1
#define _COM_RECEIVE 2
#define _COM_STATUS  3

struct diskinfo_t {
    unsigned drive;     /* 0 = A:, 0x80 = first hard disk */
    unsigned head;
    unsigned track;     /* cylinder */
    unsigned sector;    /* 1-based */
    unsigned nsectors;
    void *buffer;       /* flat */
};

/* _KEYBRD_READY/_NKEYBRD_READY: 0 if no key, else the key (scan<<8|ascii). */
unsigned _bios_keybrd(unsigned service);
/* Returns AX: AH = status, AL = sectors transferred. */
unsigned _bios_disk(unsigned service, struct diskinfo_t *info);
unsigned _bios_equiplist(void);
unsigned _bios_memsize(void);       /* KB of conventional memory */
unsigned _bios_timeofday(unsigned service, long *ticks);
unsigned _bios_printer(unsigned service, unsigned printer, unsigned data);
unsigned _bios_serialcom(unsigned service, unsigned port, unsigned data);

#define bioskey(c)     _bios_keybrd(c)
#define biosequip()    _bios_equiplist()
#define biosmemory()   _bios_memsize()

#ifdef __cplusplus
}
#endif
#endif
