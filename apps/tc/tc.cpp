/*
 * tc.cpp - TC.EXE, "ARM Turbo C": the application - menu bar, status line,
 * desktop, command dispatch, start-up and the configuration file.
 *
 *   TC [file.C | project.PRJ]
 *
 * An integrated development environment in the style of 1990's Turbo C++
 * (the blue Turbo Vision IDE): editor windows, the Message window, F9 Make,
 * Ctrl+F9 Run, Alt+F5 User screen, Alt+F9 Compile, a Project, Options and
 * context help. The compiler is TinyCC (apps/tcc) linked in (tcomp.c); the
 * IDE runs from extended memory (apps/tvlib/xload.c), so the programs it
 * builds and runs get nearly all of conventional memory.
 *
 * Copyright (c) 2026 Europa Micro Systems. MIT License.
 */
#include "tc.h"
#include "tvhelp.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <direct.h>
#include <armdos.h>

/* the heap lives in XMS (the IDE runs from an XMS block): take 4 MB of it,
 * and leave the rest to the programs run from the IDE */
extern "C" { unsigned _armdos_xms_kb = 4096; }

TcOptions opts = {
    "C:\\TC\\INCLUDE", "C:\\TC\\LIB", "", "",
    1, 0, 16, 0, envBackup | envAutoSave | envHighlight | envAutoIndent, 8, ""
};
char tcHome[80] = "C:\\TC";
char configPath[90];

class TcApp : public TApplication
{
public:
    TcApp();
    static TMenuBar *initMenuBar( TRect r );
    static TStatusLine *initStatusLine( TRect r );
    static TDeskTop *initDeskTop( TRect r );
    virtual void handleEvent( TEvent &event );
    virtual void idle();
    virtual void getEvent( TEvent &event );
    virtual void writeShellMsg();
    virtual TPalette &getPalette() const;
    void newFile();
    void openFile();
    void saveAll();
};

TcApp *tcApp;
static bool escapingMenus, helpPending;   /* F1 in a menu (getEvent) */
static ushort pendingHelpCtx;

/* ------------------------------------------------------------- menus */

