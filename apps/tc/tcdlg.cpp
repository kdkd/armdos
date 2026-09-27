/*
 * tcdlg.cpp - TC.EXE's dialog boxes (Turbo Vision's standard look, as in
 * Turbo C++): the editor's prompts, Find/Replace, Go to line, the Options
 * dialogs, About, Get info, Change dir, List all, and the Project.
 *
 * Copyright (c) 2026 Europa Micro Systems. MIT License.
 */
#include "tc.h"

#define Uses_TFindDialogRec
#define Uses_TReplaceDialogRec
#include <tvision/tv.h>

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <direct.h>
#include <malloc.h>
#include <sys/stat.h>

char projectName[80];
TStringCollection *projectItems;

static ushort execDialog( TDialog *d, void *data )
{
    TView *p = TProgram::application->validView( d );
    if (!p)
        return cmCancel;
    if (data)
        p->setData( data );
    ushort result = TProgram::deskTop->execView( p );
    if (result != cmCancel && data)
        p->getData( data );
    TObject::destroy( p );
    return result;
}

static TDialog *newDialog( int w, int h, const char *title )
{
    TDialog *d = new TDialog( TRect( 0, 0, w, h ), title );
    d->options |= ofCentered;
    d->helpCtx = hcDialog;
    return d;
}

static void okCancel( TDialog *d, int y, bool help = false )
{
    int w = d->size.x;
    int x = help ? (w - 38) / 2 : (w - 24) / 2;
    d->insert( new TButton( TRect( x, y, x + 10, y + 2 ), "O~K~", cmOK, bfDefault ) );
    d->insert( new TButton( TRect( x + 12, y, x + 22, y + 2 ), "Cancel", cmCancel, bfNormal ) );
    if (help)
        d->insert( new TButton( TRect( x + 24, y, x + 34, y + 2 ), "~H~elp", cmHelpF1, bfNormal ) );
}

/* ------------------------------------------------------- editor prompts */

static TDialog *findDialogBox( bool replace )
{
    TDialog *d = newDialog( 40, replace ? 16 : 12, replace ? "Replace" : "Find" );
    TInputLine *find = new TInputLine( TRect( 3, 3, 34, 4 ), maxFindStrLen );
    d->insert( find );
    d->insert( new TLabel( TRect( 2, 2, 20, 3 ), "~T~ext to find", find ) );
    d->insert( new THistory( TRect( 34, 3, 37, 4 ), find, 10 ) );
    int y = 5;
    if (replace)
    {
        TInputLine *rep = new TInputLine( TRect( 3, 6, 34, 7 ), maxReplaceStrLen );
        d->insert( rep );
        d->insert( new TLabel( TRect( 2, 5, 20, 6 ), "~N~ew text", rep ) );
        d->insert( new THistory( TRect( 34, 6, 37, 7 ), rep, 11 ) );
        y = 8;
    }
    TSItem *items = new TSItem( "~C~ase sensitive", new TSItem( "~W~hole words only",
                     replace ? new TSItem( "~P~rompt on replace", new TSItem( "~R~eplace all", 0 ) ) : 0 ) );
    d->insert( new TCheckBoxes( TRect( 3, y + 1, 37, y + 1 + (replace ? 4 : 2) ), items ) );
    d->insert( new TLabel( TRect( 2, y, 12, y + 1 ), "Options", 0 ) );
    okCancel( d, replace ? 13 : 9 );
    d->selectNext( False );
    return d;
}

