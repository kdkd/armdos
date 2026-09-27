/*
 * tcwin.cpp - TC.EXE's windows: the edit windows (a TFileEditor with C
 * syntax colouring and the error line mark), the Message window, window
 * placement.
 *
 * Copyright (c) 2026 Europa Micro Systems. MIT License.
 */
#include "tc.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

TcMessageWindow *msgWindow;
TcEditWindow *clipWindow;

/* ================================================================ editor */

TcEditor::TcEditor( const TRect &bounds, TScrollBar *h, TScrollBar *v, TIndicator *ind, TStringView fileName ) :
    TFileEditor( bounds, h, v, ind, fileName )
{
    autoIndent = (opts.envFlags & envAutoIndent) ? True : False;
    helpCtx = hcEditorWin;
}

void TcEditor::updateCommands()
{
    TFileEditor::updateCommands();
}

int TcEditor::lineNumber( uint ptr )
{
    int n = 1;
    for (uint p = 0; p < bufLen && p < ptr; ++p)
    {
        char c = bufChar( p );
        if (c == '\n')
            ++n;
        else if (c == '\r' && !(p + 1 < bufLen && bufChar( p + 1 ) == '\n'))
            ++n;
    }
    return n;
}

void TcEditor::gotoLine( int line, bool mark )
{
    if (line < 1)
        line = 1;
    lock();
    uint p = lineMove( 0, line - 1 );
    setCurPtr( p, 0 );
    trackCursor( True );
    if (mark)
    {
        tvMarkEditor = this;
        tvMarkLine = lineStart( p );
        tvMarkAttr = 0x3F;
    }
    else if (tvMarkEditor == this)
        tvMarkEditor = 0;
    drawView();
    unlock();
}

void TcEditor::wordAtCursor( char *buf, int size )
{
    uint p = curPtr, s = p;
    auto isw = [&]( uint q ) { char c = bufChar( q ); return isalnum( (uchar) c ) || c == '_'; };
    while (s > 0 && isw( s - 1 ))
        --s;
    int n = 0;
    for (uint q = s; q < bufLen && isw( q ) && n < size - 1; ++q)
        buf[n++] = bufChar( q );
    buf[n] = 0;
}

void TcEditor::handleEvent( TEvent &event )
{
    if (event.what == evKeyDown && tvMarkEditor == this)
    {
        /* the error mark goes away as soon as the user types */
        tvMarkEditor = 0;
        drawView();
    }
    if (event.what == evMouseDown && tvMarkEditor == this)
    {
        tvMarkEditor = 0;
        drawView();
    }
    uint before = insCount + delCount;
    (void) before;
    TFileEditor::handleEvent( event );
}

/* ========================================================= edit window */

/* line:column in the bottom frame, and a * when the file has changed */
class TcIndicator : public TIndicator
{
public:
    TcIndicator( const TRect &r ) : TIndicator( r ) {}
    virtual void draw()
    {
        TColorAttr color = getColor( (state & sfDragging) ? 2 : 1 );
        char frame = (state & sfDragging) ? '\xC4' : '\xCD';
        TDrawBuffer b;
        char s[16];
        b.moveChar( 0, frame, color, size.x );
        if (modified)
            b.putChar( 0, '*' );
        snprintf( s, sizeof s, " %d:%d ", location.y + 1, location.x + 1 );
        b.moveStr( 8 - int( strchr( s, ':' ) - s ), s, color );
        writeBuf( 0, 0, size.x, 1, b );
    }
};

static int nextWindowNumber()
{
    bool used[10] = {};
    TView *first = TProgram::deskTop->first(), *p = first;
    if (p)
        do
        {
            TWindow *w = (TWindow *) p;
            if (w->number > 0 && w->number < 10)
                used[w->number] = true;
            p = p->next;
        } while (p != first);
    for (int i = 1; i < 10; ++i)
        if (!used[i])
            return i;
    return wnNoNumber;
}