TMenuBar *TcApp::initMenuBar( TRect r )
{
    r.b.y = r.a.y + 1;
    return new TMenuBar( r,
      *new TSubMenu( "~\xF0~", kbAltSpace, hcSystemMenu ) +
        *new TMenuItem( "~A~bout...", cmAbout, kbNoKey, hxAbout ) +
        newLine() +
        *new TMenuItem( "~R~epaint desktop", cmRepaint, kbNoKey, hxRepaint ) +
      *new TSubMenu( "~F~ile", kbAltF, hcFileMenu ) +
        *new TMenuItem( "~O~pen...", cmOpen, kbF3, hxOpen, "F3" ) +
        *new TMenuItem( "~N~ew", cmNew, kbNoKey, hxNew ) +
        *new TMenuItem( "~S~ave", cmSave, kbF2, hxSave, "F2" ) +
        *new TMenuItem( "S~a~ve as...", cmSaveAs, kbNoKey, hxSaveAs ) +
        *new TMenuItem( "Save a~l~l", cmSaveAll, kbNoKey, hxSaveAll ) +
        newLine() +
        *new TMenuItem( "~C~hange dir...", cmChDir, kbNoKey, hxChDir ) +
        *new TMenuItem( "~P~rint", cmPrint, kbNoKey, hxPrint ) +
        *new TMenuItem( "~G~et info...", cmGetInfo, kbNoKey, hxGetInfo ) +
        *new TMenuItem( "~D~OS shell", cmDosShell, kbNoKey, hxDosShell ) +
        *new TMenuItem( "~Q~uit", cmQuit, kbAltX, hxQuit, "Alt+X" ) +
      *new TSubMenu( "~E~dit", kbAltE, hcEditMenu ) +
        *new TMenuItem( "~R~estore line", cmUndo, kbNoKey, hxRestore ) +
        newLine() +
        *new TMenuItem( "Cu~t~", cmCut, kbShiftDel, hxCut, "Shift+Del" ) +
        *new TMenuItem( "~C~opy", cmCopy, kbCtrlIns, hxCopy, "Ctrl+Ins" ) +
        *new TMenuItem( "~P~aste", cmPaste, kbShiftIns, hxPaste, "Shift+Ins" ) +
        *new TMenuItem( "Copy e~x~ample", cmCopyExample, kbNoKey, hxCopyExample ) +
        *new TMenuItem( "~S~how clipboard", cmShowClip, kbNoKey, hxShowClip ) +
        newLine() +
        *new TMenuItem( "C~l~ear", cmClear, kbCtrlDel, hxClear, "Ctrl+Del" ) +
      *new TSubMenu( "~S~earch", kbAltS, hcSearchMenu ) +
        *new TMenuItem( "~F~ind...", cmFind, kbNoKey, hxFind ) +
        *new TMenuItem( "~R~eplace...", cmReplace, kbNoKey, hxReplace ) +
        *new TMenuItem( "~S~earch again", cmSearchAgain, kbCtrlL, hxSearchAgain, "Ctrl+L" ) +
        newLine() +
        *new TMenuItem( "~G~o to line number...", cmGotoLine, kbNoKey, hxGotoLine ) +
        *new TMenuItem( "~P~revious error", cmPrevError, kbAltF7, hxPrevError, "Alt+F7" ) +
        *new TMenuItem( "~N~ext error", cmNextError, kbAltF8, hxNextError, "Alt+F8" ) +
        *new TMenuItem( "~L~ocate function...", cmLocateFunc, kbNoKey, hxLocateFunc ) +
      *new TSubMenu( "~R~un", kbAltR, hcRunMenu ) +
        *new TMenuItem( "~R~un", cmRun, kbCtrlF9, hxRun, "Ctrl+F9" ) +
        *new TMenuItem( "~P~rogram reset", cmProgReset, kbCtrlF2, hxProgReset, "Ctrl+F2" ) +
        *new TMenuItem( "~G~o to cursor", cmGotoCursor, kbF4, hxGotoCursor, "F4" ) +
        *new TMenuItem( "~T~race into", cmTraceInto, kbF7, hxTrace, "F7" ) +
        *new TMenuItem( "~S~tep over", cmStepOver, kbF8, hxStep, "F8" ) +
        *new TMenuItem( "~A~rguments...", cmArguments, kbNoKey, hxArguments ) +
      *new TSubMenu( "~C~ompile", kbAltC, hcCompileMenu ) +
        *new TMenuItem( "~C~ompile to OBJ", cmCompileObj, kbAltF9, hxCompileObj, "Alt+F9" ) +
        *new TMenuItem( "~M~ake EXE file", cmMake, kbF9, hxMake, "F9" ) +
        *new TMenuItem( "~L~ink EXE file", cmLink, kbNoKey, hxLink ) +
        *new TMenuItem( "~B~uild all", cmBuildAll, kbNoKey, hxBuildAll ) +
        newLine() +
        *new TMenuItem( "~I~nformation...", cmInformation, kbNoKey, hxInformation ) +
        *new TMenuItem( "~R~emove messages", cmRemoveMsgs, kbNoKey, hxRemoveMsgs ) +
      *new TSubMenu( "~D~ebug", kbAltD, hcDebugMenu ) +
        *new TMenuItem( "~I~nspect...", cmInspect, kbAltF4, hxInspect, "Alt+F4" ) +
        *new TMenuItem( "~E~valuate/modify...", cmEvaluate, kbCtrlF4, hxEvaluate, "Ctrl+F4" ) +
        *new TMenuItem( "~C~all stack...", cmCallStack, kbCtrlF3, hxCallStack, "Ctrl+F3" ) +
        *new TMenuItem( "~W~atches", cmWatches, kbNoKey, hxWatches ) +
        *new TMenuItem( "~T~oggle breakpoint", cmToggleBreak, kbCtrlF8, hxToggleBreak, "Ctrl+F8" ) +
        *new TMenuItem( "~B~reakpoints...", cmBreakpoints, kbNoKey, hxBreakpoints ) +
      *new TSubMenu( "~P~roject", kbAltP, hcProjectMenu ) +
        *new TMenuItem( "~O~pen project...", cmOpenProject, kbNoKey, hxOpenProject ) +
        *new TMenuItem( "~C~lose project", cmCloseProject, kbNoKey, hxCloseProject ) +
        newLine() +
        *new TMenuItem( "~A~dd item...", cmAddItem, kbNoKey, hxAddItem ) +
        *new TMenuItem( "~D~elete item", cmDeleteItem, kbNoKey, hxDeleteItem ) +
      *new TSubMenu( "~O~ptions", kbAltO, hcOptionsMenu ) +
        *new TMenuItem( "~C~ompiler...", cmOptCompiler, kbNoKey, hxOptCompiler ) +
        *new TMenuItem( "~M~ake...", cmOptMake, kbNoKey, hxOptMake ) +
        *new TMenuItem( "~L~inker...", cmOptLinker, kbNoKey, hxOptLinker ) +
        *new TMenuItem( "~D~irectories...", cmOptDirs, kbNoKey, hxOptDirs ) +
        *new TMenuItem( "~E~nvironment...", cmOptEnv, kbNoKey, hxOptEnv ) +
        newLine() +
        *new TMenuItem( "~S~ave...", cmOptSave, kbNoKey, hxOptSave ) +
      *new TSubMenu( "~W~indow", kbAltW, hcWindowMenu ) +
        *new TMenuItem( "~S~ize/Move", cmResize, kbCtrlF5, hxSizeMove, "Ctrl+F5" ) +
        *new TMenuItem( "~Z~oom", cmZoom, kbF5, hxZoom, "F5" ) +
        *new TMenuItem( "~T~ile", cmTile, kbNoKey, hxTile ) +
        *new TMenuItem( "C~a~scade", cmCascade, kbNoKey, hxCascade ) +
        *new TMenuItem( "~N~ext", cmNext, kbF6, hxNext, "F6" ) +
        *new TMenuItem( "~C~lose", cmClose, kbAltF3, hxClose, "Alt+F3" ) +
        newLine() +
        *new TMenuItem( "~M~essage", cmMessageWin, kbNoKey, hxMessage ) +
        *new TMenuItem( "~P~roject", cmProjectWin, kbNoKey, hxProjectW ) +
        *new TMenuItem( "~U~ser screen", cmUserScreen, kbAltF5, hxUserScreen, "Alt+F5" ) +
        newLine() +
        *new TMenuItem( "~L~ist all...", cmListAll, kbAlt0, hxListAll, "Alt+0" ) +
      *new TSubMenu( "~H~elp", kbAltH, hcHelpMenu ) +
        *new TMenuItem( "~C~ontents", cmHelpContents, kbNoKey, hxContents ) +
        *new TMenuItem( "~I~ndex", cmHelpIndex, kbShiftF1, hxIndex, "Shift+F1" ) +
        *new TMenuItem( "~T~opic search", cmHelpTopic, kbCtrlF1, hxTopicSearch, "Ctrl+F1" ) +
        *new TMenuItem( "~P~revious topic", cmHelpPrev, kbAltF1, hxPrevTopic, "Alt+F1" ) +
        *new TMenuItem( "~H~elp on help", cmHelpHelp, kbNoKey, hxHelpOnHelp ) +
        newLine() +
        *new TMenuItem( "~A~bout...", cmAbout, kbNoKey, hxAboutH )
    );
}