ushort tcEditorDialog( int dialog, ... )
{
    va_list ap;
    va_start( ap, dialog );
    ushort r = cmCancel;
    switch (dialog)
    {
    case edOutOfMemory:
        r = messageBox( "Not enough memory for this operation.", mfError | mfOKButton );
        break;
    case edReadError:
        r = messageBox( mfError | mfOKButton, "Error reading file %s.", va_arg( ap, char * ) );
        break;
    case edWriteError:
        r = messageBox( mfError | mfOKButton, "Error writing file %s.", va_arg( ap, char * ) );
        break;
    case edCreateError:
        r = messageBox( mfError | mfOKButton, "Error creating file %s.", va_arg( ap, char * ) );
        break;
    case edSaveModify:
        r = messageBox( mfInformation | mfYesNoCancel, "%s has been modified. Save?", va_arg( ap, char * ) );
        break;
    case edSaveUntitled:
        r = messageBox( "Save untitled file?", mfInformation | mfYesNoCancel );
        break;
    case edSaveAs:
    {
        char *name = va_arg( ap, char * );
        TFileDialog *d = new TFileDialog( "*.C", "Save File As", "~N~ame", fdOKButton, 101 );
        r = execDialog( d, name );
        break;
    }
    case edFind:
        r = execDialog( findDialogBox( false ), va_arg( ap, void * ) );
        break;
    case edSearchFailed:
        r = messageBox( "Search string not found.", mfError | mfOKButton );
        break;
    case edReplace:
        r = execDialog( findDialogBox( true ), va_arg( ap, void * ) );
        break;
    case edReplacePrompt:
    {
        /* keep the box away from the found text */
        TPoint *c = va_arg( ap, TPoint * );
        TRect rr( 0, 1, 40, 8 );
        rr.move( (TProgram::deskTop->size.x - rr.b.x) / 2, 0 );
        TPoint t = TProgram::deskTop->makeGlobal( rr.b );
        t.y++;
        if (c->y <= t.y)
            rr.move( 0, TProgram::deskTop->size.y - rr.b.y - 2 );
        r = messageBoxRect( rr, "Replace this occurrence?", mfYesNoCancel | mfInformation );
        break;
    }
    }
    va_end( ap );
    return r;
}

bool openFileDialog( char *name, const char *title, const char *wild )
{
    TFileDialog *d = new TFileDialog( wild, title, "~N~ame", fdOpenButton, 100 );
    return execDialog( d, name ) != cmCancel;
}

bool gotoLineDialog( int &line )
{
    TDialog *d = newDialog( 36, 8, "Go to Line Number" );
    TInputLine *in = new TInputLine( TRect( 3, 3, 14, 4 ), 7 );
    d->insert( in );
    d->insert( new TLabel( TRect( 2, 2, 30, 3 ), "~E~nter new line number", in ) );
    d->insert( new THistory( TRect( 14, 3, 17, 4 ), in, 12 ) );
    okCancel( d, 5 );
    d->selectNext( False );
    char buf[8] = "";
    if (execDialog( d, buf ) == cmCancel)
        return false;
    line = atoi( buf );
    return line > 0;
}

bool argumentsDialog()
{
    TDialog *d = newDialog( 50, 8, "Program Arguments" );
    TInputLine *in = new TInputLine( TRect( 3, 3, 44, 4 ), sizeof opts.args );
    d->insert( in );
    d->insert( new TLabel( TRect( 2, 2, 30, 3 ), "~A~rguments", in ) );
    d->insert( new THistory( TRect( 44, 3, 47, 4 ), in, 13 ) );
    okCancel( d, 5 );
    d->selectNext( False );
    char buf[sizeof opts.args];
    strcpy( buf, opts.args );
    if (execDialog( d, buf ) == cmCancel)
        return false;
    strcpy( opts.args, buf );
    return true;
}

/* ------------------------------------------------------------ Options */

bool compilerDialog()
{
    struct { char defines[sizeof opts.defines]; ushort warn; ushort werr; } data;
    TDialog *d = newDialog( 52, 15, "Compiler" );
    TInputLine *in = new TInputLine( TRect( 3, 3, 49, 4 ), sizeof data.defines );
    d->insert( in );
    d->insert( new TLabel( TRect( 2, 2, 20, 3 ), "~D~efines", in ) );
    TRadioButtons *w = new TRadioButtons( TRect( 3, 6, 25, 9 ),
        new TSItem( "~N~one", new TSItem( "~S~tandard", new TSItem( "~A~ll", 0 ) ) ) );
    d->insert( w );
    d->insert( new TLabel( TRect( 2, 5, 20, 6 ), "~W~arnings", w ) );
    TCheckBoxes *c = new TCheckBoxes( TRect( 27, 6, 49, 7 ), new TSItem( "~E~rrors", 0 ) );
    d->insert( c );
    d->insert( new TLabel( TRect( 26, 5, 49, 6 ), "~T~reat warnings as", c ) );
    d->insert( new TStaticText( TRect( 3, 10, 49, 11 ), "Model: flat 32-bit, ARMv5TE, soft float" ) );
    okCancel( d, 12, true );
    d->selectNext( False );
    strcpy( data.defines, opts.defines );
    data.warn = opts.warnings;
    data.werr = opts.warnError;
    if (execDialog( d, &data ) == cmCancel)
        return false;
    strcpy( opts.defines, data.defines );
    opts.warnings = data.warn;
    opts.warnError = data.werr;
    buildDirty = true;
    return true;
}

