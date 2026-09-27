/*
 * tc.h - TC.EXE, "ARM Turbo C": an integrated C development environment in
 * the style of the 1990 Turbo C++ IDE, built on Turbo Vision with TinyCC
 * (libtcc) as the integrated compiler. Shared declarations.
 *
 * Copyright (c) 2026 Europa Micro Systems. MIT License.
 */
#ifndef TC_H
#define TC_H

#define Uses_TApplication
#define Uses_TProgram
#define Uses_TWindow
#define Uses_TFrame
#define Uses_TScrollBar
#define Uses_TIndicator
#define Uses_TEditor
#define Uses_TFileEditor
#define Uses_TDialog
#define Uses_TButton
#define Uses_TInputLine
#define Uses_THistory
#define Uses_TLabel
#define Uses_TStaticText
#define Uses_TParamText
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
#define Uses_TBackground
#define Uses_TPalette
#define Uses_TDrawBuffer
#define Uses_TRect
#define Uses_TCommandSet
#define Uses_TScreen
#define Uses_TGroup
#define Uses_TView
#define Uses_TText
#define Uses_TMouse
#define Uses_TScroller
#define Uses_MsgBox
#define Uses_TFileDialog
#define Uses_TChDirDialog
#include <tvision/tv.h>

#include "tvlib.h"

/* ------------------------------------------------------------ commands */
enum
{
    cmAbout = 200, cmRepaint,
    cmPrint, cmGetInfo, cmDosShellTC,
    cmRestoreLine, cmCopyExample, cmShowClip,
    cmFindTC, cmReplaceTC, cmSearchAgainTC, cmGotoLine, cmPrevError, cmNextError, cmLocateFunc,
    cmRun, cmProgReset, cmGotoCursor, cmTraceInto, cmStepOver, cmArguments,
    cmCompileObj, cmMake, cmLink, cmBuildAll, cmInformation, cmRemoveMsgs,
    cmInspect, cmEvaluate, cmCallStack, cmWatches, cmToggleBreak, cmBreakpoints,
    cmOpenProject, cmCloseProject, cmAddItem, cmDeleteItem,
    cmOptCompiler, cmOptMake, cmOptLinker, cmOptDirs, cmOptEnv, cmOptSave,
    cmMessageWin, cmProjectWin, cmUserScreen, cmListAll,
    cmHelpContents, cmHelpIndex, cmHelpTopic, cmHelpPrev, cmHelpHelp, cmHelpF1,
    cmMsgGoto,          /* broadcast: the message list wants a source line */
    cmEditorMoved,
};

/* ------------------------------------------------------- help contexts
 * (the status line shows the hint for the highlighted menu item; F1 shows
 * the help topic of the same name) */
enum
{
    hcEditorWin = 10, hcMessageWin, hcProjectWin, hcHelpWin, hcDialog,
    hcSystemMenu = 100, hxAbout, hxRepaint,
    hcFileMenu = 110, hxOpen, hxNew, hxSave, hxSaveAs, hxSaveAll, hxChDir, hxPrint, hxGetInfo, hxDosShell, hxQuit,
    hcEditMenu = 130, hxRestore, hxCut, hxCopy, hxPaste, hxCopyExample, hxShowClip, hxClear,
    hcSearchMenu = 140, hxFind, hxReplace, hxSearchAgain, hxGotoLine, hxPrevError, hxNextError, hxLocateFunc,
    hcRunMenu = 150, hxRun, hxProgReset, hxGotoCursor, hxTrace, hxStep, hxArguments,
    hcCompileMenu = 160, hxCompileObj, hxMake, hxLink, hxBuildAll, hxInformation, hxRemoveMsgs,
    hcDebugMenu = 170, hxInspect, hxEvaluate, hxCallStack, hxWatches, hxToggleBreak, hxBreakpoints,
    hcProjectMenu = 180, hxOpenProject, hxCloseProject, hxAddItem, hxDeleteItem,
    hcOptionsMenu = 190, hxOptCompiler, hxOptMake, hxOptLinker, hxOptDirs, hxOptEnv, hxOptSave,
    hcWindowMenu = 200, hxSizeMove, hxZoom, hxTile, hxCascade, hxNext, hxClose, hxMessage, hxProjectW, hxUserScreen, hxListAll,
    hcHelpMenu = 220, hxContents, hxIndex, hxTopicSearch, hxPrevTopic, hxHelpOnHelp, hxAboutH,
};

