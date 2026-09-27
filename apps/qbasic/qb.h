/*
 * qb.h - QB.EXE, "ARM QuickBASIC": a QBasic-style BASIC environment on
 * Turbo Vision (the look of the ARM-DOS Editor, apps/edit) with Bywater
 * BASIC (apps/basic, GPL-2; our QuickBASIC-flavoured copy in bw/) as the
 * interpreter, linked in. Shared declarations.
 *
 * Copyright (c) 2026 Europa Micro Systems. MIT License (the IDE; the
 * interpreter in bw/ is GPL-2, and so is QB.EXE as a whole).
 */
#ifndef QB_H
#define QB_H

#define Uses_TApplication
#define Uses_TProgram
#define Uses_TWindow
#define Uses_TFrame
#define Uses_TScrollBar
#define Uses_TEditor
#define Uses_TFileEditor
#define Uses_TDialog
#define Uses_TButton
#define Uses_TInputLine
#define Uses_TLabel
#define Uses_TStaticText
#define Uses_TCheckBoxes
#define Uses_TRadioButtons
#define Uses_TSItem
#define Uses_TListBox
#define Uses_TListViewer
#define Uses_TStringCollection
#define Uses_TMenuBar
#define Uses_TMenuBox
#define Uses_TSubMenu
#define Uses_TMenuItem
#define Uses_TMenu
#define Uses_TStatusLine
#define Uses_TStatusItem
#define Uses_TStatusDef
#define Uses_TKeys
#define Uses_TEvent
#define Uses_TDeskTop
#define Uses_TPalette
#define Uses_TDrawBuffer
#define Uses_TRect
#define Uses_TCommandSet
#define Uses_TScreen
#define Uses_TGroup
#define Uses_TView
#define Uses_TText
#define Uses_TMouse
#include <tvision/tv.h>

#include "tvlib.h"

/* ------------------------------------------------------------ commands */
enum
{
    cmPrint         = 200,
    cmDisplay,
    cmHelpPath,
    cmSyntaxCheck,
    cmNewSub, cmNewFunction,
    cmSubs, cmSplit, cmOutputScreen,
    cmStart, cmRestart, cmContinue,
    cmStep, cmProcStep, cmTraceOn, cmToggleBreak, cmClearBreaks, cmSetNext,
    cmHelpIndex, cmHelpContents, cmHelpTopicCmd, cmHelpUsing, cmHelpAbout,
    cmHelpTopic,            /* show infoPtr's topic */
    cmHelpClose,
    cmFindVerify,           /* Change dialog buttons */
    cmChangeAll,
    cmSkip,
    cmDlgHelp,              /* a dialog's < Help > button */
    cmEditorMoved,
    cmImmediateExec,
};

/* ------------------------------------------------------- help contexts
 * (the status line shows the hint of the menu item under the cursor) */
enum
{
    hcEditor = 1,
    hcImmediate,
    hcMenuBar,
    hcDialog,
    hcHelp,
    hcFileMenu = 100, hxNew, hxOpen, hxSave, hxSaveAs, hxPrint, hxExit,
    hcEditMenu = 110, hxCut, hxCopy, hxPaste, hxClear, hxNewSub, hxNewFunction,
    hcViewMenu = 120, hxSubs, hxSplit, hxOutput,
    hcSearchMenu = 130, hxFind, hxRepeat, hxChange,
    hcRunMenu = 140, hxStart, hxRestart, hxContinue,
    hcDebugMenu = 150, hxStep, hxProcStep, hxTrace, hxToggleBreak, hxClearBreaks, hxSetNext,
    hcOptionsMenu = 160, hxDisplay, hxHelpPath, hxSyntax,
    hcHelpMenu = 170, hxIndex, hxContents, hxTopic, hxUsing, hxAbout,
    hcDlgFind = 200, hcDlgChange, hcDlgOpen, hcDlgSaveAs, hcDlgPrint,
    hcDlgDisplay, hcDlgHelpPath, hcDlgVerify, hcDlgSaveChanges, hcDlgMessage,
    hcDlgSubs, hcDlgNewSub, hcDlgError,
};

/* The synthetic key the platform layer sends when Alt is pressed and
 * released on its own ("Press ALT to choose commands"). */
const ushort kbAltAlone = 0xFE00;

/* ----------------------------------------------------- application palette
 * Turbo Vision's layout for 1-135 (see app.h), plus entries of our own. */