/* ------------------------------------------------------- status line */

static const struct { ushort ctx; const char *text; } hints[] = {
    { hxAbout, "Show version and copyright information" },
    { hxRepaint, "Redraw the screen" },
    { hxOpen, "Locate and open a file in an edit window" },
    { hxNew, "Create a new file in a new edit window" },
    { hxSave, "Save the file in the active edit window" },
    { hxSaveAs, "Save the current file under a different name" },
    { hxSaveAll, "Save all modified files" },
    { hxChDir, "Choose a new default directory" },
    { hxPrint, "Print the contents of the active edit window" },
    { hxGetInfo, "Show status information" },
    { hxDosShell, "Temporarily exit to DOS" },
    { hxQuit, "Exit ARM Turbo C" },
    { hxRestore, "Cancel edits to the current line" },
    { hxCut, "Remove the selected text and put it in the clipboard" },
    { hxCopy, "Copy the selected text into the clipboard" },
    { hxPaste, "Insert the text from the clipboard at the cursor" },
    { hxCopyExample, "Copy the example of the current help topic to the clipboard" },
    { hxShowClip, "Open the clipboard window" },
    { hxClear, "Delete the selected text" },
    { hxFind, "Search for text" },
    { hxReplace, "Search for text and replace it with new text" },
    { hxSearchAgain, "Repeat the last Find or Replace" },
    { hxGotoLine, "Move the cursor to a line number" },
    { hxPrevError, "Move to the previous error or warning" },
    { hxNextError, "Move to the next error or warning" },
    { hxLocateFunc, "Find the definition of a function" },
    { hxRun, "Make and run the current program" },
    { hxProgReset, "Release the current program" },
    { hxGotoCursor, "Run the program to the cursor (needs the debugger)" },
    { hxTrace, "Execute the next statement, entering functions (needs the debugger)" },
    { hxStep, "Execute the next statement (needs the debugger)" },
    { hxArguments, "Set the command-line arguments of the program" },
    { hxCompileObj, "Compile the active edit window to an .O file" },
    { hxMake, "Make the .EXE file, compiling what changed" },
    { hxLink, "Link the .EXE file without compiling" },
    { hxBuildAll, "Rebuild every file of the program" },
    { hxInformation, "Show information about the last compile" },
    { hxRemoveMsgs, "Clear the Message window" },
    { hxInspect, "Inspect a variable (needs the debugger)" },
    { hxEvaluate, "Evaluate an expression (needs the debugger)" },
    { hxCallStack, "Show the function calls (needs the debugger)" },
    { hxWatches, "Add, delete and edit watches (needs the debugger)" },
    { hxToggleBreak, "Set or clear a breakpoint (needs the debugger)" },
    { hxBreakpoints, "Edit breakpoints (needs the debugger)" },
    { hxOpenProject, "Load a project file" },
    { hxCloseProject, "Close the current project" },
    { hxAddItem, "Add a file to the project" },
    { hxDeleteItem, "Delete the highlighted file from the project" },
    { hxOptCompiler, "Set compiler options: defines, warnings" },
    { hxOptMake, "Set the options of Make" },
    { hxOptLinker, "Set linker options: stack size" },
    { hxOptDirs, "Set the include, library and output directories" },
    { hxOptEnv, "Set editor and environment preferences" },
    { hxOptSave, "Save all the options to TCCONFIG.TC" },
    { hxSizeMove, "Change the size or position of the active window" },
    { hxZoom, "Enlarge or restore the size of the active window" },
    { hxTile, "Arrange windows on the desktop by tiling" },
    { hxCascade, "Arrange windows on the desktop by cascading" },
    { hxNext, "Make the next window active" },
    { hxClose, "Close the active window" },
    { hxMessage, "Open the Message window" },
    { hxProjectW, "Open the Project window" },
    { hxUserScreen, "Switch to the full-screen user output" },
    { hxListAll, "Show a list of all open windows" },
    { hxContents, "Show the table of contents of the online help" },
    { hxIndex, "Show the index of the online help" },
    { hxTopicSearch, "Show help on the word at the cursor" },
    { hxPrevTopic, "Redisplay the previous help screen" },
    { hxHelpOnHelp, "How to use the online help" },
    { hxAboutH, "Show version and copyright information" },
    { hcSystemMenu, "System commands" },
    { hcFileMenu, "File management commands (Open, New, Save, etc.)" },
    { hcEditMenu, "Cut-and-paste editing commands" },
    { hcSearchMenu, "Text and error search commands" },
    { hcRunMenu, "Execute the program" },
    { hcCompileMenu, "Compile, link and make your program" },
    { hcDebugMenu, "Debugging commands" },
    { hcProjectMenu, "Project management commands" },
    { hcOptionsMenu, "Set defaults for the compiler, editor, mouse, etc." },
    { hcWindowMenu, "Open, arrange and list windows" },
    { hcHelpMenu, "Get online help" },
};