TcEditWindow::TcEditWindow( const TRect &bounds, TStringView fileName, int number ) :
    TWindowInit( &TcEditWindow::initFrame ),
    TWindow( bounds, 0, number )
{
    options |= ofTileable;
    helpCtx = hcEditorWin;
    TScrollBar *hs = new TScrollBar( TRect( 18, size.y - 1, size.x - 2, size.y ) );
    hs->hide();
    insert( hs );
    TScrollBar *vs = new TScrollBar( TRect( size.x - 1, 1, size.x, size.y - 1 ) );
    vs->hide();
    insert( vs );
    TIndicator *ind = new TcIndicator( TRect( 2, size.y - 1, 16, size.y ) );
    ind->hide();
    insert( ind );
    TRect r( getExtent() );
    r.grow( -1, -1 );
    editor = new TcEditor( r, hs, vs, ind, fileName );
    insert( editor );
}

void TcEditWindow::close()
{
    if (this == clipWindow)
        hide();
    else
    {
        if (tvMarkEditor == editor)
            tvMarkEditor = 0;
        TWindow::close();
    }
}

const char *TcEditWindow::getTitle( short )
{
    if (this == clipWindow)
        return "Clipboard";
    if (!editor->fileName[0])
        return "NONAME00.C";
    return editor->fileName;
}

void TcEditWindow::handleEvent( TEvent &event )
{
    TWindow::handleEvent( event );
    if (event.what == evBroadcast && event.message.command == cmUpdateTitle)
    {
        if (frame)
            frame->drawView();
        clearEvent( event );
    }
}

void TcEditWindow::sizeLimits( TPoint &min, TPoint &max )
{
    TWindow::sizeLimits( min, max );
    min.x = 24;
    min.y = 6;
}

TcEditWindow *findEditor( const char *fileName )
{
    char full[MAXPATH];
    fullPath( full, fileName );
    TView *first = TProgram::deskTop->first(), *p = first;
    if (p)
        do
        {
            if (p->helpCtx == hcEditorWin && p != clipWindow)
            {
                TcEditWindow *w = (TcEditWindow *) p;
                if (!strcasecmp( w->editor->fileName, full ))
                    return w;
            }
            p = p->next;
        } while (p != first);
    return 0;
}

TcEditWindow *currentEditor()
{
    TView *p = TProgram::deskTop->current;
    if (p && p->helpCtx == hcEditorWin && p != clipWindow)
        return (TcEditWindow *) p;
    /* else the topmost edit window */
    TView *first = TProgram::deskTop->first();
    p = first;
    if (p)
        do
        {
            if (p->helpCtx == hcEditorWin && p != clipWindow && (p->state & sfVisible))
                return (TcEditWindow *) p;
            p = p->next;
        } while (p != first);
    return 0;
}

static int untitledCount;

TcEditWindow *openEditor( const char *fileName, bool visible )
{
    if (fileName && visible)
    {
        TcEditWindow *w = findEditor( fileName );
        if (w)
        {
            w->select();
            return w;
        }
    }
    TRect r = TProgram::deskTop->getExtent();
    if (msgWindow && (msgWindow->state & sfVisible))
        r.b.y = msgWindow->origin.y;
    char full[MAXPATH] = "";
    if (fileName)
        fullPath( full, fileName );
    TcEditWindow *w = new TcEditWindow( r, full, visible ? nextWindowNumber() : wnNoNumber );
    if (!TProgram::application->validView( w ))
        return 0;
    if (!visible)
    {
        w->hide();
        TProgram::deskTop->insert( w );
    }
    else
    {
        if (!fileName)
            ++untitledCount;
        TProgram::deskTop->insert( w );
    }
    return w;
}

void tileWindows()
{
    TProgram::deskTop->tile( TProgram::deskTop->getExtent() );
}

/* ================================================= syntax colouring (C) */

