/*
 * help.cpp - EDIT's help: EDIT.HLP (plain text in code page 437, topics
 * started by ".topic Name" lines, cross references written ◄Name►) shown in
 * a help window above the document, like the MS-DOS 5 Editor's.
 *
 *   Tab / Shift+Tab   next / previous cross reference
 *   Enter             show the topic of the cross reference under the cursor
 *   Alt+F1            back to the previous topic      Ctrl+F1  next topic
 *   F6                switch between help and document Esc      close help
 *
 * Copyright (c) 2026 Europa Micro Systems. MIT License.
 */
#include "edit.h"

#define Uses_TScroller
#include <tvision/tv.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>

char helpPath[80];

static char *helpText;          /* the whole file, NUL terminated */
static uint helpSize;

static bool loadHelp()
{
    if (helpText)
        return true;
    char name[MAXPATH];
    const char *dirs[3] = { helpPath, "", 0 };
    int fd = -1;
    for (int i = 0; i < 2 && fd < 0; ++i)
    {
        const char *d = dirs[i];
        size_t l = strlen( d );
        snprintf( name, sizeof name, "%s%sEDIT.HLP", d, l && d[l - 1] != '\\' && d[l - 1] != ':' ? "\\" : "" );
        fd = open( name, O_RDONLY | O_BINARY );
    }
    if (fd < 0)
    {
        /* along the PATH */
        const char *path = getenv( "PATH" );
        while (path && *path && fd < 0)
        {
            const char *e = strchr( path, ';' );
            size_t l = e ? (size_t) (e - path) : strlen( path );
            if (l > 0 && l < sizeof name - 12)
            {
                memcpy( name, path, l );
                snprintf( name + l, sizeof name - l, "%sEDIT.HLP", path[l - 1] == '\\' ? "" : "\\" );
                fd = open( name, O_RDONLY | O_BINARY );
            }
            path = e ? e + 1 : 0;
        }
    }
    if (fd < 0)
        return false;
    long size = lseek( fd, 0, SEEK_END );
    lseek( fd, 0, SEEK_SET );
    helpText = (char *) malloc( size + 1 );
    if (!helpText || read( fd, helpText, size ) != size)
    {
        free( helpText );
        helpText = 0;
        close( fd );
        return false;
    }
    close( fd );
    helpText[size] = 0;
    helpSize = size;
    return true;
}

/* Find ".topic Name"; returns the start of its first text line. */
static const char *findTopic( const char *name, const char **nameOut = 0 )
{
    size_t nl = strlen( name );
    for (const char *p = helpText; p && *p; )
    {
        if (!strncmp( p, ".topic ", 7 ))
        {
            const char *t = p + 7;
            const char *e = t;
            while (*e && *e != '\r' && *e != '\n') ++e;
            if ((size_t) (e - t) == nl && !strncasecmp( t, name, nl ))
            {
                if (nameOut) *nameOut = t;
                while (*e == '\r' || *e == '\n') ++e;
                return e;
            }
        }
        p = strchr( p, '\n' );
        if (p) ++p;
    }
    return 0;
}

/* the topic after the given one (Ctrl+F1) */
static bool nextTopicName( const char *name, char *out, size_t size )
{
    const char *t = findTopic( name );
    if (!t)
        return false;
    for (const char *p = t; p && *p; )
    {
        if (!strncmp( p, ".topic ", 7 ))
        {
            const char *s = p + 7, *e = s;
            while (*e && *e != '\r' && *e != '\n') ++e;
            size_t l = (size_t) (e - s) < size - 1 ? (size_t) (e - s) : size - 1;
            memcpy( out, s, l );
            out[l] = 0;
            return true;
        }
        p = strchr( p, '\n' );
        if (p) ++p;
    }
    return false;
}

/* ------------------------------------------------------------ the view */

struct HelpLine { const char *text; int len; };
struct HelpLink { int line, col, len; };