const char *ctxHint( ushort ctx )
{
    for (auto &h : hints)
        if (h.ctx == ctx)
            return h.text;
    return "";
}

class TcStatusLine : public TStatusLine
{
public:
    TcStatusLine( const TRect &r, TStatusDef &d ) : TStatusLine( r, d ) {}
    virtual const char *hint( ushort ctx ) { return ctxHint( ctx ); }
};

TStatusLine *TcApp::initStatusLine( TRect r )
{
    r.a.y = r.b.y - 1;
    return new TcStatusLine( r,
        *new TStatusDef( hcHelpWin, hcHelpWin ) +
            *new TStatusItem( "~F1~ Help on help", kbF1, cmHelpHelp ) +
            *new TStatusItem( "~Alt+F1~ Previous topic", kbAltF1, cmHelpPrev ) +
            *new TStatusItem( "~Shift+F1~ Help index", kbShiftF1, cmHelpIndex ) +
            *new TStatusItem( "~Esc~ Close help", kbNoKey, cmClose ) +
        *new TStatusDef( hcMessageWin, hcMessageWin ) +
            *new TStatusItem( "~F1~ Help", kbF1, cmHelpF1 ) +
            *new TStatusItem( "~\x11\xD9~ Edit source", kbNoKey, cmNo ) +
            *new TStatusItem( "~Space~ View source", kbNoKey, cmNo ) +
            *new TStatusItem( "~F10~ Menu", kbF10, cmMenu ) +
        *new TStatusDef( hcProjectWin, hcProjectWin ) +
            *new TStatusItem( "~F1~ Help", kbF1, cmHelpF1 ) +
            *new TStatusItem( "~\x11\xD9~ Edit", kbNoKey, cmNo ) +
            *new TStatusItem( "~Ins~ Add", kbIns, cmAddItem ) +
            *new TStatusItem( "~Del~ Delete", kbDel, cmDeleteItem ) +
            *new TStatusItem( "~F10~ Menu", kbF10, cmMenu ) +
        *new TStatusDef( hcDialog, 99 ) +
            *new TStatusItem( "~F1~ Help", kbF1, cmHelpF1 ) +
        *new TStatusDef( 100, 0xFFFF ) +
            *new TStatusItem( "~F1~ Help", kbF1, cmHelpF1 ) +
        *new TStatusDef( 0, 0xFFFF ) +
            *new TStatusItem( "~F1~ Help", kbF1, cmHelpF1 ) +
            *new TStatusItem( "~F2~ Save", kbF2, cmSave ) +
            *new TStatusItem( "~F3~ Open", kbF3, cmOpen ) +
            *new TStatusItem( "~Alt+F9~ Compile", kbAltF9, cmCompileObj ) +
            *new TStatusItem( "~F9~ Make", kbF9, cmMake ) +
            *new TStatusItem( "~F10~ Menu", kbF10, cmMenu ) +
            *new TStatusItem( 0, kbAltF5, cmUserScreen ) +
            *new TStatusItem( 0, kbCtrlF9, cmRun )
    );
}

/* ------------------------------------------------------------ desktop */

TDeskTop *TcApp::initDeskTop( TRect r )
{
    r.a.y++;
    r.b.y--;
    return new TDeskTop( r );
}

/* Turbo Vision's colour palette is Turbo C++'s; ours after it (136-):
 * the Message window's list (normal, focused), the help window (text,
 * cross reference, selected cross reference) */
