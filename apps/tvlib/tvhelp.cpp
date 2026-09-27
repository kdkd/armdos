/*
 * tvhelp.cpp - the help window of TC.EXE and QB.EXE (see tvhelp.h).
 *
 *   Tab / Shift+Tab   next / previous cross reference
 *   Enter             the topic of the selected cross reference (or a
 *                     double click on it)
 *   Alt+F1            back          Shift+F1   the index
 *   Esc               close
 *
 * Copyright (c) 2026 Europa Micro Systems. MIT License.
 */
#define Uses_TWindow
#define Uses_TFrame
#define Uses_TScroller
#define Uses_TScrollBar
#define Uses_TDeskTop
#define Uses_TProgram
#define Uses_TKeys
#define Uses_TEvent
#define Uses_TDrawBuffer
#define Uses_TPalette
#include <tvision/tv.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

#include "tvhelp.h"

static const char *helpText;
static void buildIndex();
static const char *helpPalette = "\x10\x11\x12\x13\x14\x06\x07\x08";
static ushort helpCtxNo;
static char *indexText;
const char *tvHelpTitle = "Help";
TFrame *(*tvHelpFrame)( TRect ) = 0;
bool tvHelpShadow = true;

static TRect defaultRect()
{
    TRect r = TProgram::deskTop->getExtent();
    r.b.y = r.a.y + (r.b.y - r.a.y) * 2 / 3;
    return r;
}
TRect (*tvHelpRect)() = defaultRect;

void tvHelpInit( const char *text, const char *palette, ushort ctx )
{
    helpText = text;
    if (palette)
        helpPalette = palette;
    helpCtxNo = ctx;
}

/* ---------------------------------------------------------------- topics */

static const char *lineEnd( const char *p )
{
    while (*p && *p != '\n')
        ++p;
    return p;
}

static const char *nextLine( const char *p )
{
    p = lineEnd( p );
    return *p ? p + 1 : p;
}

/* does the ".topic" line at p name `name` (a name or an alias)? */
static bool topicMatches( const char *p, const char *name )
{
    const char *e = lineEnd( p );
    const char *s = p + 7;
    size_t nl = strlen( name );
    while (s < e)
    {
        const char *c = s;
        while (c < e && *c != ',')
            ++c;
        const char *a = s, *b = c;
        while (a < b && *a == ' ') ++a;
        while (b > a && b[-1] == ' ') --b;
        if ((size_t) (b - a) == nl && !strncasecmp( a, name, nl ))
            return true;
        s = c + 1;
    }
    return false;
}

/* the first line of the topic's text (after ".topic ...") */
static const char *findTopic( const char *name )
{
    if (!strcasecmp( name, "Index" ) && indexText)
        return indexText;
    for (const char *p = helpText; p && *p; p = nextLine( p ))
        if (!strncmp( p, ".topic ", 7 ) && topicMatches( p, name ))
            return nextLine( p );
    return 0;
}

bool tvHelpHas( const char *topic )
{
    if (helpText && !strcasecmp( topic, "Index" ))
        buildIndex();
    return helpText && findTopic( topic ) != 0;
}

/* "Index": every topic name and alias as a cross reference, sorted */
static int cmpName( const void *a, const void *b )
{
    return strcasecmp( *(char *const *) a, *(char *const *) b );
}