class HelpView : public TScroller
{
public:
    HelpView( const TRect &r, TScrollBar *h, TScrollBar *v );
    ~HelpView();
    virtual void draw();
    virtual void handleEvent( TEvent &event );
    virtual TPalette &getPalette() const;
    bool show( const char *topic, bool remember = true );
    void selectLink( int i );
    void follow();

    char topic[40];
    HelpLine *lines = 0;
    int nLines = 0;
    HelpLink *links = 0;
    int nLinks = 0;
    int cur = -1;
    char history[8][40];
    int nHistory = 0;
};

#define cpHelpView "\x06\x07\x08"

TPalette &HelpView::getPalette() const
{
    static TPalette palette( cpHelpView, sizeof( cpHelpView ) - 1 );
    return palette;
}

HelpView::HelpView( const TRect &r, TScrollBar *h, TScrollBar *v ) : TScroller( r, h, v )
{
    topic[0] = 0;
    helpCtx = hcHelp;
    growMode = gfGrowHiX | gfGrowHiY;
    options |= ofSelectable;
}

HelpView::~HelpView()
{
    free( lines );
    free( links );
}

bool HelpView::show( const char *name, bool remember )
{
    const char *t = findTopic( name, 0 );
    if (!t)
        return false;
    if (remember && topic[0])
    {
        if (nHistory == 8)
        {
            memmove( history[0], history[1], sizeof history[0] * 7 );
            --nHistory;
        }
        strcpy( history[nHistory++], topic );
    }
    strnzcpy( topic, name, sizeof topic );
    free( lines );
    free( links );
    lines = 0; links = 0; nLines = nLinks = 0;
    int capL = 0, capK = 0;
    for (const char *p = t; *p && strncmp( p, ".topic ", 7 ); )
    {
        const char *e = p;
        while (*e && *e != '\r' && *e != '\n') ++e;
        if (nLines == capL)
        {
            capL = capL ? capL * 2 : 64;
            lines = (HelpLine *) realloc( lines, capL * sizeof *lines );
        }
        lines[nLines].text = p;
        lines[nLines].len = (int) (e - p);
        for (const char *q = p; q < e; ++q)
            if (*q == '\x11')
            {
                const char *z = (const char *) memchr( q, '\x10', e - q );
                if (!z)
                    break;
                if (nLinks == capK)
                {
                    capK = capK ? capK * 2 : 16;
                    links = (HelpLink *) realloc( links, capK * sizeof *links );
                }
                links[nLinks].line = nLines;
                links[nLinks].col = (int) (q - p);
                links[nLinks].len = (int) (z - q) + 1;
                ++nLinks;
                q = z;
            }
        ++nLines;
        if (*e == '\r') ++e;
        if (*e == '\n') ++e;
        p = e;
    }
    /* drop trailing blank lines */
    while (nLines > 0 && lines[nLines - 1].len == 0)
        --nLines;
    setLimit( 80, nLines );
    scrollTo( 0, 0 );
    /* the cursor starts at the top; Tab goes to the first cross reference */
    cur = -1;
    setCursor( 0, 0 );
    showCursor();
    if (owner)
        ((TWindow *) owner)->frame->drawView();
    drawView();
    return true;
}

void HelpView::selectLink( int i )
{
    if (i < 0 || i >= nLinks)
        return;
    cur = i;
    HelpLink &k = links[i];
    int y = k.line;
    if (y < delta.y)
        scrollTo( delta.x, y );
    else if (y >= delta.y + size.y)
        scrollTo( delta.x, y - size.y + 1 );
    setCursor( k.col + 1 - delta.x, y - delta.y );
    showCursor();
    drawView();
}

void HelpView::follow()
{
    if (cur < 0 || cur >= nLinks)
        return;
    HelpLink &k = links[cur];
    char name[40];
    int l = k.len - 2 < 39 ? k.len - 2 : 39;
    memcpy( name, lines[k.line].text + k.col + 1, l );
    name[l] = 0;
    show( name );
}