#define cpTcExtra "\x30\x1F\x30\x3E\x1E"
#define cpTcExtraBW "\x70\x0F\x70\x7F\x0F"
#define cpTcExtraMono "\x07\x70\x07\x0F\x70"

TPalette &TcApp::getPalette() const
{
    static TPalette color( cpAppColor cpTcExtra, sizeof( cpAppColor cpTcExtra ) - 1 );
    static TPalette bw( cpAppBlackWhite cpTcExtraBW, sizeof( cpAppBlackWhite cpTcExtraBW ) - 1 );
    static TPalette mono( cpAppMonochrome cpTcExtraMono, sizeof( cpAppMonochrome cpTcExtraMono ) - 1 );
    static TPalette *p[] = { &color, &bw, &mono };
    return *p[appPalette];
}

TcApp::TcApp() :
    TProgInit( &TcApp::initStatusLine, &TcApp::initMenuBar, &TcApp::initDeskTop ),
    TApplication()
{
    TEditor::editorDialog = tcEditorDialog;
    TCommandSet dis;
    dis += cmProgReset; dis += cmGotoCursor; dis += cmTraceInto; dis += cmStepOver;
    dis += cmInspect; dis += cmEvaluate; dis += cmCallStack; dis += cmWatches;
    dis += cmToggleBreak; dis += cmBreakpoints; dis += cmCopyExample;
    disableCommands( dis );
}

void TcApp::writeShellMsg()
{
    printf( "Type EXIT to return to ARM Turbo C. . .\n" );
    fflush( stdout );
}

/* ------------------------------------------------------------ files */

void fullPath( char *out, const char *name )
{
    /* C:\DIR\NAME.EXT, upper case, from a relative name */
    char buf[160];
    if (name[0] && name[1] == ':')
    {
        if (name[2] == '\\' || name[2] == '/')
            strcpy( buf, name );
        else
        {
            char cwd[80];
            int drv = toupper( (uchar) name[0] ) - 'A' + 1;
            if (!_getdcwd( drv, cwd, sizeof cwd ))
                sprintf( cwd, "%c:\\", name[0] );
            snprintf( buf, sizeof buf, "%s%s%s", cwd, cwd[strlen( cwd ) - 1] == '\\' ? "" : "\\", name + 2 );
        }
    }
    else
    {
        char cwd[80];
        if (!getcwd( cwd, sizeof cwd ))
            strcpy( cwd, "C:\\" );
        if (name[0] == '\\' || name[0] == '/')
            snprintf( buf, sizeof buf, "%c:%s", cwd[0], name );
        else
            snprintf( buf, sizeof buf, "%s%s%s", cwd, cwd[strlen( cwd ) - 1] == '\\' ? "" : "\\", name );
    }
    for (char *p = buf; *p; ++p)
    {
        if (*p == '/')
            *p = '\\';
        *p = toupper( (uchar) *p );
    }
    /* fold "\.\" and "\..\" */
    char *p;
    while ((p = strstr( buf, "\\.\\" )) != 0)
        memmove( p, p + 2, strlen( p + 2 ) + 1 );
    while ((p = strstr( buf, "\\..\\" )) != 0)
    {
        char *q = p;
        while (q > buf + 2 && q[-1] != '\\')
            --q;
        if (q > buf + 2)
            --q;
        memmove( q, p + 3, strlen( p + 3 ) + 1 );
    }
    strcpy( out, buf );
}

void TcApp::newFile()
{
    openEditor( 0 );
}

void TcApp::openFile()
{
    char name[MAXPATH] = "*.C";
    if (openFileDialog( name, "Open a File", "*.C" ))
        openEditor( name );
}

void TcApp::saveAll()
{
    TView *first = deskTop->first(), *p = first;
    if (!p) return;
    do
    {
        if (p->helpCtx == hcEditorWin && p != clipWindow)
        {
            TcEditor *e = ((TcEditWindow *) p)->editor;
            if (e->modified)
                e->save();
        }
        p = p->next;
    } while (p != first);
}

/* --------------------------------------------------------- dispatch */

/* F1 in a menu: close the menus (feed them Esc), then show the topic */

static bool isMenuCtx( ushort ctx )
{
    return ctx >= hcSystemMenu;
}

