/*
 * qbhelp.cpp - QB.EXE's online help: data/qbhelp.txt (compiled in by
 * apps/tvlib/tools/mkhelp.mjs) in apps/tvlib's help window, which opens at
 * the top of the screen above the program (F1 on a keyword, Shift+F1 Using
 * Help, the Help menu); from a dialog's < Help > or F1, on top of it.
 *
 * Copyright (c) 2026 Europa Micro Systems. MIT License.
 */
#include "qb.h"
#include "tvhelp.h"

#include <string.h>
#include <stdio.h>
#include <ctype.h>

extern const char qbHelpText[];
TFrame *qbNewFrame( TRect r );

/* the program window's colours: frame, title, scroll bars (8-12), then the
 * help text, cross reference, selected one (application 138-140) */
#define cpQbHelp "\x08\x09\x0A\x0B\x0C\x8A\x8B\x8C"

static TRect qbHelpRect()
{
    TRect r = TProgram::deskTop->getExtent();
    r.b.y = r.a.y + 11;
    return r;
}

static void init()
{
    static bool done;
    if (done)
        return;
    done = true;
    tvHelpInit( qbHelpText, cpQbHelp, hcHelp );
    tvHelpRect = qbHelpRect;
    tvHelpTitle = 0;
    tvHelpFrame = qbNewFrame;
    tvHelpShadow = false;
}

void helpOpen( const char *topic )
{
    init();
    if (!tvHelpShow( topic ))
        tvHelpShow( "Survival Guide" );
}

void helpModal( const char *topic )
{
    init();
    TRect (*was)() = tvHelpRect;
    tvHelpModal( tvHelpHas( topic ) ? topic : "Survival Guide" );
    tvHelpRect = was;
}

void helpClose()
{
    tvHelpClose();
    if (docWindow)
        docWindow->select();
}

bool helpIsOpen()
{
    return tvHelpIsOpen();
}

bool helpKeyword( const char *word )
{
    init();
    char w[40];
    snprintf( w, sizeof w, "%s", word );
    for (char *p = w; *p; ++p)
        *p = toupper( (uchar) *p );
    return tvHelpShow( w );
}

const char *helpTopicFor( ushort ctx )
{
    if (ctx >= hcFileMenu && ctx < hcEditMenu) return "File Menu";
    if (ctx >= hcEditMenu && ctx < hcViewMenu) return "Edit Menu";
    if (ctx >= hcViewMenu && ctx < hcSearchMenu) return "View Menu";
    if (ctx >= hcSearchMenu && ctx < hcRunMenu) return "Search Menu";
    if (ctx >= hcRunMenu && ctx < hcDebugMenu) return "Run Menu";
    if (ctx >= hcDebugMenu && ctx < hcOptionsMenu) return "Debug Menu";
    if (ctx >= hcOptionsMenu && ctx < hcHelpMenu) return "Options Menu";
    if (ctx >= hcHelpMenu && ctx < hcDlgFind) return "Help Menu";
    switch (ctx)
    {
        case hcMenuBar:        return "Menus";
        case hcDlgFind:        return "Search Menu";
        case hcDlgChange:
        case hcDlgVerify:      return "Search Menu";
        case hcDlgOpen:
        case hcDlgSaveAs:
        case hcDlgPrint:
        case hcDlgSaveChanges: return "File Menu";
        case hcDlgDisplay:
        case hcDlgHelpPath:    return "Options Menu";
        case hcDlgSubs:        return "View Menu";
        case hcDlgNewSub:      return "SUB";
        case hcDlgError:       return "Run-Time Errors";
        case hcHelp:           return "Using Help";
        case hcImmediate:      return "Immediate Window";
    }
    return "Survival Guide";
}
