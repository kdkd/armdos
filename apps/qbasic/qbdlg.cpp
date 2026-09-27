/*
 * qbdlg.cpp - QB.EXE's dialog boxes, in the style of the ARM-DOS Editor's
 * (this file started as a copy of apps/edit/dialogs.cpp): a single line
 * frame with the title in the top border, fields in boxes, a separator line
 * above the buttons, buttons drawn "< OK >". Plus QB's own: New SUB / New
 * FUNCTION, the SUBs list.
 *
 * Copyright (c) 2026 Europa Micro Systems. MIT License.
 */
#include "qb.h"
#include "edplat.h"

#define Uses_TFindDialogRec
#define Uses_TReplaceDialogRec
#include <tvision/tv.h>

#include <dir.h>
#include <dos.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>

/* ------------------------------------------------------------ pieces */

/* Dialog frame: ┌── Title ──┐ │ │ └──┘, not movable, no icons. */
class EdDlgFrame : public TFrame
{
public:
    EdDlgFrame( const TRect &r ) : TFrame( r ) {}
    virtual void draw();
    virtual void handleEvent( TEvent &event ) { TView::handleEvent( event ); }
};

void EdDlgFrame::draw()
{
    TDrawBuffer b;
    TColorAttr c = getColor( 1 );
    b.moveChar( 0, '\xDA', c, 1 );
    b.moveChar( 1, '\xC4', c, size.x - 2 );
    b.moveChar( size.x - 1, '\xBF', c, 1 );
    const char *title = owner ? ((TWindow *) owner)->getTitle( size.x - 4 ) : 0;
    if (title && *title)
    {
        int l = (int) strlen( title );
        int x = (size.x - l - 2) / 2;
        b.moveChar( x, ' ', c, 1 );
        b.moveStr( x + 1, title, c );
        b.moveChar( x + 1 + l, ' ', c, 1 );
    }
    writeLine( 0, 0, size.x, 1, b );
    b.moveChar( 0, '\xB3', c, 1 );
    b.moveChar( 1, ' ', c, size.x - 2 );
    b.moveChar( size.x - 1, '\xB3', c, 1 );
    writeLine( 0, 1, size.x, size.y - 2, b );
    b.moveChar( 0, '\xC0', c, 1 );
    b.moveChar( 1, '\xC4', c, size.x - 2 );
    b.moveChar( size.x - 1, '\xD9', c, 1 );
    writeLine( 0, size.y - 1, size.x, 1, b );
}

/* A single-line box, optionally with a title in its top border. */
class EdBox : public TView
{
public:
    EdBox( const TRect &r, const char *t ) : TView( r ), title( t ) {}
    virtual void draw();
    virtual TPalette &getPalette() const;
    const char *title;
};

#define cpEdBox "\x06"      /* dialog: static text */

TPalette &EdBox::getPalette() const
{
    static TPalette palette( cpEdBox, sizeof( cpEdBox ) - 1 );
    return palette;
}

void EdBox::draw()
{
    TDrawBuffer b;
    TColorAttr c = getColor( 1 );
    b.moveChar( 0, '\xDA', c, 1 );
    b.moveChar( 1, '\xC4', c, size.x - 2 );
    b.moveChar( size.x - 1, '\xBF', c, 1 );
    if (title)
    {
        int l = (int) strlen( title );
        int x = (size.x - l - 2) / 2;
        b.moveChar( x, ' ', c, 1 );
        b.moveStr( x + 1, title, c );
        b.moveChar( x + 1 + l, ' ', c, 1 );
    }
    writeLine( 0, 0, size.x, 1, b );
    b.moveChar( 0, '\xB3', c, 1 );
    b.moveChar( 1, ' ', c, size.x - 2 );
    b.moveChar( size.x - 1, '\xB3', c, 1 );
    if (size.y > 2)
        writeLine( 0, 1, size.x, size.y - 2, b );
    b.moveChar( 0, '\xC0', c, 1 );
    b.moveChar( 1, '\xC4', c, size.x - 2 );
    b.moveChar( size.x - 1, '\xD9', c, 1 );
    writeLine( 0, size.y - 1, size.x, 1, b );
}

/* ├──────┤ across the dialog */
class EdSeparator : public TView
{
public:
    EdSeparator( const TRect &r ) : TView( r ) {}
    virtual void draw()
    {
        TDrawBuffer b;
        TColorAttr c = getColor( 1 );
        b.moveChar( 0, '\xC3', c, 1 );
        b.moveChar( 1, '\xC4', c, size.x - 2 );
        b.moveChar( size.x - 1, '\xB4', c, 1 );
        writeLine( 0, 0, size.x, 1, b );
    }
    virtual TPalette &getPalette() const
    {
        static TPalette palette( cpEdBox, sizeof( cpEdBox ) - 1 );
        return palette;
    }
};

/* A line of text; centred if the text starts with \x03 (as TStaticText). */
class EdText : public TStaticText
{
public:
    EdText( const TRect &r, TStringView t ) : TStaticText( r, t ) {}
    void setText( const char *t )
    {
        delete[] (char *) text;
        text = newStr( t );
        drawView();
    }
};

/* ------------------------------------------------------------- dialog */

EdDialog::EdDialog( const TRect &bounds, TStringView title, ushort aHelpCtx ) :
    TWindowInit( &EdDialog::initFrame ),
    TDialog( bounds, title )
{
    flags = 0;
    helpCtx = aHelpCtx;
    options |= ofCentered;
}