void TcApp::getEvent( TEvent &event )
{
    if (escapingMenus)
    {
        TView *top = TopView();
        if (top && top != this && isMenuCtx( top->getHelpCtx() ))
        {
            event.what = evKeyDown;
            event.keyDown.keyCode = kbEsc;
            event.keyDown.controlKeyState = 0;
            event.keyDown.textLength = 0;
            return;
        }
        escapingMenus = false;
        helpPending = true;         /* shown when idle */
    }
    TApplication::getEvent( event );
    if (event.what == evCommand && event.message.command == cmHelpF1)
    {
        TView *top = TopView();
        ushort ctx = top ? top->getHelpCtx() : hcNoContext;
        if (top != this && isMenuCtx( ctx ))
        {
            escapingMenus = true;
            pendingHelpCtx = ctx;
            event.what = evKeyDown;
            event.keyDown.keyCode = kbEsc;
            event.keyDown.controlKeyState = 0;
            event.keyDown.textLength = 0;
            return;
        }
    }
    /* Alt+Space (the BIOS gives scan 39h, ' '): the system menu */
    if (event.what == evKeyDown && event.keyDown.charScan.scanCode == 0x39 &&
        (event.keyDown.controlKeyState & kbAltShift))
    {
        event.keyDown.keyCode = kbAltSpace;
        event.keyDown.textLength = 0;
    }
    /* F1 anywhere: help for what is selected (menus: the item) */
    if (event.what == evCommand && event.message.command == cmHelpF1)
    {
        helpContext( getHelpCtx() );
        clearEvent( event );
    }
}

/* Copy example: when the help window shows a topic with an example */
void updateMenusState()
{
    static int was = -1;
    int now = 0;
    if (tvHelpIsOpen())
    {
        char *ex = tvHelpExample();
        now = ex != 0;
        free( ex );
    }
    if (now != was)
    {
        if (now)
            TView::enableCommand( cmCopyExample );
        else
            TView::disableCommand( cmCopyExample );
        was = now;
    }
}

void TcApp::idle()
{
    TApplication::idle();
    if (helpPending && TopView() == this)
    {
        helpPending = false;
        helpContext( pendingHelpCtx );
    }
    messageTrack();
    static char lastTopic[40];
    if (strcmp( lastTopic, tvHelpTopic() ))
    {
        snprintf( lastTopic, sizeof lastTopic, "%s", tvHelpTopic() );
        updateMenusState();
    }
}

void TcApp::handleEvent( TEvent &event )
{
    if (event.what == evCommand && event.message.command == cmDosShell)
    {
        /* ours (below), not TApplication's */
        event.message.command = cmDosShellTC;
    }
    TApplication::handleEvent( event );
    if (event.what != evCommand)
        return;
    switch (event.message.command)
    {
    case cmAbout:       aboutBox(); break;
    case cmDosShellTC:
        /* as TApplication::dosShell, and the screen is rewritten after */
        suspend();
        writeShellMsg();
        system( getenv( "COMSPEC" ) );
        resume();
        tvInvalidateScreen();
        redraw();
        break;
    case cmRepaint:     tvInvalidateScreen(); redraw(); break;
    case cmNew:         newFile(); break;
    case cmOpen:        openFile(); break;
    case cmSaveAll:     saveAll(); break;
    case cmChDir:       chdirDialog(); break;
    case cmGetInfo:     infoBox(); break;
    case cmCopyExample:
    {
        char *ex = tvHelpExample();
        if (ex && clipWindow)
        {
            TEditor *c = clipWindow->editor;
            c->lock();
            c->deleteRange( 0, c->bufLen, False );
            c->insertText( ex, (uint) strlen( ex ), True );
            c->unlock();
        }
        free( ex );
        break;
    }
    case cmShowClip:
        if (clipWindow)
        {
            clipWindow->select();
            clipWindow->show();
        }
        break;
    case cmGotoLine:
    {
        TcEditWindow *w = currentEditor();
        int line = 1;
        if (w && gotoLineDialog( line ))
            w->editor->gotoLine( line, false );
        break;
    }
    case cmPrevError:   messageGo( -1 ); break;
    case cmNextError:   messageGo( 1 ); break;
    case cmLocateFunc:
    {
        TcEditWindow *w = currentEditor();
        char name[64] = "";
        if (w)
            w->editor->wordAtCursor( name, sizeof name );
        if (w && inputBox( "Locate Function", "~F~unction name", name, sizeof name - 1 ) == cmOK && name[0])
        {
            /* the definition: "name(" at the start of a line or after a type */
            TcEditor *e = w->editor;
            char pat[70];
            snprintf( pat, sizeof pat, "%s(", name );
            uint save = e->curPtr;
            e->lock();
            e->setCurPtr( 0, 0 );
            bool found = false;
            while (e->search( pat, efCaseSensitive | efWholeWordsOnly ))
            {
                /* a definition: the line does not end in ';' and is not indented */
                uint ls = e->lineStart( e->curPtr ), le = e->lineEnd( e->curPtr );
                char first = e->bufChar( ls );
                char last = le > ls ? e->bufChar( le - 1 ) : 0;
                if (first != ' ' && first != '\t' && last != ';')
                {
                    found = true;
                    break;
                }
                e->setCurPtr( e->selEnd, 0 );
            }
            if (!found)
            {
                e->setCurPtr( save, 0 );
                e->unlock();
                messageBox( mfError | mfOKButton, "Function %s not found.", name );
            }
            else
            {
                e->trackCursor( True );
                e->unlock();
            }
        }
        break;
    }
    case cmRun:         buildRun(); break;
    case cmArguments:   argumentsDialog(); break;
    case cmCompileObj:  buildCompile( true ); break;
    case cmMake:        buildMake( false, false ); break;
    case cmLink:        buildLinkOnly(); break;
    case cmBuildAll:    buildMake( true, false ); break;
    case cmInformation: infoBox(); break;
    case cmRemoveMsgs:  messagesClear(); break;
    case cmOpenProject:
    {
        char name[MAXPATH] = "*.PRJ";
        if (openFileDialog( name, "Open Project File", "*.PRJ" ))
            projectOpen( name );
        break;
    }
    case cmCloseProject: projectClose(); break;
    case cmAddItem:
        if (!projectName[0])
            messageBox( "Open or create a project first (Project / Open project).", mfInformation | mfOKButton );
        else
            projectAddDialog();
        break;
    case cmOptCompiler: compilerDialog(); break;
    case cmOptMake:     makeDialog(); break;
    case cmOptLinker:   linkerDialog(); break;
    case cmOptDirs:     dirsDialog(); break;
    case cmOptEnv:      envDialog(); break;
    case cmOptSave:
        saveConfig();
        messageBox( mfInformation | mfOKButton, "Options saved to %s.", configPath );
        break;
    case cmMessageWin:  messageShow( true ); break;
    case cmProjectWin:  projectWindowShow(); break;
    case cmUserScreen:  userScreen(); break;
    case cmListAll:     listAllDialog(); break;
    case cmHelpContents: helpShow( "Contents" ); break;
    case cmHelpIndex:   helpIndex(); break;
    case cmHelpTopic:
    {
        char word[40] = "";
        TcEditWindow *w = currentEditor();
        if (w)
            w->editor->wordAtCursor( word, sizeof word );
        helpTopicSearch( word );
        break;
    }
    case cmHelpPrev:    helpPrevious(); break;
    case cmHelpHelp:    helpShow( "Help on help" ); break;
    case cmPrint:
    {
        TcEditWindow *w = currentEditor();
        if (!w) break;
        int fd = open( "PRN", O_WRONLY | O_BINARY );
        if (fd < 0)
        {
            messageBox( "Printer not ready.", mfError | mfOKButton );
            break;
        }
        TcEditor *e = w->editor;
        char buf[256];
        uint n = 0;
        for (uint i = 0; i < e->bufLen; ++i)
        {
            char c = e->bufChar( i );
            buf[n++] = c;
            if (n == sizeof buf)
            {
                ::write( fd, buf, n );
                n = 0;
            }
        }
        if (n)
            ::write( fd, buf, n );
        ::write( fd, "\r\n\f", 3 );
        ::close( fd );
        break;
    }
    default:
        return;
    }
    clearEvent( event );
}