void HelpView::draw()
{
    TColorAttr cText = getColor( 1 ), cLink = getColor( 2 ), cSel = getColor( 3 );
    for (int y = 0; y < size.y; ++y)
    {
        TDrawBuffer b;
        b.moveChar( 0, ' ', cText, size.x );
        int ln = y + delta.y;
        if (ln < nLines)
        {
            const HelpLine &L = lines[ln];
            for (int x = 0; x < size.x; ++x)
            {
                int col = x + delta.x;
                if (col >= L.len)
                    break;
                TColorAttr c = cText;
                for (int k = 0; k < nLinks; ++k)
                    if (links[k].line == ln && col >= links[k].col && col < links[k].col + links[k].len)
                        c = k == cur ? cSel : cLink;
                b.moveChar( x, L.text[col], c, 1 );
            }
        }
        writeLine( 0, y, size.x, 1, b );
    }
    if (cur >= 0)
        setCursor( links[cur].col + 1 - delta.x, links[cur].line - delta.y );
    else
        setCursor( -delta.x, -delta.y );
}

void HelpView::handleEvent( TEvent &event )
{
    if (event.what == evKeyDown)
    {
        ushort k = event.keyDown.keyCode;
        switch (k)
        {
            case kbTab:
                selectLink( nLinks ? (cur + 1) % nLinks : -1 );
                clearEvent( event );
                return;
            case kbShiftTab:
                selectLink( nLinks ? (cur + nLinks - 1 + (cur < 0)) % nLinks : -1 );
                clearEvent( event );
                return;
            case kbEnter:
                follow();
                clearEvent( event );
                return;
            case kbAltF1:
                if (nHistory > 0)
                    show( history[--nHistory], false );
                clearEvent( event );
                return;
            case kbCtrlF1:
            {
                char next[40];
                if (nextTopicName( topic, next, sizeof next ))
                    show( next );
                clearEvent( event );
                return;
            }
            case kbEsc:
                clearEvent( event );
                event.what = evCommand;
                event.message.command = cmHelpClose;
                event.message.infoPtr = 0;
                putEvent( event );
                clearEvent( event );
                return;
        }
        /* a letter: the next cross reference starting with it */
        char c = event.keyDown.charScan.charCode;
        if (c > ' ' && nLinks)
        {
            for (int i = 1; i <= nLinks; ++i)
            {
                int j = (cur + i + nLinks) % nLinks;
                char f = lines[links[j].line].text[links[j].col + 1];
                if (toupper( (uchar) f ) == toupper( (uchar) c ))
                {
                    selectLink( j );
                    break;
                }
            }
            clearEvent( event );
            return;
        }
    }
    else if (event.what == evMouseDown)
    {
        TPoint m = makeLocal( event.mouse.where );
        int ln = m.y + delta.y, col = m.x + delta.x;
        for (int k = 0; k < nLinks; ++k)
            if (links[k].line == ln && col >= links[k].col && col < links[k].col + links[k].len)
            {
                selectLink( k );
                if (event.mouse.eventFlags & meDoubleClick || event.mouse.buttons & mbRightButton)
                    follow();
                clearEvent( event );
                return;
            }
    }
    TScroller::handleEvent( event );
}

/* ---------------------------------------------------------- the window */

class HelpWindow : public TWindow
{
public:
    HelpWindow( const TRect &r );
    virtual const char *getTitle( short );
    virtual TPalette &getPalette() const;
    virtual void handleEvent( TEvent &event );
    HelpView *view;
    char title[48];
};

#define cpHelpWindow "\x8A\x8A\x0A\x0B\x0C\x8A\x8B\x8C"

HelpWindow::HelpWindow( const TRect &r ) :
    TWindowInit( &EdWindow::initFrame ),
    TWindow( r, 0, wnNoNumber )
{
    flags = 0;
    state &= ~sfShadow;
    TRect e = getExtent();
    TScrollBar *v = new TScrollBar( TRect( e.b.x - 1, 1, e.b.x, e.b.y - 1 ) );
    v->growMode = gfGrowLoX | gfGrowHiX | gfGrowHiY;
    insert( v );
    TScrollBar *h = new TScrollBar( TRect( 1, e.b.y - 1, e.b.x - 1, e.b.y ) );
    h->growMode = gfGrowLoY | gfGrowHiX | gfGrowHiY;
    insert( h );
    view = new HelpView( TRect( 1, 1, e.b.x - 1, e.b.y - 1 ), h, v );
    insert( view );
    helpCtx = hcHelp;
}