bool makeDialog()
{
    struct { ushort brk; } data = { opts.makeBreak };
    TDialog *d = newDialog( 40, 10, "Make" );
    TRadioButtons *r = new TRadioButtons( TRect( 3, 3, 25, 5 ),
        new TSItem( "~E~rrors", new TSItem( "~W~arnings", 0 ) ) );
    d->insert( r );
    d->insert( new TLabel( TRect( 2, 2, 20, 3 ), "~B~reak make on", r ) );
    okCancel( d, 7 );
    d->selectNext( False );
    if (execDialog( d, &data ) == cmCancel)
        return false;
    opts.makeBreak = data.brk;
    return true;
}

bool linkerDialog()
{
    char buf[8];
    snprintf( buf, sizeof buf, "%u", opts.stackKB );
    TDialog *d = newDialog( 44, 10, "Linker" );
    TInputLine *in = new TInputLine( TRect( 3, 3, 12, 4 ), sizeof buf );
    d->insert( in );
    d->insert( new TLabel( TRect( 2, 2, 30, 3 ), "~S~tack size (KB)", in ) );
    d->insert( new TStaticText( TRect( 3, 5, 41, 6 ), "Output: ARM-DOS .EXE (MZ + AR1)" ) );
    okCancel( d, 7 );
    d->selectNext( False );
    if (execDialog( d, buf ) == cmCancel)
        return false;
    int kb = atoi( buf );
    if (kb < 4) kb = 4;
    if (kb > 1024) kb = 1024;
    opts.stackKB = kb;
    buildDirty = true;
    return true;
}

bool dirsDialog()
{
    struct { char inc[sizeof opts.incDirs]; char lib[sizeof opts.libDirs]; char out[sizeof opts.outDir]; } data;
    TDialog *d = newDialog( 60, 14, "Directories" );
    TInputLine *a = new TInputLine( TRect( 3, 3, 54, 4 ), sizeof data.inc );
    d->insert( a );
    d->insert( new TLabel( TRect( 2, 2, 30, 3 ), "~I~nclude directories", a ) );
    d->insert( new THistory( TRect( 54, 3, 57, 4 ), a, 14 ) );
    TInputLine *b = new TInputLine( TRect( 3, 6, 54, 7 ), sizeof data.lib );
    d->insert( b );
    d->insert( new TLabel( TRect( 2, 5, 30, 6 ), "~L~ibrary directories", b ) );
    d->insert( new THistory( TRect( 54, 6, 57, 7 ), b, 15 ) );
    TInputLine *c = new TInputLine( TRect( 3, 9, 54, 10 ), sizeof data.out );
    d->insert( c );
    d->insert( new TLabel( TRect( 2, 8, 45, 9 ), "~O~utput directory (empty: the source's)", c ) );
    d->insert( new THistory( TRect( 54, 9, 57, 10 ), c, 16 ) );
    okCancel( d, 11, true );
    d->selectNext( False );
    strcpy( data.inc, opts.incDirs );
    strcpy( data.lib, opts.libDirs );
    strcpy( data.out, opts.outDir );
    if (execDialog( d, &data ) == cmCancel)
        return false;
    strcpy( opts.incDirs, data.inc );
    strcpy( opts.libDirs, data.lib );
    strcpy( opts.outDir, data.out );
    buildDirty = true;
    return true;
}

