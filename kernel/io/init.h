/* init.h - SYSINIT's shared state */
#ifndef INIT_H
#define INIT_H

#include "iosys.h"

struct sysinit {
    int bootdrive_bios;
    int bootunit;               /* 0 = A:, 2 = C: */
    int nunits;
    int handles_open;
    uint32_t dos_end;
    const struct dosapi *api;
};
extern struct sysinit S;

void sys_printf(const char *fmt, ...);
__attribute__((noreturn)) void sys_halt(void);
void sys_reopen_std(void);
uint32_t ar1_place(const uint8_t *f, uint32_t fsize, uint32_t base, uint32_t limit, uint32_t *end);
__attribute__((noreturn)) void sysinit_config(void);

#endif