TFrame *EdDialog::initFrame( TRect r )
{
    return new EdDlgFrame( r );
}

TPalette &EdDialog::getPalette() const
{
    return TDialog::getPalette();
}

void EdDialog::handleEvent( TEvent &event )
{
    if (event.what == evCommand)
        switch (event.message.command)
        {
            case cmFindVerify:
            case cmChangeAll:
            case cmSkip:
                if (state & sfModal)
                {
                    endModal( event.message.command );
                    clearEvent( event );
                    return;
                }
                break;
            case cmDlgHelp:
                clearEvent( event );
                helpModal( helpTopicFor( helpCtx ) );
                return;
        }
    TDialog::handleEvent( event );
}

void EdDialog::separator( int y )
{
    insert( new EdSeparator( TRect( 0, y, size.x, y + 1 ) ) );
}

void EdDialog::box( const TRect &r, const char *title )
{
    TRect b = r;
    b.grow( 1, 1 );
    insert( new EdBox( b, title ) );
}

/* Focus the first selectable view inserted (TV's list runs newest first;
 * 'last' is the oldest). */
static void focusFirst( TGroup *g )
{
    TView *p = g->last;
    if (!p)
        return;
    do
    {
        if ((p->options & ofSelectable) && (p->state & sfVisible) && !(p->state & sfDisabled))
        {
            p->select();
            return;
        }
        p = p->prev();
    } while (p != g->last);
}

/* A row of buttons, centred, at row y. Returns the first. */
struct BtnDef { const char *title; ushort command; ushort flags; };

static void buttonRow( EdDialog *d, int y, const BtnDef *defs, int n )
{
    int total = 0;
    for (int i = 0; i < n; ++i)
        total += EdButton::width( defs[i].title );
    int gap = (d->size.x - 2 - total) / (n + 1);
    if (gap < 1)
        gap = 1;
    int x = 1 + (d->size.x - 2 - total - gap * (n - 1)) / 2;
    for (int i = 0; i < n; ++i)
    {
        int w = EdButton::width( defs[i].title );
        d->insert( new EdButton( TRect( x, y, x + w, y + 1 ), defs[i].title,
                                 defs[i].command, defs[i].flags ) );
        x += w + gap;
    }
}

/* ------------------------------------------------------------- button */

EdButton::EdButton( const TRect &bounds, TStringView title, ushort command, ushort flags ) :
    TButton( bounds, title, command, flags )
{
}

void EdButton::drawState( Boolean down )
{
    TDrawBuffer b;
    bool disabled = (state & sfDisabled) != 0;
    TColorAttr cNormal = getColor( disabled ? 4 : 1 );
    TColorAttr cHigh = getColor( 5 );
    if (down)
        cNormal = TColorAttr( 0x07 ), cHigh = TColorAttr( 0x0F );
    bool lit = !disabled && ((state & sfFocused) || amDefault);
    TColorAttr cBracket = lit ? cHigh : cNormal;
    b.moveChar( 0, ' ', cNormal, size.x );
    b.moveChar( 0, '<', cBracket, 1 );
    b.moveCStr( 2, title, TAttrPair( cNormal, disabled ? cNormal : cHigh ) );
    b.moveChar( size.x - 1, '>', cBracket, 1 );
    writeLine( 0, 0, size.x, 1, b );
    if (state & sfFocused)
    {
        setCursor( 2, 0 );
        showCursor();
    }
    else
        hideCursor();
}

void EdButton::draw()
{
    drawState( False );
}

/* ------------------------------------------------------------ helpers */

ushort edExecDialog( TDialog *d, void *data )
{
    TView *p = TProgram::application->validView( d );
    if (p == 0)
        return cmCancel;
    if (data)
        p->setData( data );
    ushort result = TProgram::deskTop->execView( p );
    if (result != cmCancel && data)
        p->getData( data );
    TObject::destroy( p );
    return result;
}

/* A message box: the text centred (lines split at \n), then the buttons. */
ushort edMessage( const char *text, ushort buttons, ushort helpCtx )
{
    static const BtnDef ok[] = { { "OK", cmOK, bfDefault }, { "Help", cmDlgHelp, bfNormal } };
    static const BtnDef ync[] = { { "~Y~es", cmYes, bfDefault }, { "~N~o", cmNo, bfNormal },
                                  { "Cancel", cmCancel, bfNormal }, { "Help", cmDlgHelp, bfNormal } };
    static const BtnDef okc[] = { { "OK", cmOK, bfDefault }, { "Cancel", cmCancel, bfNormal },
                                  { "Help", cmDlgHelp, bfNormal } };
    const BtnDef *defs = buttons == emYesNoCancel ? ync : buttons == emOKCancel ? okc : ok;
    int n = buttons == emYesNoCancel ? 4 : buttons == emOKCancel ? 3 : 2;

    /* measure */
    int lines = 1, maxw = 0, w = 0;
    for (const char *p = text; *p; ++p)
        if (*p == '\n') { ++lines; if (w > maxw) maxw = w; w = 0; }
        else ++w;
    if (w > maxw) maxw = w;
    int bw = 0;
    for (int i = 0; i < n; ++i)
        bw += EdButton::width( defs[i].title ) + 3;
    int width = maxw + 8;
    if (width < bw + 3) width = bw + 3;
    if (width < 30) width = 30;
    int height = lines + 6;

    EdDialog *d = new EdDialog( TRect( 0, 0, width, height ), "", helpCtx );
    const char *p = text;
    for (int y = 0; y < lines; ++y)
    {
        const char *e = strchr( p, '\n' );
        int l = e ? (int) (e - p) : (int) strlen( p );
        char buf[82];
        buf[0] = '\x03';
        memcpy( buf + 1, p, l < 80 ? l : 80 );
        buf[1 + (l < 80 ? l : 80)] = 0;
        d->insert( new EdText( TRect( 2, 2 + y, width - 2, 3 + y ), buf ) );
        p = e ? e + 1 : p + l;
    }
    d->separator( height - 3 );
    buttonRow( d, height - 2, defs, n );
    focusFirst( d );
    return edExecDialog( d, 0 );
}