static void buildIndex()
{
    if (indexText || !helpText)
        return;
    int n = 0, cap = 256;
    char **names = (char **) malloc( cap * sizeof( char * ) );
    for (const char *p = helpText; *p; p = nextLine( p ))
        if (!strncmp( p, ".topic ", 7 ))
        {
            const char *e = lineEnd( p ), *s = p + 7;
            while (s < e)
            {
                const char *c = s;
                while (c < e && *c != ',')
                    ++c;
                const char *a = s, *b = c;
                while (a < b && *a == ' ') ++a;
                while (b > a && b[-1] == ' ') --b;
                if (b > a)
                {
                    if (n == cap)
                        names = (char **) realloc( names, (cap *= 2) * sizeof( char * ) );
                    names[n] = (char *) malloc( b - a + 1 );
                    memcpy( names[n], a, b - a );
                    names[n][b - a] = 0;
                    ++n;
                }
                s = c + 1;
            }
        }
    qsort( names, n, sizeof( char * ), cmpName );
    /* three columns of 25 */
    size_t size = 200 + n * 40;
    char *t = (char *) malloc( size ), *o = t;
    o += sprintf( o, "  Index of help topics (Tab to a topic, then Enter)\n\n" );
    int col = 0;
    for (int i = 0; i < n; ++i)
    {
        int w = (int) strlen( names[i] );
        if (col == 0)
            o += sprintf( o, "  " );
        o += sprintf( o, "{%s}", names[i] );
        if (++col == 3 || w > 22)
        {
            o += sprintf( o, "\n" );
            col = 0;
        }
        else
            o += sprintf( o, "%*s", 25 - w, "" );
        free( names[i] );
    }
    o += sprintf( o, "\n.topic \n" );
    free( names );
    indexText = t;
}

/* ------------------------------------------------------------- the view */

struct HelpLine { char *text; int len; };
struct HelpLink { int line, col, len; char target[40]; };

class TvHelpView : public TScroller
{
public:
    TvHelpView( const TRect &r, TScrollBar *h, TScrollBar *v );
    ~TvHelpView();
    virtual void draw();
    virtual void handleEvent( TEvent &event );
    virtual TPalette &getPalette() const;
    bool show( const char *topic, bool remember = true );
    void clear();
    void selectLink( int i );
    void follow();

    char topic[40];
    const char *topicText = 0;
    HelpLine *lines = 0;
    int nLines = 0;
    HelpLink *links = 0;
    int nLinks = 0;
    int cur = -1;
    char history[16][40];
    int nHistory = 0;
};

TPalette &TvHelpView::getPalette() const
{
    static TPalette palette( "\x06\x07\x08", 3 );
    return palette;
}

TvHelpView::TvHelpView( const TRect &r, TScrollBar *h, TScrollBar *v ) : TScroller( r, h, v )
{
    topic[0] = 0;
    helpCtx = helpCtxNo;
    growMode = gfGrowHiX | gfGrowHiY;
    options |= ofSelectable;
}

void TvHelpView::clear()
{
    for (int i = 0; i < nLines; ++i)
        free( lines[i].text );
    free( lines );
    free( links );
    lines = 0; links = 0; nLines = nLinks = 0;
}

TvHelpView::~TvHelpView()
{
    clear();
}