bool envDialog()
{
    struct { ushort flags; char tab[4]; } data;
    TDialog *d = newDialog( 46, 13, "Environment" );
    TCheckBoxes *c = new TCheckBoxes( TRect( 3, 3, 43, 7 ),
        new TSItem( "Create ~b~ackup files", new TSItem( "~A~uto save desktop and options",
        new TSItem( "~S~yntax highlighting", new TSItem( "Auto ~i~ndent mode", 0 ) ) ) ) );
    d->insert( c );
    d->insert( new TLabel( TRect( 2, 2, 20, 3 ), "~E~ditor", c ) );
    TInputLine *t = new TInputLine( TRect( 3, 9, 8, 10 ), 4 );
    d->insert( t );
    d->insert( new TLabel( TRect( 2, 8, 20, 9 ), "~T~ab size", t ) );
    okCancel( d, 10 );
    d->selectNext( False );
    data.flags = opts.envFlags;
    snprintf( data.tab, sizeof data.tab, "%u", opts.tabSize );
    if (execDialog( d, &data ) == cmCancel)
        return false;
    opts.envFlags = data.flags;
    int ts = atoi( data.tab );
    if (ts < 1) ts = 1;
    if (ts > 16) ts = 16;
    opts.tabSize = ts;
    /* apply to the open editors */
    TView *first = TProgram::deskTop->first(), *p = first;
    if (p)
        do
        {
            if (p->helpCtx == hcEditorWin)
            {
                TcEditor *e = ((TcEditWindow *) p)->editor;
                e->autoIndent = (opts.envFlags & envAutoIndent) ? True : False;
                e->drawView();
            }
            p = p->next;
        } while (p != first);
    if (opts.envFlags & envBackup)
        TEditor::editorFlags |= efBackupFiles;
    else
        TEditor::editorFlags &= ~efBackupFiles;
    return true;
}

/* ------------------------------------------------------------ boxes */

void aboutBox()
{
    TDialog *d = newDialog( 44, 13, "About" );
    d->insert( new TStaticText( TRect( 2, 2, 42, 9 ),
        "\003ARM Turbo C\n"
        "\003Version 1.0\n"
        "\003\n"
        "\003Copyright (c) 2026 Europa Micro Systems\n"
        "\003\n"
        "\003Compiler: TinyCC 0.9.28rc (LGPL-2.1)\n"
        "\003Fabrice Bellard and the TinyCC authors" ) );
    d->insert( new TButton( TRect( 17, 10, 27, 12 ), "O~K~", cmOK, bfDefault ) );
    execDialog( d, 0 );
}

extern "C" unsigned long armdos_xms_heap_size( void );
extern int lastExitCode;
extern int totalWarnings, totalErrors;

void infoBox()
{
    char cwd[80];
    if (!getcwd( cwd, sizeof cwd ))
        strcpy( cwd, "?" );
    TcEditWindow *w = currentEditor();
    char text[600];
    struct mallinfo mi = mallinfo();
    snprintf( text, sizeof text,
        "Current directory  : %s\n"
        "Current file       : %s\n"
        "Project            : %s\n"
        "Lines compiled     : %ld\n"
        "Total warnings     : %d\n"
        "Total errors       : %d\n"
        "Program exit code  : %d\n"
        "Program size       : %lu\n"
        "Available memory   : %luK\n"
        "Extended mem in use: %luK",
        cwd, w ? w->editor->fileName : "(none)", projectName[0] ? projectName : "(none)",
        lastLines, totalWarnings, totalErrors, lastExitCode, lastExeSize,
        (unsigned long) _memmax() / 1024,
        (unsigned long) mi.arena / 1024 );
    TDialog *d = newDialog( 60, 16, "Information" );
    d->insert( new TStaticText( TRect( 3, 2, 58, 12 ), text ) );
    d->insert( new TButton( TRect( 25, 13, 35, 15 ), "O~K~", cmOK, bfDefault ) );
    execDialog( d, 0 );
}

void chdirDialog()
{
    TChDirDialog *d = new TChDirDialog( cdNormal, 102 );
    execDialog( d, 0 );
}