/* ---------------------------------------------------------- find/change */

/* The Find What field starts with the selected text, or the word at the
 * cursor, as in EDIT. */
static void presetFind( char *find, size_t size )
{
    if (!docWindow)
        return;
    QbEditor *ed = docWindow->editor;
    uint a, b;
    if (ed->hasSelection())
    {
        a = ed->selStart;
        b = ed->selEnd;
    }
    else
    {
        a = b = ed->curPtr;
        auto isWord = [&]( uint p ) {
            char c = ed->bufChar( p );
            return isalnum( (uchar) c ) || c == '_' || (uchar) c >= 0x80;
        };
        while (a > 0 && isWord( a - 1 )) --a;
        while (b < ed->bufLen && isWord( b )) ++b;
    }
    if (b > a && b - a < size)
    {
        uint i;
        for (i = 0; i < b - a; ++i)
        {
            char c = ed->bufChar( a + i );
            if (c == '\r' || c == '\n')
                break;
            find[i] = c;
        }
        if (i > 0)
            find[i] = 0;
    }
}

static TInputLine *fieldWithBox( EdDialog *d, int y, const char *label, int labelX,
                                  int fieldX, int fieldW, int maxLen )
{
    TRect r( fieldX, y, fieldX + fieldW, y + 1 );
    d->box( r );
    TInputLine *in = new TInputLine( r, maxLen );
    d->insert( in );
    d->insert( new TLabel( TRect( labelX, y, fieldX - 1, y + 1 ), label, in ) );
    return in;
}

static ushort findDialog( TFindDialogRec *rec )
{
    EdDialog *d = new EdDialog( TRect( 0, 0, 62, 10 ), "Find", hcDlgFind );
    fieldWithBox( d, 2, "~F~ind What:", 2, 15, 44, maxFindStrLen );
    d->insert( new TCheckBoxes( TRect( 3, 5, 58, 6 ),
        new TSItem( "~M~atch Upper/Lowercase",
        new TSItem( "~W~hole Word", 0 ) ) ) );
    d->separator( 7 );
    static const BtnDef btns[] = { { "OK", cmOK, bfDefault }, { "Cancel", cmCancel, bfNormal },
                                   { "Help", cmDlgHelp, bfNormal } };
    buttonRow( d, 8, btns, 3 );
    focusFirst( d );
    presetFind( rec->find, sizeof rec->find );
    return edExecDialog( d, rec );
}

static ushort changeDialog( TReplaceDialogRec *rec )
{
    EdDialog *d = new EdDialog( TRect( 0, 0, 66, 13 ), "Change", hcDlgChange );
    fieldWithBox( d, 2, "~F~ind What:", 2, 15, 48, maxFindStrLen );
    fieldWithBox( d, 5, "~C~hange To:", 2, 15, 48, maxReplaceStrLen );
    d->insert( new TCheckBoxes( TRect( 3, 8, 62, 9 ),
        new TSItem( "~M~atch Upper/Lowercase",
        new TSItem( "~W~hole Word", 0 ) ) ) );
    d->separator( 10 );
    static const BtnDef btns[] = { { "Find and ~V~erify", cmFindVerify, bfDefault },
                                   { "Change ~A~ll", cmChangeAll, bfNormal },
                                   { "Cancel", cmCancel, bfNormal }, { "Help", cmDlgHelp, bfNormal } };
    buttonRow( d, 11, btns, 4 );
    focusFirst( d );
    presetFind( rec->find, sizeof rec->find );
    ushort r = edExecDialog( d, rec );
    rec->options &= ~(efPromptOnReplace | efReplaceAll);
    if (r == cmFindVerify)
        rec->options |= efPromptOnReplace | efReplaceAll;
    else if (r == cmChangeAll)
        rec->options |= efReplaceAll;
    return r == cmCancel ? cmCancel : cmOK;
}

/* "Change" verification: < Change > < Skip > < Cancel > < Help >, placed
 * so that it does not cover the match. */
static ushort verifyDialog( TPoint where )
{
    EdDialog *d = new EdDialog( TRect( 0, 0, 50, 6 ), "Change", hcDlgVerify );
    d->options &= ~ofCentered;
    d->insert( new EdText( TRect( 2, 1, 48, 2 ), "\x03" "Change this occurrence?" ) );
    d->separator( 3 );
    static const BtnDef btns[] = { { "~C~hange", cmYes, bfDefault }, { "~S~kip", cmNo, bfNormal },
                                   { "Cancel", cmCancel, bfNormal }, { "Help", cmDlgHelp, bfNormal } };
    buttonRow( d, 4, btns, 4 );
    focusFirst( d );
    TRect desk = TProgram::deskTop->getExtent();
    int x = (desk.b.x - d->size.x) / 2;
    int y = where.y - 1 < desk.b.y / 2 ? desk.b.y - d->size.y - 1 : 1;
    d->moveTo( x, y );
    return edExecDialog( d, 0 );
}