bool TvHelpView::show( const char *name, bool remember )
{
    if (!strcasecmp( name, "Index" ))
        buildIndex();
    const char *t = findTopic( name );
    if (!t)
        return false;
    if (remember && topic[0] && strcasecmp( topic, name ))
    {
        if (nHistory == 16)
        {
            memmove( history[0], history[1], sizeof history[0] * 15 );
            --nHistory;
        }
        strcpy( history[nHistory++], topic );
    }
    snprintf( topic, sizeof topic, "%s", name );
    topicText = t;
    clear();
    int capL = 0, capK = 0, width = 0;
    for (const char *p = t; *p && strncmp( p, ".topic ", 7 ); p = nextLine( p ))
    {
        const char *e = lineEnd( p );
        if (!strncmp( p, ".ex", 3 ) && (e - p == 3 || p[3] == '\r'))
            continue;
        if (!strncmp( p, ".endex", 6 ))
            continue;
        if (nLines == capL)
        {
            capL = capL ? capL * 2 : 64;
            lines = (HelpLine *) realloc( lines, capL * sizeof *lines );
        }
        char *out = (char *) malloc( e - p + 1 );
        int n = 0;
        for (const char *q = p; q < e; ++q)
        {
            if (*q == '\r')
                continue;
            if (*q == '{')
            {
                const char *z = q + 1;
                while (z < e && *z != '}')
                    ++z;
                if (z < e)
                {
                    const char *colon = (const char *) memchr( q + 1, ':', z - q - 1 );
                    const char *te = colon ? colon : z;
                    if (nLinks == capK)
                    {
                        capK = capK ? capK * 2 : 16;
                        links = (HelpLink *) realloc( links, capK * sizeof *links );
                    }
                    HelpLink &k = links[nLinks++];
                    k.line = nLines;
                    k.col = n;
                    k.len = (int) (te - q - 1);
                    const char *ts = colon ? colon + 1 : q + 1;
                    int tl = (int) (z - ts) < 39 ? (int) (z - ts) : 39;
                    memcpy( k.target, ts, tl );
                    k.target[tl] = 0;
                    memcpy( out + n, q + 1, k.len );
                    n += k.len;
                    q = z;
                    continue;
                }
            }
            out[n++] = *q;
        }
        out[n] = 0;
        lines[nLines].text = out;
        lines[nLines].len = n;
        if (n > width)
            width = n;
        ++nLines;
    }
    while (nLines > 0 && lines[nLines - 1].len == 0)
    {
        free( lines[nLines - 1].text );
        --nLines;
    }
    setLimit( width > size.x ? width : size.x, nLines );
    scrollTo( 0, 0 );
    cur = nLinks ? 0 : -1;
    if (owner)
        ((TWindow *) owner)->frame->drawView();
    drawView();
    return true;
}

void TvHelpView::selectLink( int i )
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
    drawView();
}

void TvHelpView::follow()
{
    if (cur < 0 || cur >= nLinks)
        return;
    char name[40];
    strcpy( name, links[cur].target );
    show( name );
}

/* Alt+F1 in a modal window: its own history */
static void backIn( TvHelpView *v )
{
    if (v->nHistory > 0)
    {
        v->show( v->history[--v->nHistory], false );
    }
}

void TvHelpView::draw()
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
    if (cur >= 0 && cur < nLinks)
    {
        setCursor( links[cur].col - delta.x, links[cur].line - delta.y );
        showCursor();
    }
    else
        hideCursor();
}

void TvHelpView::handleEvent( TEvent &event )
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
                backIn( this );
                clearEvent( event );
                return;
            case kbShiftF1:
                show( "Index" );
                clearEvent( event );
                return;
            case kbEsc:
                clearEvent( event );
                if (owner)
                    ((TWindow *) owner)->close();
                return;
        }
        /* a letter: the next cross reference starting with it */
        char c = event.keyDown.charScan.charCode;
        if (c > ' ' && nLinks && !(event.keyDown.controlKeyState & (kbAltShift | kbCtrlShift)))
        {
            for (int i = 1; i <= nLinks; ++i)
            {
                int j = (cur + i + nLinks) % nLinks;
                char f = lines[links[j].line].text[links[j].col];
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
                if ((event.mouse.eventFlags & meDoubleClick) || (event.mouse.buttons & mbRightButton))
                    follow();
                clearEvent( event );
                return;
            }
    }
    TScroller::handleEvent( event );
}

/* ---------------------------------------------------------- the window */

class TvHelpWindow : public TWindow
{
public:
    TvHelpWindow( const TRect &r );
    static TFrame *makeFrame( TRect r )
    {
        return tvHelpFrame ? tvHelpFrame( r ) : TWindow::initFrame( r );
    }
    virtual const char *getTitle( short );
    virtual TPalette &getPalette() const;
    virtual void close();
    TvHelpView *view;
    char title[60];
};

static TvHelpWindow *helpWin;