/* ------------------------------------------------------ configuration */

static char **pendingFiles;
static int nPending;

bool loadConfig()
{
    FILE *f = 0;
    if (access( "TCCONFIG.TC", 0 ) == 0)
    {
        fullPath( configPath, "TCCONFIG.TC" );
        f = fopen( configPath, "r" );
    }
    if (!f)
    {
        snprintf( configPath, sizeof configPath, "%s\\TCCONFIG.TC", tcHome );
        f = fopen( configPath, "r" );
    }
    if (!f)
        return false;
    char line[200];
    while (fgets( line, sizeof line, f ))
    {
        char *e = line + strlen( line );
        while (e > line && (e[-1] == '\n' || e[-1] == '\r'))
            *--e = 0;
        char *v = strchr( line, '=' );
        if (!v)
            continue;
        *v++ = 0;
        if (!strcmp( line, "include" )) snprintf( opts.incDirs, sizeof opts.incDirs, "%s", v );
        else if (!strcmp( line, "library" )) snprintf( opts.libDirs, sizeof opts.libDirs, "%s", v );
        else if (!strcmp( line, "output" )) snprintf( opts.outDir, sizeof opts.outDir, "%s", v );
        else if (!strcmp( line, "defines" )) snprintf( opts.defines, sizeof opts.defines, "%s", v );
        else if (!strcmp( line, "warnings" )) opts.warnings = atoi( v );
        else if (!strcmp( line, "warnerror" )) opts.warnError = atoi( v );
        else if (!strcmp( line, "stack" )) opts.stackKB = atoi( v );
        else if (!strcmp( line, "makebreak" )) opts.makeBreak = atoi( v );
        else if (!strcmp( line, "env" )) opts.envFlags = atoi( v );
        else if (!strcmp( line, "tabsize" )) opts.tabSize = atoi( v );
        else if (!strcmp( line, "args" )) snprintf( opts.args, sizeof opts.args, "%s", v );
        else if (!strcmp( line, "project" )) snprintf( projectName, sizeof projectName, "%s", v );
        else if (!strcmp( line, "file" ) && nPending < 16)
        {
            pendingFiles = (char **) realloc( pendingFiles, (nPending + 1) * sizeof( char * ) );
            pendingFiles[nPending++] = strdup( v );
        }
    }
    fclose( f );
    return true;
}