static const char *const cKeywords[] = {
    "auto", "break", "case", "char", "const", "continue", "default", "do",
    "double", "else", "enum", "extern", "float", "for", "goto", "if",
    "inline", "int", "long", "register", "restrict", "return", "short",
    "signed", "sizeof", "static", "struct", "switch", "typedef", "union",
    "unsigned", "void", "volatile", "while", "asm", "__asm__", "_Bool",
    "far", "near", "huge", "interrupt", "cdecl", "pascal", 0
};

static bool isKeyword( const char *w, int n )
{
    for (const char *const *k = cKeywords; *k; ++k)
        if ((int) strlen( *k ) == n && !memcmp( *k, w, n ))
            return true;
    return false;
}

/* states between lines: 0 normal, 1 inside a block comment */
static int scanLine( TEditor *ed, uint p, uint end, int state, uint8_t *attrs, uint8_t normal )
{
    uint8_t bg = normal & 0xF0;
    uint8_t aComment = bg | 0x07, aKey = bg | 0x0F, aStr = bg | 0x0B, aNum = bg | 0x0B, aPre = bg | 0x0A;
    uint start = p;
    bool pre = false, first = true;
    auto put = [&]( uint q, uint8_t a ) { if (attrs) attrs[q - start] = a; };
    while (p < end)
    {
        char c = ed->bufChar( p );
        if (state == 1)
        {
            put( p, aComment );
            if (c == '*' && p + 1 < end && ed->bufChar( p + 1 ) == '/')
            {
                put( p + 1, aComment );
                p += 2;
                state = 0;
                continue;
            }
            ++p;
            continue;
        }
        if (c == '/' && p + 1 < end && ed->bufChar( p + 1 ) == '*')
        {
            put( p, aComment ); put( p + 1, aComment );
            p += 2;
            state = 1;
            continue;
        }
        if (c == '/' && p + 1 < end && ed->bufChar( p + 1 ) == '/')
        {
            while (p < end)
                put( p++, aComment );
            break;
        }
        if (first && c == '#')
            pre = true;
        if (c != ' ' && c != '\t')
            first = false;
        if (c == '"' || c == '\'')
        {
            char q = c;
            put( p++, aStr );
            while (p < end)
            {
                char d = ed->bufChar( p );
                put( p++, aStr );
                if (d == '\\' && p < end)
                {
                    put( p++, aStr );
                    continue;
                }
                if (d == q)
                    break;
            }
            continue;
        }
        if (isalpha( (uchar) c ) || c == '_')
        {
            char w[32];
            int n = 0;
            uint s = p;
            while (p < end)
            {
                char d = ed->bufChar( p );
                if (!(isalnum( (uchar) d ) || d == '_'))
                    break;
                if (n < 31)
                    w[n] = d;
                ++n;
                ++p;
            }
            uint8_t a = pre ? aPre : (n < 31 && isKeyword( w, n )) ? aKey : normal;
            for (uint q = s; q < p; ++q)
                put( q, a );
            continue;
        }
        if (isdigit( (uchar) c ))
        {
            while (p < end)
            {
                char d = ed->bufChar( p );
                if (!(isalnum( (uchar) d ) || d == '.'))
                    break;
                put( p++, pre ? aPre : aNum );
            }
            continue;
        }
        put( p++, pre ? aPre : normal );
    }
    return state;
}

