/*
 * plat.cpp - the ARM-DOS platform layer of Turbo Vision for TC.EXE and
 * QB.EXE: EDIT's (apps/edit/armdos/hardware.cpp, compiled here unchanged)
 * plus a few extras the IDEs need (tvlib.h).
 *
 * Copyright (c) 2026 Europa Micro Systems. MIT License.
 */
#include "../edit/armdos/hardware.cpp"
#include "tvlib.h"

void tvInvalidateScreen()
{
    if (!shadow)
        return;
    /* a value no cell can have: every cell is rewritten on the next flush */
    for (int i = 0; i < scrCols * scrRows; ++i)
        shadow[i] = 0xFFFF;
    readVideoInfo();
    hwCaretSize = -1; hwCaretX = hwCaretY = -1;
}

int tvKeyWaiting()
{
    return keyAvailable();
}

unsigned tvGetKey()
{
    struct armregs r = {};
    r.r0 = 0x1000;
    _armdos_int16(&r);
    return r.r0 & 0xFFFF;
}

void tvWaitAnyKey()
{
    /* a key, or a mouse button pressed. While Turbo Vision is suspended
     * (a user screen) the driver has no handler of ours: poll it. */
    unsigned seen = mouseTail;
    unsigned polled = 0;
    if (mousePresent && !consoleUp)
        polled = int33(0x0003).r1 & 3;
    for (;;)
    {
        if (keyAvailable())
        {
            tvGetKey();
            break;
        }
        bool click = false;
        if (mousePresent && !consoleUp)
        {
            unsigned b = int33(0x0003).r1 & 3;
            click = (b & ~polled) != 0;
            polled = b;
            if (click)
            {
                /* wait for the release, so it does not click in the IDE */
                while (int33(0x0003).r1 & 3)
                    armdos_halt();
            }
        }
        else
            while (seen != mouseTail)
            {
                if (mouseQ[seen % mouseQSize].buttons & ~lastButtons & 3)
                    click = true;
                ++seen;
            }
        if (click)
            break;
        armdos_halt();
    }
    /* the mouse events so far are used up */
    if (mousePresent && consoleUp)
    {
        mouseHead = mouseTail;
        struct armregs r = int33(0x0003);
        lastButtons = r.r1 & 3;
    }
}
