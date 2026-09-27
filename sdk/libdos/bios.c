/* bios.c - bios.h */
#include <bios.h>
#include "libdos.h"

unsigned _bios_keybrd(unsigned service)
{
    struct armregs r = {0};
    r.r0 = (service & 0xFF) << 8;
    _armdos_int16(&r);
    if (service == _KEYBRD_READY || service == _NKEYBRD_READY)
        return (r.cpsr & ARM_CPSR_Z) ? 0 : (r.r0 & 0xFFFF);
    if (service == _KEYBRD_SHIFTSTATUS)
        return r.r0 & 0xFF;
    return r.r0 & 0xFFFF;
}

unsigned _bios_disk(unsigned service, struct diskinfo_t *d)
{
    struct armregs r = {0};
    r.r0 = ((service & 0xFF) << 8) | (d->nsectors & 0xFF);
    r.r1 = (uint32_t)d->buffer;                                   /* ES:BX */
    r.r2 = ((d->track & 0xFF) << 8) | ((d->track >> 2) & 0xC0) | (d->sector & 0x3F);
    r.r3 = ((d->head & 0xFF) << 8) | (d->drive & 0xFF);
    _armdos_int13(&r);
    return r.r0 & 0xFFFF;
}

unsigned _bios_equiplist(void)
{
    struct armregs r = {0};
    _armdos_intr(0x11, &r);
    return r.r0 & 0xFFFF;
}

unsigned _bios_memsize(void)
{
    struct armregs r = {0};
    _armdos_intr(0x12, &r);
    return r.r0 & 0xFFFF;
}

unsigned _bios_timeofday(unsigned service, long *ticks)
{
    struct armregs r = {0};
    r.r0 = (service & 0xFF) << 8;
    if (service == _TIME_SETCLOCK) {
        r.r2 = ((unsigned long)*ticks >> 16) & 0xFFFF;
        r.r3 = (unsigned long)*ticks & 0xFFFF;
    }
    _armdos_int1a(&r);
    if (service == _TIME_GETCLOCK)
        *ticks = (long)(((r.r2 & 0xFFFF) << 16) | (r.r3 & 0xFFFF));
    return r.r0 & 0xFF;         /* AL = midnight flag */
}

unsigned _bios_printer(unsigned service, unsigned printer, unsigned data)
{
    struct armregs r = {0};
    r.r0 = ((service & 0xFF) << 8) | (data & 0xFF);
    r.r3 = printer;
    _armdos_intr(0x17, &r);
    return (r.r0 >> 8) & 0xFF;
}

unsigned _bios_serialcom(unsigned service, unsigned port, unsigned data)
{
    struct armregs r = {0};
    r.r0 = ((service & 0xFF) << 8) | (data & 0xFF);
    r.r3 = port;
    _armdos_intr(0x14, &r);
    return r.r0 & 0xFFFF;
}