bool highlightC( TEditor *ed, unsigned linePtr, unsigned len, uint8_t *attrs, uint8_t normal )
{
    if (!(opts.envFlags & envHighlight))
        return false;
    if (clipWindow && ed == clipWindow->editor)
        return false;
    /* file type: colour only C-like files and new ones */
    const char *name = ((TFileEditor *) ed)->fileName;
    const char *dot = strrchr( name, '.' );
    if (name[0] && dot && strcasecmp( dot, ".C" ) && strcasecmp( dot, ".H" ) && strcasecmp( dot, ".CPP" )
        && strcasecmp( dot, ".S" ))
        return false;
    /* the state at the start of the line: consecutive lines of one redraw
     * carry it over; otherwise scan from up to 100 lines back */
    static TEditor *lastEd;
    static uint lastNext;
    static int lastState;
    static uint lastBufLen;
    int state = 0;
    if (lastEd == ed && lastNext == linePtr && lastBufLen == ed->bufLen)
        state = lastState;
    else
    {
        uint p = ed->lineMove( linePtr, -100 );
        while (p < linePtr)
        {
            uint e = ed->lineEnd( p );
            state = scanLine( ed, p, e, state, 0, normal );
            uint n = ed->nextLine( p );
            if (n <= p)
                break;
            p = n;
        }
    }
    lastState = scanLine( ed, linePtr, linePtr + len, state, attrs, normal );
    lastEd = ed;
    lastNext = ed->nextLine( linePtr );
    lastBufLen = ed->bufLen;
    return true;
}

/* ======================================================== Message window */

static TcMessage *messages;
static int nMessages, capMessages;

/* the colours of the list: entries 9 and 10 of the window (see getPalette) */
#define cpMsgList "\x09\x09\x0A\x09\x09"

class TcMessageList : public TListViewer
{
public:
    TcMessageList( const TRect &r, TScrollBar *v ) : TListViewer( r, 1, 0, v )
    {
        helpCtx = hcMessageWin;
        setRange( 0 );
    }
    virtual TPalette &getPalette() const
    {
        static TPalette p( cpMsgList, sizeof( cpMsgList ) - 1 );
        return p;
    }
    virtual void getText( char *dest, short item, short maxLen )
    {
        if (item < 0 || item >= nMessages)
        {
            *dest = 0;
            return;
        }
        TcMessage &m = messages[item];
        char buf[260];
        const char *base = m.file;
        const char *s = strrchr( base, '\\' );
        if (s)
            base = s + 1;
        if (m.kind == 'E' || m.kind == 'W')
        {
            if (m.line > 0)
                snprintf( buf, sizeof buf, "%c%s %s %d: %s", item == focused ? '\x07' : ' ',
                          m.kind == 'E' ? "Error" : "Warning", base, m.line, m.text );
            else if (m.file[0])
                snprintf( buf, sizeof buf, "%c%s %s: %s", item == focused ? '\x07' : ' ',
                          m.kind == 'E' ? "Error" : "Warning", base, m.text );
            else
                snprintf( buf, sizeof buf, "%c%s: %s", item == focused ? '\x07' : ' ',
                          m.kind == 'E' ? "Linker Error" : "Linker Warning", m.text );
        }
        else
            snprintf( buf, sizeof buf, " %s", m.text );
        strncpy( dest, buf, maxLen );
        dest[maxLen] = 0;
    }
    virtual void focusItem( short item )
    {
        TListViewer::focusItem( item );
        drawView();
        pendingTrack = true;
    }
    virtual void selectItem( short item )
    {
        messageGoto( item, true );
    }
    virtual void handleEvent( TEvent &event )
    {
        if (event.what == evKeyDown && event.keyDown.keyCode == kbEnter)
        {
            messageGoto( focused, true );
            clearEvent( event );
            return;
        }
        if (event.what == evKeyDown && event.keyDown.charScan.charCode == ' ')
        {
            messageGoto( focused, false );
            clearEvent( event );
            return;
        }
        TListViewer::handleEvent( event );
    }
    bool pendingTrack = false;
};

TcMessageWindow::TcMessageWindow( const TRect &r ) :
    TWindowInit( &TcMessageWindow::initFrame ),
    TWindow( r, "Message", wnNoNumber )
{
    options |= ofTileable;
    helpCtx = hcMessageWin;
    TScrollBar *vs = new TScrollBar( TRect( size.x - 1, 1, size.x, size.y - 1 ) );
    insert( vs );
    TRect lr( getExtent() );
    lr.grow( -1, -1 );
    list = new TcMessageList( lr, vs );
    insert( list );
}

/* cyan window + the list colours (application entries 136, 137) */
#define cpMsgWindow "\x10\x11\x12\x13\x14\x15\x16\x17\x88\x89"