/* The editor's dialog callback (TEditor::editorDialog). */
ushort doEditDialog( int dialog, ... )
{
    va_list arg;
    char buf[MAXPATH + 64];
    va_start( arg, dialog );
    ushort r = cmCancel;
    switch (dialog)
    {
        case edOutOfMemory:
            r = edMessage( "Out of memory", emOK );
            break;
        case edReadError:
            snprintf( buf, sizeof buf, "Error reading file %s", va_arg( arg, char * ) );
            r = edMessage( buf, emOK );
            break;
        case edWriteError:
            snprintf( buf, sizeof buf, "Error writing file %s", va_arg( arg, char * ) );
            r = edMessage( buf, emOK );
            break;
        case edCreateError:
        {
            char *name = va_arg( arg, char * );
            snprintf( buf, sizeof buf, armdosCritError >= 0 ? "Device not ready: %s" : "Path not found: %s", name );
            armdosCritError = -1;
            r = edMessage( buf, emOK );
            break;
        }
        case edSaveModify:
        case edSaveUntitled:
            r = edMessage( "Loaded file is not saved. Save it now?", emYesNoCancel, hcDlgSaveChanges );
            break;
        case edSaveAs:
            r = saveAsDialog( va_arg( arg, char * ) ) ? cmOK : cmCancel;
            break;
        case edFind:
            r = findDialog( va_arg( arg, TFindDialogRec * ) );
            break;
        case edSearchFailed:
            r = edMessage( "Match not found", emOK );
            break;
        case edReplace:
            r = changeDialog( va_arg( arg, TReplaceDialogRec * ) );
            break;
        case edReplacePrompt:
            r = verifyDialog( *va_arg( arg, TPoint * ) );
            break;
    }
    va_end( arg );
    return r;
}

/* ------------------------------------------------------ file dialogs */

static bool hasWild( const char *s )
{
    return strpbrk( s, "*?" ) != 0;
}

static void currentDir( char *buf, size_t size )
{
    int d = getdisk();
    char dir[MAXDIR];
    if (getcurdir( d + 1, dir ) != 0)
        dir[0] = 0;
    snprintf( buf, size, "%c:\\%s", 'A' + d, dir );
}

/* Directory lists: TListBox over a TStringCollection. */
class NameList : public TListBox
{
public:
    NameList( const TRect &r, ushort cols, TScrollBar *sb ) : TListBox( r, cols, sb ) {}
    virtual void focusItem( short item );
    virtual void selectItem( short item );
    virtual void handleEvent( TEvent &event );
    void (*onFocus)( void *, const char * ) = 0;
    void (*onSelect)( void *, const char * ) = 0;
    void *ctx = 0;
    const char *itemText( short i )
    {
        return list() && i >= 0 && i < list()->getCount() ? (const char *) list()->at( i ) : "";
    }
};

void NameList::focusItem( short item )
{
    TListBox::focusItem( item );
    if (onFocus)
        onFocus( ctx, itemText( item ) );
}

void NameList::selectItem( short item )
{
    if (onSelect)
        onSelect( ctx, itemText( item ) );
}

void NameList::handleEvent( TEvent &event )
{
    if (event.what == evKeyDown && event.keyDown.keyCode == kbEnter && range > 0)
    {
        selectItem( focused );
        clearEvent( event );
        return;
    }
    if (event.what == evMouseDown && (event.mouse.eventFlags & meDoubleClick))
    {
        TListBox::handleEvent( event );
        selectItem( focused );
        clearEvent( event );
        return;
    }
    TListBox::handleEvent( event );
}

/* A string collection that keeps our (unsorted) order and owns copies. */
class NameCollection : public TStringCollection
{
public:
    NameCollection() : TStringCollection( 32, 32 ) { duplicates = True; }
};

class FileDialog : public EdDialog
{
public:
    FileDialog( bool open );
    virtual Boolean valid( ushort command );
    void refresh();
    static void fileFocused( void *ctx, const char *name );
    static void fileChosen( void *ctx, const char *name );
    static void dirChosen( void *ctx, const char *name );

    bool isOpen;
    TInputLine *nameLine;
    EdText *pathText;
    NameList *files, *dirs;
    char pattern[MAXPATH];
    char pathBuf[MAXPATH + 2];
};

