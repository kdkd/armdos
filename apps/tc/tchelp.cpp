/*
 * tchelp.cpp - TC.EXE's online help: the text of data/tchelp.txt (compiled
 * in by apps/tvlib/tools/mkhelp.mjs) in apps/tvlib's help window; F1 in a
 * menu, dialog or window shows the topic for it, Ctrl+F1 the topic of the
 * word at the cursor (printf, #include ...).
 *
 * Copyright (c) 2026 Europa Micro Systems. MIT License.
 */
#include "tc.h"
#include "tvhelp.h"

#include <string.h>
#include <ctype.h>

extern const char tcHelpText[];

/* the help window: cyan frame, cyan text, yellow cross references
 * (application palette entries 138-140, see TcApp::getPalette) */
#define cpTcHelp "\x10\x11\x12\x13\x14\x8A\x8B\x8C"

static TRect tcHelpRect()
{
    /* the help text is up to 78 columns wide */
    TRect r = TProgram::deskTop->getExtent();
    r.grow( 0, -1 );
    return r;
}

static void init()
{
    static bool done;
    if (done)
        return;
    done = true;
    tvHelpInit( tcHelpText, cpTcHelp, hcHelpWin );
    tvHelpRect = tcHelpRect;
    tvHelpTitle = "Help";
}

void helpShow( const char *topic )
{
    init();
    if (!tvHelpShow( topic ))
        tvHelpShow( "Contents" );
}

const char *helpTopicForCtx( ushort ctx )
{
    switch (ctx)
    {
        case hxAbout: case hxAboutH:    return "About";
        case hxOpen:                    return "File Open";
        case hxNew:                     return "File New";
        case hxSave:                    return "File Save";
        case hxGetInfo:                 return "Get info";
        case hxRun:                     return "Run menu";
        case hxUserScreen:              return "User screen";
        case hxMessage: case hcMessageWin: return "Message window";
        case hxCompileObj: case hxMake: case hxLink: case hxBuildAll: return "Compile menu";
        case hcEditorWin:               return "Editor keys";
        case hcProjectWin:              return "Project menu";
        case hcHelpWin:                 return "Help on help";
        case hxHelpOnHelp:              return "Help on help";
        case hxContents:                return "Contents";
    }
    if (ctx >= hcSystemMenu && ctx < hcFileMenu) return "System menu";
    if (ctx >= hcFileMenu && ctx < hcEditMenu) return "File menu";
    if (ctx >= hcEditMenu && ctx < hcSearchMenu) return "Edit menu";
    if (ctx >= hcSearchMenu && ctx < hcRunMenu) return "Search menu";
    if (ctx >= hcRunMenu && ctx < hcCompileMenu) return "Run menu";
    if (ctx >= hcCompileMenu && ctx < hcDebugMenu) return "Compile menu";
    if (ctx >= hcDebugMenu && ctx < hcProjectMenu) return "Debug menu";
    if (ctx >= hcProjectMenu && ctx < hcOptionsMenu) return "Project menu";
    if (ctx >= hcOptionsMenu && ctx < hcWindowMenu) return "Options menu";
    if (ctx >= hcWindowMenu && ctx < hcHelpMenu) return "Window menu";
    if (ctx >= hcHelpMenu) return "Help menu";
    return "Contents";
}

void helpContext( ushort ctx )
{
    helpShow( helpTopicForCtx( ctx ) );
    updateMenusState();
}

void helpIndex()
{
    init();
    tvHelpIndex();
    updateMenusState();
}

void helpPrevious()
{
    init();
    tvHelpPrevious();
}

void helpTopicSearch( const char *word )
{
    init();
    char w[48];
    snprintf( w, sizeof w, "%s", word ? word : "" );
    /* "#include" is found as "include" by the editor's word rule */
    if (w[0] && tvHelpShow( w ))
        ;
    else if (w[0] && w[0] != '#' && strlen( w ) < sizeof w - 3)
    {
        char h[52];
        snprintf( h, sizeof h, "%s.h", w );
        if (!tvHelpShow( h ))
            tvHelpIndex();
    }
    else
        tvHelpIndex();
    updateMenusState();
}