/* List all: the open windows */
static TCollection *windowList;
static void collectWindow( TView *p, void * )
{
    if (!(p->state & sfVisible))
        return;
    TWindow *w = (TWindow *) p;
    const char *t = w->getTitle( 60 );
    if (t)
    {
        char buf[80];
        if (w->number > 0 && w->number < 10)
            snprintf( buf, sizeof buf, "%d %s", w->number, t );
        else
            snprintf( buf, sizeof buf, "  %s", t );
        windowList->insert( newStr( buf ) );
    }
}

class TUnsortedList : public TStringCollection
{
public:
    TUnsortedList() : TStringCollection( 10, 5 ) { duplicates = True; }
    virtual ccIndex insert( void *item ) { return atInsert( getCount(), item ), getCount() - 1; }
};

void listAllDialog()
{
    TUnsortedList *items = new TUnsortedList;
    windowList = items;
    TProgram::deskTop->forEach( collectWindow, 0 );
    TDialog *d = newDialog( 50, 16, "Window List" );
    TScrollBar *sb = new TScrollBar( TRect( 45, 3, 46, 12 ) );
    d->insert( sb );
    TListBox *lb = new TListBox( TRect( 3, 3, 45, 12 ), 1, sb );
    d->insert( lb );
    d->insert( new TLabel( TRect( 2, 2, 20, 3 ), "~W~indows", lb ) );
    lb->newList( items );
    d->insert( new TButton( TRect( 12, 13, 22, 15 ), "~O~K", cmOK, bfDefault ) );
    d->insert( new TButton( TRect( 26, 13, 36, 15 ), "Cancel", cmCancel, bfNormal ) );
    d->selectNext( False );
    TView *p = TProgram::application->validView( d );
    if (!p)
        return;
    ushort r = TProgram::deskTop->execView( p );
    int sel = lb->focused;
    char title[80] = "";
    if (r == cmOK && sel >= 0 && sel < items->getCount())
        snprintf( title, sizeof title, "%s", (char *) items->at( sel ) + 2 );
    TObject::destroy( p );
    if (!title[0])
        return;
    TView *first = TProgram::deskTop->first(), *q = first;
    if (q)
        do
        {
            TWindow *w = (TWindow *) q;
            const char *t = w->getTitle( 60 );
            if ((q->state & sfVisible) && t && !strcmp( t, title ))
            {
                w->select();
                return;
            }
            q = q->next;
        } while (q != first);
}

/* ------------------------------------------------------------ Project */

class TcProjectList : public TListBox
{
public:
    TcProjectList( const TRect &r, TScrollBar *sb ) : TListBox( r, 1, sb ) { helpCtx = hcProjectWin; }
    virtual TPalette &getPalette() const
    {
        static TPalette p( "\x09\x09\x0A\x09\x09", 5 );
        return p;
    }
    virtual void selectItem( short item )
    {
        if (projectItems && item >= 0 && item < projectItems->getCount())
            openEditor( (char *) projectItems->at( item ) );
    }
    virtual void handleEvent( TEvent &event )
    {
        if (event.what == evKeyDown && event.keyDown.keyCode == kbEnter)
        {
            selectItem( focused );
            clearEvent( event );
            return;
        }
        if (event.what == evCommand && event.message.command == cmDeleteItem)
        {
            if (projectItems && focused >= 0 && focused < projectItems->getCount())
            {
                projectItems->atFree( focused );
                setRange( projectItems->getCount() );
                drawView();
                projectSave();
                buildDirty = true;
            }
            clearEvent( event );
            return;
        }
        TListBox::handleEvent( event );
    }
};

class TcProjectWindow : public TWindow
{
public:
    TcProjectWindow( const TRect &r ) :
        TWindowInit( &TcProjectWindow::initFrame ),
        TWindow( r, "Project", wnNoNumber )
    {
        options |= ofTileable;
        helpCtx = hcProjectWin;
        TScrollBar *vs = new TScrollBar( TRect( size.x - 1, 1, size.x, size.y - 1 ) );
        insert( vs );
        TRect lr( getExtent() );
        lr.grow( -1, -1 );
        list = new TcProjectList( lr, vs );
        insert( list );
    }
    virtual TPalette &getPalette() const
    {
        static TPalette p( "\x10\x11\x12\x13\x14\x15\x16\x17\x88\x89", 10 );
        return p;
    }
    virtual void close() { hide(); }
    virtual const char *getTitle( short )
    {
        static char t[90];
        snprintf( t, sizeof t, "Project: %s", projectName );
        return t;
    }
    TcProjectList *list;
};