TPalette &HelpWindow::getPalette() const
{
    static TPalette palette( cpHelpWindow, sizeof( cpHelpWindow ) - 1 );
    return palette;
}

const char *HelpWindow::getTitle( short )
{
    snprintf( title, sizeof title, "HELP: %s", view ? view->topic : "" );
    return title;
}

void HelpWindow::handleEvent( TEvent &event )
{
    if ((state & sfModal) && event.what == evCommand && event.message.command == cmHelpClose)
    {
        endModal( cmCancel );
        clearEvent( event );
        return;
    }
    TWindow::handleEvent( event );
}

static HelpWindow *helpWin;

bool helpIsOpen()
{
    return helpWin != 0;
}

void helpRedraw()
{
    if (helpWin)
        helpWin->redraw();
}

static TRect helpRect()
{
    TRect r = TProgram::deskTop->getExtent();
    int bottom = r.b.y - 8 < r.a.y + 3 ? r.a.y + 3 : r.b.y - 8;
    return TRect( r.a.x, r.a.y, r.b.x, bottom );
}

static bool noHelpFile()
{
    edMessage( "Help file EDIT.HLP not found.\nUse Options / Help Path to say where it is.", emOK );
    return false;
}

void helpOpen( const char *topic )
{
    if (!loadHelp())
    {
        noHelpFile();
        return;
    }
    if (!helpWin)
    {
        helpWin = new HelpWindow( helpRect() );
        if (!helpWin->view->show( topic ) && !helpWin->view->show( "Survival Guide" ))
        {
            TObject::destroy( helpWin );
            helpWin = 0;
            edMessage( "Help topic not found.", emOK );
            return;
        }
        layoutWindows();
        TProgram::deskTop->insert( helpWin );
    }
    else
        helpWin->view->show( topic );
    helpWin->select();
}

void helpClose()
{
    if (!helpWin)
        return;
    TProgram::deskTop->remove( helpWin );
    TObject::destroy( helpWin );
    helpWin = 0;
    layoutWindows();
    docWindow->select();
}

void helpModal( const char *topic )
{
    if (!loadHelp())
    {
        noHelpFile();
        return;
    }
    HelpWindow *w = new HelpWindow( helpRect() );
    if (!w->view->show( topic ) && !w->view->show( "Survival Guide" ))
    {
        TObject::destroy( w );
        return;
    }
    TProgram::deskTop->execView( w );
    TObject::destroy( w );
}

const char *helpTopicFor( ushort ctx )
{
    if (ctx >= hcFileMenu && ctx < hcEditMenu) return "File Menu";
    if (ctx >= hcEditMenu && ctx < hcSearchMenu) return "Edit Menu";
    if (ctx >= hcSearchMenu && ctx < hcOptionsMenu) return "Search Menu";
    if (ctx >= hcOptionsMenu && ctx < hcHelpMenu) return "Options Menu";
    if (ctx >= hcHelpMenu && ctx < hcDlgFind) return "Help Menu";
    switch (ctx)
    {
        case hcMenuBar:        return "Using Menus and Commands";
        case hcDlgFind:        return "Find Dialog";
        case hcDlgChange:
        case hcDlgVerify:      return "Change Dialog";
        case hcDlgOpen:        return "Open Dialog";
        case hcDlgSaveAs:      return "Save As Dialog";
        case hcDlgPrint:       return "Print Dialog";
        case hcDlgDisplay:     return "Display Dialog";
        case hcDlgHelpPath:    return "Help Path Dialog";
        case hcDlgSaveChanges: return "File Menu";
        case hcHelp:           return "Using Help";
    }
    if (ctx >= hcDlgFind)
        return "Using Dialog Boxes";
    return "Survival Guide";
}
