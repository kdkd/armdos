/*
 * game_armdos.c - DUKE3D.EXE's start-up on ARM-DOS: the game directory, the
 * extended memory the extender stub (loader/d3dload.c) loaded us into, and
 * the text-mode title bars of GAME.C main().
 *
 * Copyright (C) 2026 the ARM-DOS project. GPL-2 or later (see COPYING).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <direct.h>
#include <armdos.h>
/* armdos.h's register views (AX(f), CX(f) ...) clash with duke3d.h's actor macros */
#undef AX
#undef AL
#undef AH
#undef BX
#undef BL
#undef BH
#undef CX
#undef CL
#undef CH
#undef DX
#undef DL
#undef DH

#include "duke3d.h"
#include "armdos_duke.h"

int armdos_duke_vga_present(void) { return armdos_vga_present(); }

extern const char *_armdos_progpath;
extern struct psp *_armdos_psp;

/* ---- the XMS block holding our own image (loader/d3dload.c) ----------- */
static uint16_t image_handle;

static void free_image_block(void)
{
    struct armregs r;
    void *entry = armdos_xms_entry();

    if (!image_handle || !entry)
        return;
    /* We are running from this block: DOS gets control back (INT 21h
       AH=4Ch) before anything can reuse the memory. */
    memset(&r, 0, sizeof r);
    r.r0 = 0x0D00; r.r3 = image_handle;    /* unlock */
    _armdos_farcall(entry, &r);
    memset(&r, 0, sizeof r);
    r.r0 = 0x0A00; r.r3 = image_handle;    /* free */
    _armdos_farcall(entry, &r);
    image_handle = 0;
}

/* ---- the game directory -------------------------------------------------
 * DUKE3D.EXE expected to be started in its own directory. "C:\>GAMES\DUKE3D\
 * DUKE3D" works too: without DUKE3D.GRP here we go to DUKE3D.EXE's directory
 * for the run and come back at exit. */
static char saved_cwd[80];
static int saved_drive;

static void restore_cwd(void)
{
    if (saved_cwd[0])
    {
        _chdrive(saved_drive);
        chdir(saved_cwd);
        saved_cwd[0] = 0;
    }
}

void armdos_duke_init(void)
{
    uint8_t *p = (uint8_t *)_armdos_psp;
    char dir[80], *slash;

    if (p[0x58] == 'D' && p[0x59] == 'X')
    {
        image_handle = p[0x5A] | (p[0x5B] << 8);
        p[0x58] = p[0x59] = 0;
        atexit(free_image_block);
    }

    if (access("DUKE3D.GRP", 0) != 0 && _armdos_progpath[0])
    {
        strncpy(dir, _armdos_progpath, sizeof dir - 1);
        dir[sizeof dir - 1] = 0;
        slash = strrchr(dir, '\\');
        if (slash)
        {
            *slash = 0;
            if (slash == dir + 2 && dir[1] == ':')
                strcpy(slash, "\\");
            saved_drive = _getdrive();
            if (getcwd(saved_cwd, sizeof saved_cwd))
            {
                if (dir[1] == ':')
                    _chdrive((dir[0] & ~0x20) - 'A' + 1);
                if (chdir(dir) == 0)
                    atexit(restore_cwd);
                else
                    restore_cwd();
            }
        }
    }
}

/* ---- GAME.C main(): setvmode(3), then the two title bars in attribute 79
 * (bright white on red) at the top of the colour text screen. ---------- */
void armdos_title_screen(const char *head)
{
    static const char copyright[] =
        "                   Copyright (c) 1996 3D Realms Entertainment                   ";
    char bar[81];
    struct armregs r;
    int len = strlen(head);

    memset(&r, 0, sizeof r);
    r.r0 = 0x0003;
    _armdos_int10(&r);

    memset(bar, ' ', 80);
    bar[80] = 0;
    printstr(0, 0, (uint8_t *)bar, 79);
    printstr(40 - (len >> 1), 0, (uint8_t *)head, 79);
    printstr(0, 1, (uint8_t *)copyright, 79);

    printf("\n\n");
}

/* ---- the known group files (FIX_00022's table), by size --------------- */
int armdos_known_grp(int32_t size, uint32_t *crc)
{
    int i;

    for (i = 0; i < MAX_KNOWN_GRP; i++)
        if (crc32lookup[i].size == size)
        {
            *crc = crc32lookup[i].crc32;
            return 1;
        }
    return 0;
}