FileDialog::FileDialog( bool open ) :
    TWindowInit( &EdDialog::initFrame ),
    EdDialog( open ? TRect( 0, 0, 68, 21 ) : TRect( 0, 0, 56, 18 ),
              open ? "Open" : "Save As", open ? hcDlgOpen : hcDlgSaveAs ),
    isOpen( open ), files( 0 )
{
    strcpy( pattern, "*.BAS" );
    int fw = size.x - 18;
    nameLine = fieldWithBox( this, 2, "File ~N~ame:", 2, 15, fw, MAXPATH );
    pathBuf[0] = 0;
    pathText = new EdText( TRect( 2, 4, size.x - 2, 5 ), "" );
    insert( pathText );
    int top = 6, bottom = size.y - 4;
    if (open)
    {
        TRect fr( 3, top, 45, bottom );
        box( fr, "Files" );
        TScrollBar *sb = new TScrollBar( TRect( fr.b.x, fr.a.y, fr.b.x + 1, fr.b.y ) );
        insert( sb );
        files = new NameList( fr, 3, sb );
        files->onFocus = fileFocused;
        files->onSelect = fileChosen;
        files->ctx = this;
        insert( files );
        TRect dr( 49, top, size.x - 4, bottom );
        box( dr, "Dirs/Drives" );
        TScrollBar *sb2 = new TScrollBar( TRect( dr.b.x, dr.a.y, dr.b.x + 1, dr.b.y ) );
        insert( sb2 );
        dirs = new NameList( dr, 1, sb2 );
    }
    else
    {
        TRect dr( 3, top, 20, bottom );
        box( dr, "Dirs/Drives" );
        TScrollBar *sb2 = new TScrollBar( TRect( dr.b.x, dr.a.y, dr.b.x + 1, dr.b.y ) );
        insert( sb2 );
        dirs = new NameList( dr, 1, sb2 );
    }
    dirs->onSelect = dirChosen;
    dirs->ctx = this;
    insert( dirs );
    separator( size.y - 3 );
    static const BtnDef btns[] = { { "OK", cmOK, bfDefault }, { "Cancel", cmCancel, bfNormal },
                                   { "Help", cmDlgHelp, bfNormal } };
    buttonRow( this, size.y - 2, btns, 3 );
    focusFirst( this );
}

void FileDialog::fileFocused( void *ctx, const char *name )
{
    FileDialog *d = (FileDialog *) ctx;
    if (d->files && (d->files->state & sfFocused) && *name)
    {
        d->nameLine->setData( (void *) name );
        d->nameLine->drawView();
    }
}

void FileDialog::fileChosen( void *ctx, const char *name )
{
    FileDialog *d = (FileDialog *) ctx;
    if (*name)
    {
        d->nameLine->setData( (void *) name );
        TEvent e;
        e.what = evCommand;
        e.message.command = cmOK;
        e.message.infoPtr = 0;
        d->putEvent( e );
    }
}

void FileDialog::dirChosen( void *ctx, const char *name )
{
    FileDialog *d = (FileDialog *) ctx;
    if (name[0] == '[' && name[1] == '-')
        setdisk( toupper( (uchar) name[2] ) - 'A' );
    else if (*name)
        chdir( name );
    d->refresh();
}

static int cmpName( const void *a, const void *b )
{
    return strcmp( *(const char **) a, *(const char **) b );
}

void FileDialog::refresh()
{
    currentDir( pathBuf, sizeof pathBuf );
    pathText->setText( pathBuf );

    ffblk ff;
    /* files */
    if (files)
    {
        TStringCollection *c = new NameCollection;
        if (findfirst( pattern, &ff, 0 ) == 0)
            do
                c->insert( newStr( ff.ff_name ) );
            while (findnext( &ff ) == 0);
        files->newList( c );
    }
    /* directories, then drives */
    TStringCollection *c = new NameCollection;
    char **names = 0;
    int n = 0, cap = 0;
    if (findfirst( "*.*", &ff, FA_DIREC ) == 0)
        do
            if ((ff.ff_attrib & FA_DIREC) && strcmp( ff.ff_name, "." ) != 0)
            {
                if (n == cap)
                {
                    cap = cap ? cap * 2 : 32;
                    names = (char **) realloc( names, cap * sizeof *names );
                }
                names[n++] = newStr( ff.ff_name );
            }
        while (findnext( &ff ) == 0);
    qsort( names, n, sizeof *names, cmpName );
    for (int i = 0; i < n; ++i)
        c->atInsert( c->getCount(), names[i] );
    free( names );
    int nd = setdisk( getdisk() );
    for (int i = 0; i < nd && i < 26; ++i)
        if (i != 1 && armdosDriveValid( 'A' + i ))
        {
            char dr[6] = { '[', '-', (char) ('A' + i), '-', ']', 0 };
            c->atInsert( c->getCount(), newStr( dr ) );
        }
    dirs->newList( c );
}

Boolean FileDialog::valid( ushort command )
{
    if (command != cmOK)
        return TDialog::valid( command );
    char name[MAXPATH];
    nameLine->getData( name );
    /* trim */
    char *s = name;
    while (*s == ' ') ++s;
    int l = (int) strlen( s );
    while (l > 0 && s[l - 1] == ' ') s[--l] = 0;
    if (*s == 0)
        return False;
    if (hasWild( s ))
    {
        /* a new pattern: may include a directory */
        char drive[MAXDRIVE], dir[MAXDIR], file[MAXFILE], ext[MAXEXT];
        fnsplit( s, drive, dir, file, ext );
        if (drive[0])
            setdisk( toupper( (uchar) drive[0] ) - 'A' );
        if (dir[0])
        {
            int dl = (int) strlen( dir );
            if (dl > 1 && dir[dl - 1] == '\\')
                dir[dl - 1] = 0;
            if (chdir( dir ) != 0)
            {
                edMessage( "Path not found", emOK );
                return False;
            }
        }
        snprintf( pattern, sizeof pattern, "%s%s", file, ext );
        strupr( pattern );
        nameLine->setData( pattern );
        nameLine->drawView();
        refresh();
        return False;
    }
    /* a directory or a drive? */
    if ((l == 2 && s[1] == ':') || isDir( s ))
    {
        if (l == 2 && s[1] == ':')
            setdisk( toupper( (uchar) s[0] ) - 'A' );
        else if (chdir( s ) != 0)
        {
            edMessage( "Path not found", emOK );
            return False;
        }
        nameLine->setData( pattern );
        nameLine->drawView();
        refresh();
        return False;
    }
    if (!validFileName( s ))
    {
        edMessage( "Bad file name", emOK );
        return False;
    }
    if (!isOpen)
    {
        char full[MAXPATH];
        strnzcpy( full, s, sizeof full );
        fexpand( full );
        if (docWindow && strcasecmp( full, docWindow->editor->fileName ) != 0)
        {
            int fd = open( full, O_RDONLY );
            if (fd >= 0)
            {
                ::close( fd );
                if (edMessage( "File already exists. Overwrite?", emOKCancel ) != cmOK)
                    return False;
            }
        }
    }
    nameLine->setData( s );
    return True;
}

