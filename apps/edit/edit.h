/*
 * edit.h - EDIT.EXE, the ARM-DOS Editor (an MS-DOS 5 EDIT look-alike built
 * on Turbo Vision). Shared declarations.
 *
 * Copyright (c) 2026 Europa Micro Systems. MIT License.
 */
#ifndef EDIT_H
#define EDIT_H

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

/* ------------------------------------------------------------ commands */
enum
{
    cmPrint         = 200,
    cmDisplay,
    cmHelpPath,
    cmHelpGetStarted,
    cmHelpKeyboard,
    cmHelpAbout,
    cmHelpTopic,            /* F1: help on the current menu item / dialog */
    cmHelpClose,
    cmFindVerify,           /* Change dialog buttons */
    cmChangeAll,
    cmSkip,
    cmDlgHelp,              /* a dialog's < Help > button */
    cmEditorMoved,
};

/* ------------------------------------------------------- help contexts
 * (the status line shows the hint of the menu item under the cursor) */
enum
{
    hcEditor = 1,
    hcMenuBar,
    hcDialog,
    hcHelp,
    hcFileMenu = 100, hxNew, hxOpen, hxSave, hxSaveAs, hxPrint, hxExit,
    hcEditMenu = 110, hxCut, hxCopy, hxPaste, hxClear,
    hcSearchMenu = 120, hxFind, hxRepeat, hxChange,
    hcOptionsMenu = 130, hxDisplay, hxHelpPath,
    hcHelpMenu = 140, hxGetStarted, hxKeyboard, hxAbout,
    hcDlgFind = 150, hcDlgChange, hcDlgOpen, hcDlgSaveAs, hcDlgPrint,
    hcDlgDisplay, hcDlgHelpPath, hcDlgVerify, hcDlgSaveChanges, hcDlgMessage,
};

/* The synthetic key the platform layer sends when Alt is pressed and
 * released on its own (EDIT: "Press ALT to choose commands"). */
const ushort kbAltAlone = 0xFE00;

/* ----------------------------------------------------- application palette
 * Turbo Vision's layout for 1-135 (see app.h), plus entries of our own. */
enum
{
    apStatus = 136,         /* status line: black on cyan */
    apStatusHi,             /* status line: position field */
    apHelpText,             /* help window text */
    apHelpLink,             /* help window: a ◄link► */
    apHelpLinkSel,          /* help window: the link under the cursor */
    apEnd
};

extern uchar edPalette[apEnd];

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
class EdEditor;
class EdWindow;

/* The editor's document window: the whole screen between the menu bar and
 * the status line, frame and scroll bars drawn like EDIT's. */
class EdWindow : public TWindow
{
public:
    EdWindow( const TRect &bounds, const char *fileName );
    virtual TPalette &getPalette() const;
    virtual const char *getTitle( short maxSize );
    virtual void sizeLimits( TPoint &min, TPoint &max );
    virtual void changeBounds( const TRect &bounds );
    virtual void handleEvent( TEvent &event );
    void showScrollBars( bool on );
    static TFrame *initFrame( TRect );

    EdEditor *editor;
    TScrollBar *hScroll, *vScroll;
};

class EdEditor : public TFileEditor
{
public:
    EdEditor( const TRect &bounds, TScrollBar *h, TScrollBar *v, const char *fileName );
    virtual void handleEvent( TEvent &event );
    virtual void updateCommands();
    void searchReplace( bool replace );
};

/* EDIT-style dialog pieces */
class EdDialog : public TDialog
{
public:
    EdDialog( const TRect &bounds, TStringView title, ushort helpCtx );
    virtual TPalette &getPalette() const;
    virtual void handleEvent( TEvent &event );
    static TFrame *initFrame( TRect );
    /* a separator line (├───┤) across the dialog at row y */
    void separator( int y );
    /* a single-line box around the rectangle r (the box is outside r) */
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

/* dialogs.cpp */
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

/* help.cpp */
void helpOpen( const char *topic );         /* help window above the document */
void helpModal( const char *topic );        /* on top of a dialog */
const char *helpTopicFor( ushort helpCtx );
void helpClose();
bool helpIsOpen();
void helpRedraw();
extern char helpPath[80];

/* edit.cpp */
extern EdWindow *docWindow;
void layoutWindows();
bool printText( const char *p, uint len );

#endif