static TcProjectWindow *projWindow;

static void projectRefresh()
{
    if (!projWindow)
        return;
    projWindow->list->newList( 0 );
    if (projectItems)
    {
        /* newList takes ownership: give it a copy */
        TStringCollection *c = new TStringCollection( 10, 5 );
        for (int i = 0; i < projectItems->getCount(); ++i)
            c->insert( newStr( (char *) projectItems->at( i ) ) );
        projWindow->list->newList( c );
    }
    if (projWindow->frame)
        projWindow->frame->drawView();
}

void projectSave()
{
    if (!projectName[0] || !projectItems)
        return;
    FILE *f = fopen( projectName, "w" );
    if (!f)
        return;
    for (int i = 0; i < projectItems->getCount(); ++i)
        fprintf( f, "%s\n", (char *) projectItems->at( i ) );
    fclose( f );
    projectRefresh();
}

void projectOpen( const char *name )
{
    char full[MAXPATH];
    fullPath( full, name );
    if (!strrchr( full, '.' ) || strrchr( full, '.' ) < strrchr( full, '\\' ))
        strcat( full, ".PRJ" );
    projectClose();
    strcpy( projectName, full );
    projectItems = new TStringCollection( 10, 5 );
    FILE *f = fopen( full, "r" );
    if (f)
    {
        char line[120];
        while (fgets( line, sizeof line, f ))
        {
            char *e = line + strlen( line );
            while (e > line && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' '))
                *--e = 0;
            char *s = line;
            while (*s == ' ' || *s == '\t')
                ++s;
            if (!*s || *s == ';')
                continue;
            for (char *p = s; *p; ++p)
                *p = toupper( (uchar) *p );
            projectItems->insert( newStr( s ) );
        }
        fclose( f );
    }
    buildDirty = true;
    projectWindowShow();
}

void projectClose()
{
    if (projectItems)
    {
        TObject::destroy( projectItems );
        projectItems = 0;
    }
    projectName[0] = 0;
    if (projWindow)
    {
        projWindow->list->newList( 0 );
        projWindow->hide();
    }
    buildDirty = true;
}

void projectAdd( const char *file )
{
    if (!projectItems)
        return;
    /* relative to the project's directory when it is there */
    char full[MAXPATH], dir[MAXPATH];
    fullPath( full, file );
    strcpy( dir, projectName );
    char *s = strrchr( dir, '\\' );
    if (s)
        s[1] = 0;
    const char *item = full;
    if (!strncasecmp( full, dir, strlen( dir ) ) && !strchr( full + strlen( dir ), '\\' ))
        item = full + strlen( dir );
    for (int i = 0; i < projectItems->getCount(); ++i)
        if (!strcasecmp( (char *) projectItems->at( i ), item ))
            return;
    projectItems->insert( newStr( item ) );
    projectSave();
    buildDirty = true;
}

bool projectAddDialog()
{
    char name[MAXPATH] = "*.C";
    TFileDialog *d = new TFileDialog( "*.C", "Add to Project List", "~N~ame", fdOKButton, 103 );
    if (execDialog( d, name ) == cmCancel)
        return false;
    projectAdd( name );
    return true;
}

void projectWindowShow()
{
    if (!projectName[0])
    {
        messageBox( "No project is open.", mfInformation | mfOKButton );
        return;
    }
    if (!projWindow)
    {
        TRect dt = TProgram::deskTop->getExtent();
        projWindow = new TcProjectWindow( TRect( 0, dt.b.y - 7, dt.b.x, dt.b.y ) );
        TProgram::deskTop->insert( projWindow );
    }
    projectRefresh();
    projWindow->show();
    projWindow->select();
}