/* Run a file dialog; the name typed (or chosen) comes from its File Name
 * field (the lists' data must not go through TGroup::setData/getData). */
static bool runFileDialog( FileDialog *d, char *fileName )
{
    if (!TProgram::application->validView( d ))
        return false;
    ushort r = TProgram::deskTop->execView( d );
    char buf[MAXPATH];
    d->nameLine->getData( buf );
    TObject::destroy( d );
    if (r == cmCancel)
        return false;
    char *s = buf;
    while (*s == ' ') ++s;
    strnzcpy( fileName, s, MAXPATH );
    fexpand( fileName );
    return true;
}

bool openDialog( char *fileName )
{
    FileDialog *d = new FileDialog( true );
    if (hasWild( fileName ))
        strnzcpy( d->pattern, fileName, sizeof d->pattern );
    d->nameLine->setData( d->pattern );
    d->refresh();
    return runFileDialog( d, fileName );
}

bool saveAsDialog( char *fileName )
{
    FileDialog *d = new FileDialog( false );
    char buf[MAXPATH];
    const char *base = fileName;
    const char *q = strrchr( base, '\\' );
    if (q) base = q + 1;
    strnzcpy( buf, base, sizeof buf );
    d->nameLine->setData( buf );
    d->refresh();
    if (!runFileDialog( d, fileName ))
        return false;
    /* BASIC programs: .BAS when no extension is given */
    const char *b = strrchr( fileName, '\\' );
    b = b ? b + 1 : fileName;
    if (!strchr( b, '.' ) && strlen( fileName ) + 4 < MAXPATH)
        strcat( fileName, ".BAS" );
    return true;
}

/* -------------------------------------------------------------- print */

int printDialog( bool haveSelection )
{
    EdDialog *d = new EdDialog( TRect( 0, 0, 40, 9 ), "Print", hcDlgPrint );
    TRadioButtons *rb = new TRadioButtons( TRect( 3, 2, 37, 4 ),
        new TSItem( "~S~elected Text Only",
        new TSItem( "~C~omplete Document", 0 ) ) );
    d->insert( rb );
    if (!haveSelection)
        rb->setButtonState( 1, False );
    d->separator( 6 );
    static const BtnDef btns[] = { { "OK", cmOK, bfDefault }, { "Cancel", cmCancel, bfNormal },
                                   { "Help", cmDlgHelp, bfNormal } };
    buttonRow( d, 7, btns, 3 );
    focusFirst( d );
    ushort v = haveSelection ? 0 : 1;
    if (edExecDialog( d, &v ) == cmCancel)
        return 0;
    return v == 0 ? 1 : 2;
}

/* ------------------------------------------------------------ display */

static const char *const colorNames[16] = {
    "Black", "Blue", "Green", "Cyan", "Red", "Magenta", "Brown", "White",
    "Gray", "BrBlue", "BrGreen", "BrCyan", "BrRed", "Pink", "Yellow", "BrWhite",
};

class ColorList : public TListViewer
{
public:
    ColorList( const TRect &r, TScrollBar *sb, int n ) : TListViewer( r, 1, 0, sb ) { setRange( n ); }
    virtual void getText( char *dest, short item, short maxLen )
    {
        strnzcpy( dest, colorNames[item], maxLen + 1 );
    }
    virtual void focusItem( short item )
    {
        TListViewer::focusItem( item );
        message( owner, evBroadcast, cmEditorMoved, this );
    }
    virtual TPalette &getPalette() const
    {
        static TPalette p( "\x1A\x1A\x1B\x1B\x1D", 5 );   /* current item always shown */
        return p;
    }
};

class ItemList : public ColorList
{
public:
    ItemList( const TRect &r ) : ColorList( r, 0, 2 ) {}
    virtual void getText( char *dest, short item, short maxLen )
    {
        strnzcpy( dest, item == 0 ? "1 Normal Text" : "2 Highlighted Text", maxLen + 1 );
    }
};

/* the sample of the chosen colours */
class ColorSample : public TView
{
public:
    ColorSample( const TRect &r ) : TView( r ) {}
    uchar attr = 0x17;
    virtual void draw()
    {
        TDrawBuffer b;
        b.moveChar( 0, ' ', TColorAttr( attr ), size.x );
        b.moveStr( 1, "ARM QuickBASIC", TColorAttr( attr ) );
        writeLine( 0, 0, size.x, size.y, b );
    }
};

class DisplayDialog : public EdDialog
{
public:
    DisplayDialog();
    virtual void handleEvent( TEvent &event );
    void syncFromItem();
    ItemList *items;
    ColorList *fg, *bg;
    ColorSample *sample;
    TCheckBoxes *bars;
    TInputLine *tabs;
    DisplayOptions opts;
    int curItem = -1;
    bool syncing = false;
};