TvHelpWindow::TvHelpWindow( const TRect &r ) :
    TWindowInit( &TvHelpWindow::makeFrame ),
    TWindow( r, 0, wnNoNumber )
{
    options |= ofTileable;
    if (!tvHelpShadow)
        state &= ~sfShadow;
    TRect e = getExtent();
    TScrollBar *v = new TScrollBar( TRect( e.b.x - 1, 1, e.b.x, e.b.y - 1 ) );
    insert( v );
    TScrollBar *h = new TScrollBar( TRect( 2, e.b.y - 1, e.b.x - 2, e.b.y ) );
    insert( h );
    view = new TvHelpView( TRect( 1, 1, e.b.x - 1, e.b.y - 1 ), h, v );
    insert( view );
    helpCtx = helpCtxNo;
}

TPalette &TvHelpWindow::getPalette() const
{
    static TPalette palette( helpPalette, 8 );
    return palette;
}

const char *TvHelpWindow::getTitle( short )
{
    if (tvHelpTitle)
        return tvHelpTitle;
    snprintf( title, sizeof title, "HELP: %s", view ? view->topic : "" );
    return title;
}

void TvHelpWindow::close()
{
    if (state & sfModal)
        endModal( cmCancel );
    else
        tvHelpClose();
}

/* on top of a dialog: a help window of its own, until Esc */
void tvHelpModal( const char *topic )
{
    if (!helpText || !findTopic( topic ))
        return;
    TvHelpWindow *w = new TvHelpWindow( tvHelpRect() );
    w->view->show( topic );
    TProgram::deskTop->execView( w );
    TObject::destroy( w );
}

bool tvHelpShow( const char *topic )
{
    if (helpText && !strcasecmp( topic, "Index" ))
        buildIndex();
    if (!helpText || !findTopic( topic ))
        return false;
    if (!helpWin)
    {
        helpWin = new TvHelpWindow( tvHelpRect() );
        TProgram::deskTop->insert( helpWin );
        helpWin->view->show( topic );
    }
    else
        helpWin->view->show( topic );
    helpWin->select();
    return true;
}

void tvHelpIndex()
{
    buildIndex();
    tvHelpShow( "Index" );
}

void tvHelpPrevious()
{
    if (helpWin && helpWin->view->nHistory > 0)
    {
        TvHelpView *v = helpWin->view;
        v->show( v->history[--v->nHistory], false );
        helpWin->select();
    }
}

bool tvHelpIsOpen()
{
    return helpWin != 0;
}

bool tvHelpIsCurrent()
{
    return helpWin && TProgram::deskTop->current == helpWin;
}

void tvHelpClose()
{
    if (!helpWin)
        return;
    TvHelpWindow *w = helpWin;
    helpWin = 0;
    TProgram::deskTop->remove( w );
    TObject::destroy( w );
}

const char *tvHelpTopic()
{
    return helpWin ? helpWin->view->topic : "";
}

char *tvHelpExample()
{
    if (!helpWin || !helpWin->view->topicText)
        return 0;
    const char *s = 0, *e = 0;
    for (const char *p = helpWin->view->topicText; *p && strncmp( p, ".topic ", 7 ); p = nextLine( p ))
    {
        if (!s && !strncmp( p, ".ex", 3 ) && (p[3] == '\n' || p[3] == '\r' || !p[3]))
            s = nextLine( p );
        else if (s && !strncmp( p, ".endex", 6 ))
        {
            e = p;
            break;
        }
    }
    if (!s || !e)
        return 0;
    /* with CR LF line breaks, as the editor has them */
    size_t n = 0;
    for (const char *p = s; p < e; ++p)
        n += *p == '\n' ? 2 : *p == '\r' ? 0 : 1;
    char *out = (char *) malloc( n + 1 ), *o = out;
    for (const char *p = s; p < e; ++p)
    {
        if (*p == '\r')
            continue;
        if (*p == '\n')
            *o++ = '\r';
        *o++ = *p;
    }
    *o = 0;
    return out;
}
