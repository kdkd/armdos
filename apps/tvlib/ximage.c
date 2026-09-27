/*
 * ximage.c - the image side of xload.c: an IDE image that xload.c loaded
 * into an XMS block finds the block's handle in its PSP (bytes 58h-5Bh =
 * "DX" + handle) and gives the block back to HIMEM.SYS when it exits.
 *
 * (We are still running from the block while exit() finishes; DOS gets
 * control with INT 21h AH=4Ch before anything can reuse the memory.)
 *
 * Copyright (c) 2026 Europa Micro Systems. MIT License.
 */
#include <stdlib.h>
#include <string.h>
#include <armdos.h>
#include "tvlib.h"

extern struct psp *_armdos_psp;
static uint16_t image_handle;

static void free_image_block(void)
{
    struct armregs r;
    void *entry = armdos_xms_entry();

    if (!image_handle || !entry)
        return;
    memset(&r, 0, sizeof r);
    r.r0 = 0x0D00; r.r3 = image_handle;    /* unlock */
    _armdos_farcall(entry, &r);
    memset(&r, 0, sizeof r);
    r.r0 = 0x0A00; r.r3 = image_handle;    /* free */
    _armdos_farcall(entry, &r);
    image_handle = 0;
}

int xload_in_xms(void)
{
    return image_handle != 0;
}

__attribute__((constructor)) static void xload_image_init(void)
{
    uint8_t *p = (uint8_t *)_armdos_psp;

    if (p && p[0x58] == 'D' && p[0x59] == 'X')
    {
        image_handle = p[0x5A] | (p[0x5B] << 8);
        p[0x58] = p[0x59] = 0;
        atexit(free_image_block);
    }
}