DisplayDialog::DisplayDialog() :
    TWindowInit( &EdDialog::initFrame ),
    EdDialog( TRect( 0, 0, 66, 21 ), "Display", hcDlgDisplay )
{
    opts = displayOpts;
    box( TRect( 3, 2, size.x - 3, 12 ), "Colors" );
    insert( new TStaticText( TRect( 30, 2, 41, 3 ), "Foreground" ) );
    insert( new TStaticText( TRect( 46, 2, 57, 3 ), "Background" ) );
    TRect ir( 5, 4, 25, 6 );
    box( ir );
    items = new ItemList( ir );
    insert( items );
    TRect fr( 30, 4, 40, 10 );
    box( fr );
    TScrollBar *s1 = new TScrollBar( TRect( fr.b.x, fr.a.y, fr.b.x + 1, fr.b.y ) );
    insert( s1 );
    fg = new ColorList( fr, s1, 16 );
    insert( fg );
    TRect br( 46, 4, 56, 10 );
    box( br );
    TScrollBar *s2 = new TScrollBar( TRect( br.b.x, br.a.y, br.b.x + 1, br.b.y ) );
    insert( s2 );
    bg = new ColorList( br, s2, 8 );
    insert( bg );
    sample = new ColorSample( TRect( 5, 9, 21, 10 ) );
    insert( sample );
    box( TRect( 3, 15, size.x - 3, 16 ), "Display Options" );
    bars = new TCheckBoxes( TRect( 5, 15, 24, 16 ), new TSItem( "~S~croll Bars", 0 ) );
    insert( bars );
    tabs = new TInputLine( TRect( 52, 15, 56, 16 ), 3 );
    insert( tabs );
    insert( new TLabel( TRect( 40, 15, 51, 16 ), "~T~ab Stops:", tabs ) );
    separator( size.y - 3 );
    static const BtnDef btns[] = { { "OK", cmOK, bfDefault }, { "Cancel", cmCancel, bfNormal },
                                   { "Help", cmDlgHelp, bfNormal } };
    buttonRow( this, size.y - 2, btns, 3 );

    ushort b = opts.scrollBars ? 1 : 0;
    bars->setData( &b );
    char t[4];
    snprintf( t, sizeof t, "%d", opts.tabStops );
    tabs->setData( t );
    syncFromItem();
    focusFirst( this );
}

void DisplayDialog::syncFromItem()
{
    if (syncing)
        return;
    int it = items->focused;
    if (it != curItem)
    {
        curItem = it;
        syncing = true;
        fg->focusItem( it == 0 ? opts.normalFg : opts.highFg );
        bg->focusItem( it == 0 ? opts.normalBg : opts.highBg );
        syncing = false;
    }
    if (it == 0)
        opts.normalFg = fg->focused, opts.normalBg = bg->focused;
    else
        opts.highFg = fg->focused, opts.highBg = bg->focused;
    sample->attr = (uchar) (it == 0 ? (opts.normalBg << 4) | opts.normalFg
                                    : (opts.highBg << 4) | opts.highFg);
    sample->drawView();
}

void DisplayDialog::handleEvent( TEvent &event )
{
    EdDialog::handleEvent( event );
    if (event.what == evBroadcast && event.message.command == cmEditorMoved)
    {
        if (event.message.infoPtr == items)
            curItem = -1;
        syncFromItem();
    }
}

bool displayDialog()
{
    DisplayDialog *d = new DisplayDialog;
    TView *p = TProgram::application->validView( d );
    if (!p)
        return false;
    ushort r = TProgram::deskTop->execView( d );
    if (r != cmCancel)
    {
        DisplayOptions o = d->opts;
        ushort b;
        d->bars->getData( &b );
        o.scrollBars = b & 1;
        char t[4];
        d->tabs->getData( t );
        int ts = atoi( t );
        if (ts >= 1 && ts <= 99)
            o.tabStops = ts;
        displayOpts = o;
        applyDisplayOptions();
    }
    TObject::destroy( d );
    return r != cmCancel;
}

/* ---------------------------------------------------------- help path */

bool helpPathDialog( char *path )
{
    EdDialog *d = new EdDialog( TRect( 0, 0, 60, 10 ), "Help Path", hcDlgHelpPath );
    d->insert( new TStaticText( TRect( 3, 2, 57, 3 ),
        "Enter the directory for your programs:" ) );
    TRect r( 4, 4, 56, 5 );
    d->box( r );
    TInputLine *in = new TInputLine( r, 80 );
    d->insert( in );
    d->separator( 7 );
    static const BtnDef btns[] = { { "OK", cmOK, bfDefault }, { "Cancel", cmCancel, bfNormal },
                                   { "Help", cmDlgHelp, bfNormal } };
    buttonRow( d, 8, btns, 3 );
    focusFirst( d );
    char buf[80];
    strnzcpy( buf, path, sizeof buf );
    if (edExecDialog( d, buf ) == cmCancel)
        return false;
    strnzcpy( path, buf, 80 );
    return true;
}

/* -------------------------------------------------------------- about */