void saveConfig()
{
    FILE *f = fopen( configPath, "w" );
    if (!f)
        return;
    fprintf( f, "; ARM Turbo C configuration\n" );
    fprintf( f, "include=%s\nlibrary=%s\noutput=%s\ndefines=%s\n", opts.incDirs, opts.libDirs, opts.outDir, opts.defines );
    fprintf( f, "warnings=%u\nwarnerror=%u\nstack=%u\nmakebreak=%u\nenv=%u\ntabsize=%u\nargs=%s\n",
             opts.warnings, opts.warnError, opts.stackKB, opts.makeBreak, opts.envFlags, opts.tabSize, opts.args );
    if (projectName[0])
        fprintf( f, "project=%s\n", projectName );
    /* the desktop: the open edit windows, back to front */
    if (TProgram::deskTop)
    {
        TView *first = TProgram::deskTop->first(), *p = first;
        char *names[32];
        int n = 0;
        if (p)
            do
            {
                TcEditWindow *w = p->helpCtx == hcEditorWin ? (TcEditWindow *) p : 0;
                if (w && w != clipWindow && w->editor->fileName[0] && n < 32)
                    names[n++] = w->editor->fileName;
                p = p->next;
            } while (p != first);
        while (n > 0)
            fprintf( f, "file=%s\n", names[--n] );
    }
    fclose( f );
}

/* TCC.EXE's rule: %TCCDIR%, else next to the program if it has
 * INCLUDE\STDIO.H, else C:\TC */
static void findHome( const char *argv0 )
{
    const char *e = getenv( "TCCDIR" );
    if (e && *e)
    {
        snprintf( tcHome, sizeof tcHome, "%s", e );
    }
    else if (argv0 && *argv0)
    {
        char dir[80], probe[100];
        snprintf( dir, sizeof dir, "%s", argv0 );
        char *s = strrchr( dir, '\\' );
        if (s)
        {
            *s = 0;
            snprintf( probe, sizeof probe, "%s\\INCLUDE\\STDIO.H", dir );
            if (access( probe, 0 ) == 0)
                snprintf( tcHome, sizeof tcHome, "%s", dir );
        }
    }
    size_t n = strlen( tcHome );
    if (n > 3 && tcHome[n - 1] == '\\')
        tcHome[n - 1] = 0;
    snprintf( opts.incDirs, sizeof opts.incDirs, "%s\\INCLUDE", tcHome );
    snprintf( opts.libDirs, sizeof opts.libDirs, "%s\\LIB", tcHome );
}

int main( int argc, char **argv )
{
    findHome( argc > 0 ? argv[0] : 0 );
    bool haveConfig = loadConfig();
    if (!haveConfig)
        snprintf( configPath, sizeof configPath, "%s\\TCCONFIG.TC", tcHome );

    TcApp *app = new TcApp;
    tcApp = app;
    tvHighlight = highlightC;
    if (opts.envFlags & envBackup)
        TEditor::editorFlags |= efBackupFiles;
    else
        TEditor::editorFlags &= ~efBackupFiles;

    /* the clipboard: a hidden edit window */
    clipWindow = openEditor( 0, false );
    if (clipWindow)
        TEditor::clipboard = clipWindow->editor;

    if (projectName[0])
        projectOpen( projectName );

    bool opened = false;
    for (int i = 1; i < argc; ++i)
    {
        const char *a = argv[i];
        const char *dot = strrchr( a, '.' );
        if (dot && !strcasecmp( dot, ".PRJ" ))
        {
            projectOpen( a );
            opened = true;
        }
        else
        {
            char name[MAXPATH];
            snprintf( name, sizeof name, "%s", a );
            if (!strchr( name, '.' ))
                strcat( name, ".C" );
            openEditor( name );
            opened = true;
        }
    }
    if (!opened)
    {
        if (haveConfig)
            for (int i = 0; i < nPending; ++i)
            {
                if (access( pendingFiles[i], 0 ) == 0)
                {
                    openEditor( pendingFiles[i] );
                    opened = true;
                }
            }
        if (!opened && !haveConfig)
        {
            /* first start: the classic */
            char hello[100];
            snprintf( hello, sizeof hello, "%s\\SAMPLES\\HELLO.C", tcHome );
            if (access( hello, 0 ) == 0)
                openEditor( hello );
        }
    }
    msgWindow = new TcMessageWindow( TRect( 0, 17, 80, 23 ) );
    msgWindow->hide();
    TProgram::deskTop->insert( msgWindow );
    msgWindow->putInFrontOf( TProgram::deskTop->first() );
    {
        /* the editor on top (or the Project window with a project and no file) */
        TcEditWindow *w = currentEditor();
        if (w)
            w->select();
        else if (projectName[0])
            projectWindowShow();
    }

    app->run();
    if (opts.envFlags & envAutoSave)
        saveConfig();
    TObject::destroy( app );
    return 0;
}