TPalette &TcMessageWindow::getPalette() const
{
    static TPalette p( cpMsgWindow, sizeof( cpMsgWindow ) - 1 );
    return p;
}

void TcMessageWindow::close()
{
    hide();
}

void messagesClear()
{
    nMessages = 0;
    if (msgWindow)
    {
        msgWindow->list->setRange( 0 );
        msgWindow->list->drawView();
    }
    if (tvMarkEditor)
    {
        tvMarkEditor = 0;
        TProgram::deskTop->redraw();
    }
}

void messageAdd( char kind, const char *file, int line, const char *text )
{
    if (nMessages == capMessages)
    {
        int cap = capMessages ? capMessages * 2 : 32;
        TcMessage *m = (TcMessage *) realloc( messages, cap * sizeof( TcMessage ) );
        if (!m)
            return;
        messages = m;
        capMessages = cap;
    }
    TcMessage &m = messages[nMessages++];
    m.kind = kind;
    snprintf( m.file, sizeof m.file, "%s", file ? file : "" );
    m.line = line;
    snprintf( m.text, sizeof m.text, "%s", text ? text : "" );
    if (msgWindow)
    {
        msgWindow->list->setRange( nMessages );
        msgWindow->list->drawView();
    }
}

int messageCount() { return nMessages; }

int messageErrors()
{
    int n = 0;
    for (int i = 0; i < nMessages; ++i)
        if (messages[i].kind == 'E' || messages[i].kind == 'W')
            ++n;
    return n;
}

void messageShow( bool focus )
{
    if (!msgWindow)
        return;
    if (!(msgWindow->state & sfVisible))
    {
        TRect d = TProgram::deskTop->getExtent();
        TRect r( 0, d.b.y - 7, d.b.x, d.b.y );
        msgWindow->locate( r );
        msgWindow->show();
    }
    if (focus)
        msgWindow->select();
    else
        msgWindow->makeFirst();
}

void messageSelectFirst()
{
    for (int i = 0; i < nMessages; ++i)
        if (messages[i].kind == 'E' || messages[i].kind == 'W')
        {
            msgWindow->list->focusItem( i );
            return;
        }
}

/* show message i's line in its editor; focusEditor: make it the active window */
bool messageGoto( int i, bool focusEditor )
{
    if (i < 0 || i >= nMessages)
        return false;
    TcMessage &m = messages[i];
    if (!m.file[0] || m.line <= 0)
        return false;
    TcEditWindow *w = findEditor( m.file );
    if (!w)
    {
        TWindow *cur = (TWindow *) TProgram::deskTop->current;
        w = openEditor( m.file );
        if (!w)
            return false;
        if (!focusEditor && cur)
            cur->select();
    }
    w->editor->gotoLine( m.line, true );
    if (focusEditor)
        w->select();
    else if (msgWindow)
    {
        /* keep the Message window active, the editor right behind it */
        w->putInFrontOf( msgWindow->next );
        msgWindow->makeFirst();
    }
    return true;
}

void messageGo( int dir )
{
    if (!msgWindow || nMessages == 0)
        return;
    int i = msgWindow->list->focused;
    for (int n = 0; n < nMessages; ++n)
    {
        i += dir;
        if (i < 0 || i >= nMessages)
            return;
        if (messages[i].kind == 'E' || messages[i].kind == 'W')
        {
            msgWindow->list->focusItem( i );
            ((TcMessageList *) msgWindow->list)->pendingTrack = false;
            messageGoto( i, true );
            return;
        }
    }
}

/* the Message window "tracks": moving in it shows the line in the editor */
void messageTrack()
{
    if (!msgWindow)
        return;
    TcMessageList *l = (TcMessageList *) msgWindow->list;
    if (l->pendingTrack)
    {
        l->pendingTrack = false;
        if (TProgram::deskTop->current == msgWindow)
            messageGoto( l->focused, false );
    }
}