void aboutDialog()
{
    EdDialog *d = new EdDialog( TRect( 0, 0, 52, 12 ), "", hcDlgMessage );
    d->insert( new EdText( TRect( 2, 2, 50, 3 ), "\x03" "ARM QuickBASIC" ) );
    d->insert( new EdText( TRect( 2, 3, 50, 4 ), "\x03" "Version 1.00" ) );
    d->insert( new EdText( TRect( 2, 5, 50, 6 ), "\x03" "Copyright (C) Europa Micro Systems, 1990." ) );
    d->insert( new EdText( TRect( 2, 6, 50, 7 ), "\x03" "Interpreter: Bywater BASIC 3.40 (GNU GPL v2)" ) );
    d->insert( new EdText( TRect( 2, 7, 50, 8 ), "\x03" "Turbo Vision: Borland, magiblot (MIT)" ) );
    d->separator( 9 );
    static const BtnDef btns[] = { { "OK", cmOK, bfDefault } };
    buttonRow( d, 10, btns, 1 );
    focusFirst( d );
    edExecDialog( d, 0 );
}

/* ------------------------------------------------------------ welcome */

bool welcomeDialog()
{
    EdDialog *d = new EdDialog( TRect( 0, 0, 58, 12 ), "", hcDlgMessage );
    d->insert( new EdText( TRect( 2, 2, 56, 3 ), "\x03" "Welcome to ARM QuickBASIC" ) );
    d->insert( new EdText( TRect( 2, 4, 56, 5 ), "\x03" "Copyright (C) Europa Micro Systems, 1990." ) );
    d->insert( new EdText( TRect( 2, 5, 56, 6 ), "\x03" "All rights reserved." ) );
    d->separator( 7 );
    const char *t1 = "Press Enter to see the Survival Guide";
    const char *t2 = "Press ESC to clear this dialog box";
    int w1 = EdButton::width( t1 ), w2 = EdButton::width( t2 );
    d->insert( new EdButton( TRect( (58 - w1) / 2, 8, (58 - w1) / 2 + w1, 9 ), t1, cmOK, bfDefault ) );
    d->insert( new EdButton( TRect( (58 - w2) / 2, 10, (58 - w2) / 2 + w2, 11 ), t2, cmCancel, bfNormal ) );
    focusFirst( d );
    return edExecDialog( d, 0 ) == cmOK;
}

/* ------------------------------------------------ New SUB / FUNCTION */

bool newProcDialog( bool function, char *name, int size )
{
    EdDialog *d = new EdDialog( TRect( 0, 0, 48, 8 ), function ? "New FUNCTION" : "New SUB", hcDlgNewSub );
    fieldWithBox( d, 2, "~N~ame:", 2, 10, 34, size - 1 );
    d->separator( 5 );
    static const BtnDef btns[] = { { "OK", cmOK, bfDefault }, { "Cancel", cmCancel, bfNormal },
                                   { "Help", cmDlgHelp, bfNormal } };
    buttonRow( d, 6, btns, 3 );
    focusFirst( d );
    char buf[64];
    snprintf( buf, sizeof buf, "%s", name );
    if (edExecDialog( d, buf ) == cmCancel)
        return false;
    char *s = buf;
    while (*s == ' ') ++s;
    int l = (int) strlen( s );
    while (l > 0 && s[l - 1] == ' ') s[--l] = 0;
    if (!*s)
        return false;
    snprintf( name, size, "%s", s );
    return true;
}

/* ----------------------------------------------------------- SUBs (F2) */

class SubList : public TListViewer
{
public:
    SubList( const TRect &r, TScrollBar *sb, const char *const *n, int c ) :
        TListViewer( r, 1, 0, sb ), names( n ) { setRange( c ); }
    virtual void getText( char *dest, short item, short maxLen )
    {
        snprintf( dest, maxLen + 1, "%s%s", item ? "  " : "", names[item] );
    }
    virtual void selectItem( short item )
    {
        TListViewer::selectItem( item );
        message( owner, evCommand, cmOK, 0 );
    }
    virtual void handleEvent( TEvent &event )
    {
        if (event.what == evKeyDown && event.keyDown.keyCode == kbEnter)
        {
            clearEvent( event );
            message( owner, evCommand, cmOK, 0 );
            return;
        }
        TListViewer::handleEvent( event );
    }
    const char *const *names;
};

int subsDialog( const char *const *names, int n, int current )
{
    EdDialog *d = new EdDialog( TRect( 0, 0, 56, 18 ), "SUBs", hcDlgSubs );
    d->insert( new TStaticText( TRect( 3, 1, 53, 2 ), "Choose program item to edit" ) );
    TRect r( 4, 3, 51, 12 );
    d->box( r );
    TScrollBar *sb = new TScrollBar( TRect( r.b.x, r.a.y, r.b.x + 1, r.b.y ) );
    d->insert( sb );
    SubList *l = new SubList( r, sb, names, n );
    d->insert( l );
    d->insert( new TStaticText( TRect( 3, 13, 53, 14 ),
        n > 1 ? "The main module and its SUBs and FUNCTIONs" : "The main module" ) );
    d->separator( 15 );
    static const BtnDef btns[] = { { "~E~dit in Active", cmOK, bfDefault }, { "Cancel", cmCancel, bfNormal },
                                   { "Help", cmDlgHelp, bfNormal } };
    buttonRow( d, 16, btns, 3 );
    focusFirst( d );
    if (current >= 0 && current < n)
        l->focusItem( (short) current );
    TView *p = TProgram::application->validView( d );
    if (!p)
        return -1;
    ushort res = TProgram::deskTop->execView( p );
    int sel = l->focused;
    TObject::destroy( p );
    return res == cmCancel ? -1 : sel;
}