/* ------------------------------------------------------------ options */
struct TcOptions
{
    char incDirs[128];      /* Options / Directories */
    char libDirs[128];
    char outDir[80];
    char defines[80];       /* Options / Compiler */
    ushort warnings;        /* 0 none, 1 standard, 2 all */
    ushort warnError;       /* check box: treat warnings as errors */
    ushort stackKB;         /* Options / Linker */
    ushort makeBreak;       /* Options / Make: 0 errors, 1 warnings */
    ushort envFlags;        /* Options / Environment check boxes */
    ushort tabSize;
    char args[80];          /* Run / Arguments */
};
enum { envBackup = 1, envAutoSave = 2, envHighlight = 4, envAutoIndent = 8 };
extern TcOptions opts;
extern char tcHome[80];      /* C:\TC (where TC.EXE's INCLUDE\ LIB\ are) */
extern char configPath[90];
bool loadConfig();
void saveConfig();

/* ------------------------------------------------------------ views */
class TcEditor : public TFileEditor
{
public:
    TcEditor( const TRect &bounds, TScrollBar *h, TScrollBar *v, TIndicator *ind, TStringView fileName );
    virtual void handleEvent( TEvent &event );
    virtual void updateCommands();
    void gotoLine( int line, bool mark );
    int lineNumber( uint ptr );
    void wordAtCursor( char *buf, int size );
    void searchAgain( bool replace );
};

class TcEditWindow : public TWindow
{
public:
    TcEditWindow( const TRect &bounds, TStringView fileName, int number );
    virtual void close();
    virtual const char *getTitle( short );
    virtual void handleEvent( TEvent & );
    virtual void sizeLimits( TPoint &min, TPoint &max );
    TcEditor *editor;
};

/* the Message window */
struct TcMessage
{
    char kind;              /* 'E' error, 'W' warning, ' ' information */
    char file[80];
    int line;
    char text[160];
};

class TcMessageList;
class TcMessageWindow : public TWindow
{
public:
    TcMessageWindow( const TRect &r );
    virtual TPalette &getPalette() const;
    virtual void close();
    TcMessageList *list;
};

/* tcwin.cpp */
extern TcMessageWindow *msgWindow;
extern TcEditWindow *clipWindow;
TcEditWindow *openEditor( const char *fileName, bool visible = true );
TcEditWindow *findEditor( const char *fileName );
TcEditWindow *currentEditor();
void messagesClear();
void messageAdd( char kind, const char *file, int line, const char *text );
int messageCount();
int messageErrors();
void messageShow( bool focus );
void messageSelectFirst();
void messageGo( int dir );          /* previous/next error */
bool messageGoto( int index, bool focusEditor );
void messageTrack();
bool highlightC( TEditor *ed, unsigned linePtr, unsigned len, uint8_t *attrs, uint8_t normal );
void tileWindows();

/* project (tcdlg.cpp) */
extern char projectName[80];        /* "" = none */
extern TStringCollection *projectItems;
void projectOpen( const char *name );
void projectClose();
void projectSave();
void projectAdd( const char *file );
void projectWindowShow();

/* dialogs (tcdlg.cpp) */
ushort tcEditorDialog( int dialog, ... );
bool openFileDialog( char *name, const char *title, const char *wild );
void aboutBox();
bool gotoLineDialog( int &line );
bool argumentsDialog();
bool compilerDialog();
bool makeDialog();
bool linkerDialog();
bool dirsDialog();
bool envDialog();
void chdirDialog();
void infoBox();
bool projectAddDialog();
void listAllDialog();

/* build (tcbuild.cpp) */
int buildCompile( bool objOnly );   /* current file to .O */
int buildMake( bool all, bool quiet ); /* 0 ok, 1 errors, -1 nothing to do */
void buildRun();
void buildLinkOnly();
void userScreen();
extern bool buildDirty;             /* something changed since the last make */
extern long lastLines;
extern unsigned long lastExeSize;
void targetName( char *exe, const char *ext );

/* help (tchelp.cpp) */
void helpShow( const char *topic );
void helpContext( ushort ctx );
void helpIndex();
void helpTopicSearch( const char *word );
void helpPrevious();
const char *helpTopicForCtx( ushort ctx );

/* tc.cpp */
class TcApp;
extern TcApp *tcApp;
const char *ctxHint( ushort ctx );
void updateMenusState();
void fullPath( char *out, const char *name );

#endif