enum
{
    apStatus = 136,         /* status line: black on cyan */
    apStatusHi,             /* status line: position field */
    apHelpText,             /* help window text */
    apHelpLink,             /* help window: a cross reference */
    apHelpLinkSel,          /* help window: the selected one */
    apEnd
};

extern uchar qbPalette[apEnd];

/* Display options (Options / Display...) */
struct DisplayOptions
{
    uchar normalFg, normalBg;
    uchar highFg, highBg;
    bool scrollBars;
    int tabStops;
};
extern DisplayOptions displayOpts;
void applyDisplayOptions();

/* ------------------------------------------------------------ views */
class QbEditor : public TFileEditor
{
public:
    QbEditor( const TRect &bounds, TScrollBar *h, TScrollBar *v, const char *fileName );
    virtual void handleEvent( TEvent &event );
    virtual void updateCommands();
    void searchReplace( bool replace );
    void gotoLine( int line, bool select );
    int lineNumber( uint ptr );             /* 1-based */
    uint lineStartOf( int line );           /* the buffer pointer of line n */
    void wordAtCursor( char *buf, int size );
    void recaseLine( uint ptr );            /* keywords upper case */
    virtual Boolean valid( ushort command )
    {
        /* the Immediate window is never saved */
        return isImmediate ? True : TFileEditor::valid( command );
    }
    bool isImmediate = false;
};

class QbWindow : public TWindow
{
public:
    QbWindow( const TRect &bounds, const char *fileName, bool immediate );
    virtual TPalette &getPalette() const;
    virtual const char *getTitle( short maxSize );
    virtual void sizeLimits( TPoint &min, TPoint &max );
    virtual void handleEvent( TEvent &event );
    virtual void close() {}
    void showScrollBars( bool on );
    static TFrame *initFrame( TRect );

    QbEditor *editor;
    TScrollBar *hScroll, *vScroll;
    bool immediate;
};

/* QBasic-style dialog pieces (as the ARM-DOS Editor's) */
class EdDialog : public TDialog
{
public:
    EdDialog( const TRect &bounds, TStringView title, ushort helpCtx );
    virtual TPalette &getPalette() const;
    virtual void handleEvent( TEvent &event );
    static TFrame *initFrame( TRect );
    void separator( int y );
    void box( const TRect &r, const char *title = 0 );
};

class EdButton : public TButton
{
public:
    EdButton( const TRect &bounds, TStringView title, ushort command, ushort flags );
    virtual void draw();
    virtual void drawState( Boolean down );
    static int width( TStringView title ) { return cstrlen( title ) + 4; }
};

/* qbdlg.cpp */
ushort edExecDialog( TDialog *d, void *data );
ushort edMessage( const char *text, ushort buttons, ushort helpCtx = hcDlgMessage );
enum { emOK = 1, emYesNoCancel = 2, emOKCancel = 3 };
ushort doEditDialog( int dialog, ... );
bool openDialog( char *fileName );
bool saveAsDialog( char *fileName );
int printDialog( bool haveSelection );      /* 0 cancel, 1 selection, 2 all */
bool displayDialog();
bool helpPathDialog( char *path );
void aboutDialog();
bool welcomeDialog();
bool newProcDialog( bool function, char *name, int size );
int subsDialog( const char *const *names, int n, int current );   /* -1 cancel */

/* qbhelp.cpp */
void helpOpen( const char *topic );
void helpModal( const char *topic );
const char *helpTopicFor( ushort helpCtx );
void helpClose();
bool helpIsOpen();
bool helpKeyword( const char *word );     /* F1 on a keyword */

/* qbrun.cpp */
void runStart( bool restart );              /* Shift+F5 */
void runContinue();                         /* F5 */
void runStep( int mode );                   /* F8 = 1, F10 = 2 */
void runImmediate( const char *line );
void runOutputScreen();                     /* F4 */
void runReset();                            /* the program was edited */
void runToggleBreak();
void runClearBreaks();
void runSetNext();
bool runSuspended();
extern bool traceOn;
uint8_t runLineAttr( TEditor *ed, unsigned linePtr );
void runEditorChanged();

/* qb.cpp */
extern QbWindow *docWindow, *immWindow;
void layoutWindows();
bool printText( const char *p, uint len );
bool isKeyword( const char *w, int n, const char **canonical );
void subsList();                            /* F2 */
extern char helpPath[80];

#endif
