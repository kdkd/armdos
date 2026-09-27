/*
 * qb.cpp - QB.EXE, "ARM QuickBASIC": the application, menu bar, status
 * line, the program window and the Immediate window.
 *
 *   QB [/RUN] [/B] [/H] [/NOHI] [[drive:][path]filename]
 *
 * Looks like the BASIC environment of 1991 (the one DOS 5 came with): the
 * menu bar File Edit View Search Run Debug Options ... Help, the program
 * window "Untitled" over the whole screen, the Immediate window below it,
 * the status line "<Shift+F1=Help> <F6=Window> <F2=Subs> <F5=Run>
 * <F8=Step>" with the cursor position. Keywords are upper-cased when the
 * cursor leaves a line. The interpreter is Bywater BASIC (bw/, GPL-2),
 * linked in: see qbrun.cpp.
 *
 * The look is the ARM-DOS Editor's (apps/edit, whose frame, menu bar and
 * status line code this file started from).
 *
 * Copyright (c) 2026 Europa Micro Systems. MIT License.
 */
#include "qb.h"
#include "edplat.h"
#include "tvhelp.h"

#define Uses_TFindDialogRec
#define Uses_TReplaceDialogRec
#include <tvision/tv.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include <armdos.h>

QbWindow *docWindow, *immWindow;
char helpPath[80];
static const char *pendingHelpTopic;     /* F1 in a menu: shown when idle */

/* the heap lives in XMS (QB runs from an XMS block, apps/tvlib/xload.c):
 * up to 4 MB of it */
extern "C" { unsigned _armdos_xms_kb = 4096; }
static bool optBlackWhite, optMaxLines, optNoHighlight, optRun;

/* -------------------------------------------------------------- palette */

/* Colours of the 1991 BASIC environment (as the Editor's). Layout: Turbo
 * Vision's cpAppColor (app.h) for 1..135, then ours (qb.h). */
uchar qbPalette[apEnd] = {
    0,
    /* 1 desktop */ 0x17,
    /* 2-7 menus: normal, disabled, shortcut, selected, sel. disabled, sel. shortcut */
    0x70, 0x78, 0x7F, 0x07, 0x08, 0x0F,
    /* 8-15 program window: frame, frame active, title (active), scroll bar
     * track, scroll bar arrows, text, selected text, reserved */
    0x17, 0x17, 0x70, 0x70, 0x70, 0x17, 0x71, 0x1F,
    /* 16-23 cyan window, 24-31 gray window (unused) */
    0x30, 0x30, 0x3F, 0x31, 0x31, 0x30, 0x3F, 0x3F,
    0x70, 0x70, 0x7F, 0x70, 0x70, 0x70, 0x7F, 0x7F,
    /* 32-63 dialogs (gray) */
    0x70, 0x70, 0x70, 0x70, 0x70, 0x70, 0x70, 0x70,
    0x7F, 0x70, 0x70, 0x70, 0x78, 0x7F, 0x70, 0x70,
    0x70, 0x7F, 0x70, 0x07, 0x70, 0x70, 0x70, 0x70,
    0x70, 0x70, 0x07, 0x70, 0x70, 0x70, 0x78, 0x70,
    /* 64-95 blue dialogs, 96-127 cyan dialogs: as gray */
    0x70, 0x70, 0x70, 0x70, 0x70, 0x70, 0x70, 0x70,
    0x7F, 0x70, 0x70, 0x70, 0x78, 0x7F, 0x70, 0x70,
    0x70, 0x7F, 0x70, 0x07, 0x70, 0x70, 0x70, 0x70,
    0x70, 0x70, 0x07, 0x70, 0x70, 0x70, 0x78, 0x70,
    0x70, 0x70, 0x70, 0x70, 0x70, 0x70, 0x70, 0x70,
    0x7F, 0x70, 0x70, 0x70, 0x78, 0x7F, 0x70, 0x70,
    0x70, 0x7F, 0x70, 0x07, 0x70, 0x70, 0x70, 0x70,
    0x70, 0x70, 0x07, 0x70, 0x70, 0x70, 0x78, 0x70,
    /* 128-135 TV help window (unused) */
    0x17, 0x17, 0x70, 0x70, 0x70, 0x17, 0x1F, 0x71,
    /* ours: status line, status position, help text, help link, link selected */
    0x30, 0x30, 0x17, 0x1F, 0x71,
};

static void blackWhitePalette()
{
    for (int i = 1; i < apEnd; ++i)
    {
        uchar a = qbPalette[i];
        uchar fg = a & 0x0F, bg = (a >> 4) & 7;
        bool reverse = bg == 7 || bg == 3;
        uchar nfg = fg & 8 ? 0x0F : 0x07;
        qbPalette[i] = reverse ? ((fg & 8) ? 0x7F : 0x70) : nfg;
    }
    qbPalette[14] = 0x70;
}

DisplayOptions displayOpts = { 7, 1, 1, 7, true, 8 };

extern int editTabSize;

void applyDisplayOptions()
{
    qbPalette[13] = (uchar) ((displayOpts.normalBg << 4) | displayOpts.normalFg);
    qbPalette[14] = (uchar) ((displayOpts.highBg << 4) | displayOpts.highFg);
    qbPalette[8] = qbPalette[9] = qbPalette[13];
    qbPalette[1] = qbPalette[13];
    qbPalette[apHelpText] = qbPalette[13];
    qbPalette[apHelpLink] = (uchar) ((displayOpts.normalBg << 4) | 0x0F);
    qbPalette[apHelpLinkSel] = qbPalette[14];
    editTabSize = displayOpts.tabStops < 1 ? 1 : displayOpts.tabStops > 99 ? 99 : displayOpts.tabStops;
    if (docWindow)
    {
        docWindow->showScrollBars( displayOpts.scrollBars );
        docWindow->redraw();
    }
    if (TProgram::application)
        TProgram::application->redraw();
}

/* ------------------------------------------------------------ keywords */

static const char *const keywords[] = {
    "ABS", "ACCESS", "AND", "APPEND", "AS", "ASC", "ATN", "BASE", "BEEP", "BINARY",
    "BLOAD", "BSAVE", "CALL", "CASE", "CDBL", "CHAIN", "CHDIR", "CHR$", "CINT", "CIRCLE",
    "CLEAR", "CLNG", "CLOSE", "CLS", "COLOR", "COMMAND$", "COMMON", "CONST", "COS", "CSNG",
    "CSRLIN", "CVD", "CVI", "CVL", "CVS", "DATA", "DATE$", "DECLARE", "DEF", "DEFDBL",
    "DEFINT", "DEFLNG", "DEFSNG", "DEFSTR", "DIM", "DO", "DOUBLE", "DRAW", "ELSE", "ELSEIF",
    "END", "ENVIRON", "ENVIRON$", "EOF", "EQV", "ERASE", "ERL", "ERR", "ERROR", "EXIT",
    "EXP", "FIELD", "FILES", "FIX", "FOR", "FRE", "FREEFILE", "FUNCTION", "GET", "GOSUB",
    "GOTO", "HEX$", "IF", "IMP", "INKEY$", "INP", "INPUT", "INPUT$", "INSTR", "INT",
    "INTEGER", "IS", "KEY", "KILL", "LBOUND", "LCASE$", "LEFT$", "LEN", "LET", "LINE",
    "LOC", "LOCATE", "LOF", "LOG", "LONG", "LOOP", "LPOS", "LPRINT", "LSET", "LTRIM$",
    "MID$", "MKD$", "MKDIR", "MKI$", "MKL$", "MKS$", "MOD", "NAME", "NEXT", "NOT",
    "OCT$", "OFF", "ON", "OPEN", "OPTION", "OR", "OUT", "OUTPUT", "PAINT", "PALETTE",
    "PEEK", "PLAY", "POINT", "POKE", "POS", "PRESET", "PRINT", "PSET", "PUT", "RANDOM",
    "RANDOMIZE", "READ", "REDIM", "REM", "RESET", "RESTORE", "RESUME", "RETURN", "RIGHT$", "RMDIR",
    "RND", "RSET", "RTRIM$", "RUN", "SCREEN", "SEEK", "SEG", "SELECT", "SGN", "SHARED",
    "SHELL", "SIN", "SINGLE", "SLEEP", "SOUND", "SPACE$", "SPC", "SQR", "STATIC", "STEP",
    "STOP", "STR$", "STRING", "STRING$", "SUB", "SWAP", "SYSTEM", "TAB", "TAN", "THEN",
    "TIME$", "TIMER", "TO", "TROFF", "TRON", "TYPE", "UBOUND", "UCASE$", "UNTIL", "USING",
    "VAL", "VARPTR", "VIEW", "WAIT", "WEND", "WHILE", "WIDTH", "WINDOW", "WRITE", "XOR", 0
};

bool isKeyword( const char *w, int n, const char **canonical )
{
    for (const char *const *k = keywords; *k; ++k)
        if ((int) strlen( *k ) == n && !strncasecmp( *k, w, n ))
        {
            if (canonical)
                *canonical = *k;
            return true;
        }
    return false;
}

/* -------------------------------------------------------------- frames */

/* ┌── Title ──┐ │ │ for the program window; the Immediate window's top is
 * ├── Immediate ──┤ (it hangs under the program window) and it has no
 * bottom border. */
class QbFrame;
TFrame *qbNewFrame( TRect r );

class QbFrame : public TFrame
{
public:
    QbFrame( const TRect &r ) : TFrame( r ) {}
    virtual void draw();
    virtual TPalette &getPalette() const;
    virtual void handleEvent( TEvent &event );
};

#define cpQbFrame "\x01\x03"

TPalette &QbFrame::getPalette() const
{
    static TPalette palette( cpQbFrame, sizeof( cpQbFrame ) - 1 );
    return palette;
}

void QbFrame::handleEvent( TEvent &event )
{
    /* a click on the frame selects the window */
    if (event.what == evMouseDown && owner)
    {
        owner->select();
        clearEvent( event );
        return;
    }
    TView::handleEvent( event );
}

void QbFrame::draw()
{
    TDrawBuffer b;
    TColorAttr cFrame = getColor( 1 );
    TColorAttr cTitle = cFrame;
    if (state & sfActive)
        cTitle = getColor( 2 );
    bool imm = owner && owner == immWindow;
    bool bars = !imm && displayOpts.scrollBars;

    b.moveChar( 0, imm ? '\xC3' : '\xDA', cFrame, 1 );
    b.moveChar( 1, '\xC4', cFrame, size.x - 2 );
    b.moveChar( size.x - 1, imm ? '\xB4' : '\xBF', cFrame, 1 );
    const char *title = owner ? ((TWindow *) owner)->getTitle( size.x - 10 ) : 0;
    if (title && *title)
    {
        int l = (int) strlen( title );
        if (l > size.x - 10)
            l = size.x - 10;
        int x = (size.x - l - 2) / 2;
        b.moveChar( x, ' ', cTitle, 1 );
        b.moveStr( x + 1, TStringView( title, l ), cTitle );
        b.moveChar( x + 1 + l, ' ', cTitle, 1 );
    }
    writeLine( 0, 0, size.x, 1, b );

    b.moveChar( 0, '\xB3', cFrame, 1 );
    b.moveChar( 1, ' ', cFrame, size.x - 2 );
    b.moveChar( size.x - 1, '\xB3', cFrame, 1 );
    for (int y = 1; y < size.y - 1; ++y)
        writeLine( 0, y, size.x, 1, b );
    if (size.y > 1)
    {
        if (!imm && !bars)
        {
            b.moveChar( 0, '\xC0', cFrame, 1 );
            b.moveChar( 1, '\xC4', cFrame, size.x - 2 );
            b.moveChar( size.x - 1, '\xD9', cFrame, 1 );
        }
        writeLine( 0, size.y - 1, size.x, 1, b );
    }
}

TFrame *qbNewFrame( TRect r )
{
    return new QbFrame( r );
}

/* -------------------------------------------------------------- editor */

QbEditor::QbEditor( const TRect &bounds, TScrollBar *h, TScrollBar *v, const char *fileName ) :
    TFileEditor( bounds, h, v, 0, fileName ? TStringView( fileName ) : TStringView() )
{
    autoIndent = True;
    helpCtx = hcEditor;
}

void QbEditor::updateCommands()
{
    TFileEditor::updateCommands();
    setCmdState( cmUndo, False );
    setCmdState( cmPrint, True );
}

int QbEditor::lineNumber( uint ptr )
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

uint QbEditor::lineStartOf( int line )
{
    return lineMove( 0, line - 1 );
}

void QbEditor::gotoLine( int line, bool select )
{
    if (line < 1)
        line = 1;
    lock();
    uint p = lineStartOf( line );
    /* the first non-blank character */
    uint e = lineEnd( p );
    uint q = p;
    while (q < e && (bufChar( q ) == ' ' || bufChar( q ) == '\t'))
        ++q;
    if (select)
        setSelect( q, e, True );
    else
        setCurPtr( q, 0 );
    trackCursor( True );
    unlock();
}

void QbEditor::wordAtCursor( char *buf, int size )
{
    uint p = curPtr, s = p;
    auto isw = [&]( uint q ) {
        char c = bufChar( q );
        return isalnum( (uchar) c ) || c == '_' || c == '$' || c == '.';
    };
    if (s >= bufLen || !isw( s ))
        if (s > 0 && isw( s - 1 ))
            --s;
    while (s > 0 && isw( s - 1 ))
        --s;
    int n = 0;
    for (uint q = s; q < bufLen && isw( q ) && n < size - 1; ++q)
        buf[n++] = bufChar( q );
    buf[n] = 0;
}

/* Keywords of the line at ptr to upper case (outside strings and remarks).
 * Same length: the buffer is changed in place. */
void QbEditor::recaseLine( uint ptr )
{
    if (ptr > bufLen)
        ptr = bufLen;
    uint p = lineStart( ptr ), e = lineEnd( ptr );
    bool changed = false;
    while (p < e)
    {
        char c = bufChar( p );
        if (c == '"')
        {
            ++p;
            while (p < e && bufChar( p ) != '"')
                ++p;
            ++p;
            continue;
        }
        if (c == '\'')
            break;
        if (isalpha( (uchar) c ))
        {
            char w[16];
            int n = 0;
            uint s = p;
            while (p < e)
            {
                char d = bufChar( p );
                if (!(isalnum( (uchar) d ) || d == '_' || d == '.'))
                {
                    if (d == '$' && n < 15)
                    {
                        w[n++] = d;
                        ++p;
                    }
                    break;
                }
                if (n < 15)
                    w[n] = d;
                ++n;
                ++p;
            }
            if (n > 15)
                continue;
            const char *k;
            if (isKeyword( w, n, &k ))
            {
                for (int i = 0; i < n; ++i)
                    if (bufChar( s + i ) != k[i])
                    {
                        buffer[bufPtr( s + i )] = k[i];
                        changed = true;
                    }
                if (!strcmp( k, "REM" ))
                    break;
            }
            continue;
        }
        ++p;
    }
    if (changed)
    {
        modified = True;
        drawView();
    }
}

static bool literalNext;

/* Find / Repeat Last Find / Change with wrapping, as the Editor does. */
void QbEditor::searchReplace( bool replace )
{
    ushort opts = editorFlags;
    uint origStart = selStart, origEnd = selEnd, origPtr = curPtr;
    uint limit = curPtr;
    bool wrapped = curPtr == 0;
    bool found = false;
    int changed = 0;
    size_t flen = strlen( findStr ), rlen = strlen( replaceStr );
    if (flen == 0)
        return;
    for (;;)
    {
        if (!search( findStr, opts ))
        {
            if (wrapped)
                break;
            wrapped = true;
            lock();
            setCurPtr( 0, 0 );
            unlock();
            continue;
        }
        if (wrapped && limit != 0 && selStart >= limit && (replace || found))
            break;
        found = true;
        if (!replace)
            return;
        ushort r = cmYes;
        if (opts & efPromptOnReplace)
        {
            TPoint c = makeGlobal( cursor );
            r = editorDialog( edReplacePrompt, &c );
        }
        if (r == cmCancel)
            break;
        if (r == cmYes)
        {
            uint at = selStart;
            lock();
            insertText( replaceStr, (uint) rlen, False );
            trackCursor( False );
            unlock();
            ++changed;
            if (wrapped || at < limit)
                limit = limit + (uint) rlen - (uint) flen;
        }
    }
    if (!found)
    {
        lock();
        setCurPtr( origPtr, 0 );
        if (origStart != origEnd)
            setSelect( origStart, origEnd, Boolean( origPtr == origStart ) );
        unlock();
        editorDialog( edSearchFailed );
    }
    else if (replace)
    {
        lock();
        hideSelect();
        unlock();
        edMessage( "Change complete", emOK );
    }
}

void QbEditor::handleEvent( TEvent &event )
{
    if (event.what == evKeyDown)
    {
        if (literalNext)
        {
            literalNext = false;
            uchar c = event.keyDown.charScan.charCode;
            if (c != 0)
            {
                lock();
                if (overwrite && !hasSelection() && curPtr != lineEnd( curPtr ))
                    selEnd = nextChar( curPtr );
                insertText( &c, 1, False );
                trackCursor( False );
                unlock();
                clearEvent( event );
                return;
            }
        }
        else if (event.keyDown.keyCode == kbCtrlP)
        {
            literalNext = true;
            clearEvent( event );
            return;
        }
        else if (isImmediate && event.keyDown.keyCode == kbEnter)
        {
            /* Immediate window: execute the line, go on to the next */
            uint s = lineStart( curPtr ), e = lineEnd( curPtr );
            char line[256];
            int n = 0;
            for (uint p = s; p < e && n < (int) sizeof line - 1; ++p)
                line[n++] = bufChar( p );
            line[n] = 0;
            recaseLine( s );
            n = 0;
            for (uint p = s; p < e && n < (int) sizeof line - 1; ++p)
                line[n++] = bufChar( p );
            lock();
            if (nextLine( curPtr ) >= bufLen && lineEnd( curPtr ) == bufLen)
            {
                setCurPtr( bufLen, 0 );
                insertText( "\r\n", 2, False );
            }
            else
                setCurPtr( nextLine( curPtr ), 0 );
            trackCursor( False );
            unlock();
            clearEvent( event );
            runImmediate( line );
            return;
        }
        else if (keyState == 0 && event.keyDown.keyCode == kbCtrlA)
        {
            event.what = evCommand;
            event.message.command = cmWordLeft;
            event.message.infoPtr = 0;
        }
    }
    if (event.what == evKeyDown && (state & sfFocused))
    {
        TEvent e = event;
        int ks = keyState;
        convertEvent( e );
        if (e.what == evCommand && (e.message.command == cmFind || e.message.command == cmReplace ||
                                    e.message.command == cmSearchAgain))
            event = e;
        else
            keyState = ks;
    }
    if (event.what == evMouseDown && (event.mouse.buttons & mbRightButton))
    {
        clearEvent( event );
        return;
    }
    if (event.what == evCommand)
        switch (event.message.command)
        {
            case cmFind:
            {
                TFindDialogRec rec( findStr, editorFlags );
                if (editorDialog( edFind, &rec ) != cmCancel && rec.find[0])
                {
                    strcpy( findStr, rec.find );
                    editorFlags = rec.options & ~(efDoReplace | efReplaceAll);
                    searchReplace( false );
                }
                clearEvent( event );
                return;
            }
            case cmReplace:
            {
                TReplaceDialogRec rec( findStr, replaceStr, editorFlags );
                if (editorDialog( edReplace, &rec ) != cmCancel && rec.find[0])
                {
                    strcpy( findStr, rec.find );
                    strcpy( replaceStr, rec.replace );
                    editorFlags = rec.options | efDoReplace;
                    searchReplace( true );
                }
                clearEvent( event );
                return;
            }
            case cmSearchAgain:
                if (findStr[0] == 0)
                    message( this, evCommand, cmFind, 0 );
                else
                {
                    editorFlags &= ~(efDoReplace | efReplaceAll | efPromptOnReplace);
                    searchReplace( false );
                }
                clearEvent( event );
                return;
        }
    /* keywords to upper case when the cursor leaves a line; any change to
     * the program ends a suspended run */
    uint lineBefore = lineStart( curPtr );
    uint lenBefore = bufLen, insBefore = insCount, delBefore = delCount;
    bool hadEvent = event.what != evNothing;
    TFileEditor::handleEvent( event );
    if (hadEvent)
    {
        if (lineStart( curPtr ) != lineBefore && !(lineBefore <= curPtr && curPtr <= lineEnd( lineBefore ) &&
                                                   lineStart( curPtr ) == lineBefore))
            recaseLine( lineBefore < bufLen ? lineBefore : bufLen );
        if (!isImmediate && (bufLen != lenBefore || insCount != insBefore || delCount != delBefore))
            runEditorChanged();
    }
}

/* ------------------------------------------------------ program window */

#define cpQbWindow "\x08\x09\x0A\x0B\x0C\x0D\x0E\x0F"

QbWindow::QbWindow( const TRect &bounds, const char *fileName, bool imm ) :
    TWindowInit( &QbWindow::initFrame ),
    TWindow( bounds, 0, wnNoNumber ), immediate( imm )
{
    flags = 0;
    state &= ~sfShadow;
    options |= ofTileable;
    TRect r = getExtent();
    if (!imm)
    {
        vScroll = new TScrollBar( TRect( r.b.x - 1, 1, r.b.x, r.b.y - 1 ) );
        hScroll = new TScrollBar( TRect( 1, r.b.y - 1, r.b.x - 1, r.b.y ) );
        vScroll->growMode = gfGrowLoX | gfGrowHiX | gfGrowHiY;
        hScroll->growMode = gfGrowLoY | gfGrowHiX | gfGrowHiY;
        insert( vScroll );
        insert( hScroll );
        editor = new QbEditor( TRect( 1, 1, r.b.x - 1, r.b.y - 1 ), hScroll, vScroll, fileName );
    }
    else
    {
        vScroll = hScroll = 0;
        editor = new QbEditor( TRect( 1, 1, r.b.x - 1, r.b.y ), 0, 0, 0 );
        editor->isImmediate = true;
        editor->helpCtx = hcImmediate;
        helpCtx = hcImmediate;
    }
    editor->growMode = gfGrowHiX | gfGrowHiY;
    insert( editor );
}

TFrame *QbWindow::initFrame( TRect r )
{
    return new QbFrame( r );
}

TPalette &QbWindow::getPalette() const
{
    static TPalette palette( cpQbWindow, sizeof( cpQbWindow ) - 1 );
    return palette;
}

const char *QbWindow::getTitle( short )
{
    static char buf[16];
    if (immediate)
        return "Immediate";
    if (!editor || editor->fileName[0] == 0)
        return "Untitled";
    const char *p = editor->fileName, *q;
    if ((q = strrchr( p, '\\' )) != 0) p = q + 1;
    if ((q = strrchr( p, ':' )) != 0) p = q + 1;
    strnzcpy( buf, p, sizeof( buf ) );
    return buf;
}

void QbWindow::sizeLimits( TPoint &min, TPoint &max )
{
    TWindow::sizeLimits( min, max );
    min.x = 20;
    min.y = 2;
}

void QbWindow::handleEvent( TEvent &event )
{
    TWindow::handleEvent( event );
    if (event.what == evBroadcast && event.message.command == cmUpdateTitle)
    {
        if (frame)
            frame->drawView();
        clearEvent( event );
    }
}

void QbWindow::showScrollBars( bool on )
{
    if (!vScroll)
        return;
    if (on)
    {
        vScroll->show();
        hScroll->show();
    }
    else
    {
        vScroll->hide();
        hScroll->hide();
    }
    if (frame)
        frame->drawView();
}

/* ------------------------------------------------------------ menu bar */

/* Help at the right end */
class QbMenuBar : public TMenuBar
{
public:
    QbMenuBar( const TRect &r, TMenu *m ) : TMenuBar( r, m ) {}
    virtual void draw();
    virtual TRect getItemRect( TMenuItem *item );
    virtual void handleEvent( TEvent &event )
    {
        if (event.what == evCommand && event.message.command == cmMenu && menu)
            menu->deflt = menu->items;
        TMenuBar::handleEvent( event );
    }
};

TRect QbMenuBar::getItemRect( TMenuItem *item )
{
    int x = 1;
    for (TMenuItem *p = menu->items; p != 0; p = p->next)
    {
        if (p->name == 0)
            continue;
        int l = cstrlen( p->name ) + 2;
        if (p->next == 0)
            x = size.x - l - 1;
        if (p == item)
            return TRect( x, 0, x + l, 1 );
        x += l;
    }
    return TRect( 0, 0, 0, 0 );
}

void QbMenuBar::draw()
{
    TDrawBuffer b;
    TAttrPair cNormal = getColor( 0x0301 );
    TAttrPair cSelect = getColor( 0x0604 );
    TAttrPair cNormDisabled = getColor( 0x0202 );
    TAttrPair cSelDisabled = getColor( 0x0505 );
    b.moveChar( 0, ' ', cNormal, size.x );
    if (menu)
        for (TMenuItem *p = menu->items; p != 0; p = p->next)
        {
            if (p->name == 0)
                continue;
            TRect r = getItemRect( p );
            TAttrPair color = p->disabled ? (p == current ? cSelDisabled : cNormDisabled)
                                          : (p == current ? cSelect : cNormal);
            b.moveChar( r.a.x, ' ', color, 1 );
            b.moveCStr( r.a.x + 1, p->name, color );
            b.moveChar( r.b.x - 1, ' ', color, 1 );
        }
    writeBuf( 0, 0, size.x, 1, b );
}

/* --------------------------------------------------------- status line */

class QbStatusLine : public TStatusLine
{
public:
    QbStatusLine( const TRect &r, TStatusDef &d ) : TStatusLine( r, d ) {}
    virtual void draw();
    virtual TPalette &getPalette() const;
    virtual const char *hint( ushort ctx );
    void updatePos();
    TPoint shownPos { -1, -1 };
    TView *shownWin = 0;
};

static const struct { ushort ctx; const char *text; } hints[] = {
    { hxNew, "Removes currently loaded program from memory" },
    { hxOpen, "Loads new program into memory" },
    { hxSave, "Saves current program" },
    { hxSaveAs, "Saves current program with specified name" },
    { hxPrint, "Prints specified text" },
    { hxExit, "Exits QuickBASIC and returns to DOS" },
    { hxCut, "Deletes selected text and copies it to buffer" },
    { hxCopy, "Copies selected text to buffer" },
    { hxPaste, "Inserts buffer contents at current location" },
    { hxClear, "Deletes selected text without copying it to buffer" },
    { hxNewSub, "Opens a window for a new subprogram" },
    { hxNewFunction, "Opens a window for a new FUNCTION procedure" },
    { hxSubs, "Displays a loaded SUB, FUNCTION, module, include file, or document" },
    { hxSplit, "Divides screen into two View windows" },
    { hxOutput, "Displays output screen" },
    { hxFind, "Finds specified text" },
    { hxRepeat, "Finds next occurrence of text specified in previous search" },
    { hxChange, "Finds and changes specified text" },
    { hxStart, "Runs current program" },
    { hxRestart, "Clears variables in preparation for restarting single stepping" },
    { hxContinue, "Continues execution after a break" },
    { hxStep, "Executes next program statement" },
    { hxProcStep, "Executes next program statement, tracing over procedure calls" },
    { hxTrace, "Highlights statement currently executing" },
    { hxToggleBreak, "Sets/clears breakpoint at cursor location" },
    { hxClearBreaks, "Removes all breakpoints" },
    { hxSetNext, "Makes the statement at the cursor the next statement to execute" },
    { hxDisplay, "Changes display attributes" },
    { hxHelpPath, "Sets default directory for your programs" },
    { hxIndex, "Displays help index" },
    { hxContents, "Displays help table of contents" },
    { hxTopic, "Displays information about the BASIC keyword the cursor is on" },
    { hxUsing, "Displays information about how to use online Help" },
    { hxAbout, "Displays product version and copyright information" },
};

const char *QbStatusLine::hint( ushort ctx )
{
    for (auto &h : hints)
        if (h.ctx == ctx)
            return h.text;
    return "";
}

#define cpQbStatusLine "\x88\x89"

TPalette &QbStatusLine::getPalette() const
{
    static TPalette palette( cpQbStatusLine, sizeof( cpQbStatusLine ) - 1 );
    return palette;
}

static QbWindow *activeQbWindow()
{
    TView *c = TProgram::deskTop ? TProgram::deskTop->current : 0;
    if (c == immWindow)
        return immWindow;
    return docWindow;
}

void QbStatusLine::draw()
{
    TDrawBuffer b;
    TColorAttr c = getColor( 1 );
    b.moveChar( 0, ' ', c, size.x );
    const char *text;
    char buf[100];
    int posCol = size.x - 11;
    bool showPos = false;
    if (helpCtx > hcFileMenu && helpCtx < hcDlgFind && (helpCtx % 10) != 0)
    {
        snprintf( buf, sizeof buf, " F1=Help \xB3 %s", hint( helpCtx ) );
        text = buf;
    }
    else if ((helpCtx >= hcFileMenu && helpCtx < hcDlgFind) || helpCtx == hcMenuBar)
        text = " F1=Help  Enter=Display Menu  Esc=Cancel  Arrow=Next Item";
    else if (helpCtx >= hcDlgFind || helpCtx == hcDialog)
        text = " F1=Help  Enter=Execute  Esc=Cancel  Tab=Next Field  Arrow=Next Item";
    else if (helpCtx == hcHelp)
        text = " <Shift+F1=Help> <F6=Window> <Esc=Cancel> <Ctrl+F1=Next> <Alt+F1=Back>";
    else
    {
        text = " <Shift+F1=Help> <F6=Window> <F2=Subs> <F5=Run> <F8=Step>";
        showPos = true;
    }
    b.moveStr( 0, text, c, showPos ? posCol : size.x );
    QbWindow *w = activeQbWindow();
    if (showPos && w)
    {
        TPoint p = w->editor->curPos;
        snprintf( buf, sizeof buf, "\xB3 %05d:%03d", p.y + 1, p.x + 1 );
        b.moveStr( posCol, buf, getColor( 2 ) );
        shownPos = p;
        shownWin = w;
    }
    writeLine( 0, 0, size.x, 1, b );
}

void QbStatusLine::updatePos()
{
    QbWindow *w = activeQbWindow();
    if (w && (w->editor->curPos != shownPos || w != shownWin) && helpCtx <= hcImmediate)
        drawView();
}

/* ----------------------------------------------------------- application */

class QbApp : public TApplication
{
public:
    QbApp();
    static TMenuBar *initMenuBar( TRect r );
    static TStatusLine *initStatusLine( TRect r );
    static TDeskTop *initDeskTop( TRect r );
    virtual TPalette &getPalette() const;
    virtual void handleEvent( TEvent &event );
    virtual void getEvent( TEvent &event );
    virtual void idle();
    virtual void outOfMemory();
    void openFile( const char *name );

private:
    bool okToDiscard();
    void fileNew();
    void fileOpen();
    void filePrint();
    void newProc( bool function );
};

QbApp::QbApp() :
    TProgInit( &QbApp::initStatusLine, &QbApp::initMenuBar, &QbApp::initDeskTop ),
    TApplication()
{
    TEditor::editorDialog = doEditDialog;
    TEditor::editorFlags = efPromptOnReplace;

    TEditor *clip = new TFileEditor( TRect( 0, 0, 1, 1 ), 0, 0, 0, TStringView() );
    clip->hide();
    deskTop->insert( clip );
    TEditor::clipboard = clip;
    TCommandSet dis;
    dis += cmSplit; dis += cmTraceOn;
    disableCommands( dis );
}

TMenuBar *QbApp::initMenuBar( TRect r )
{
    r.b.y = r.a.y + 1;
    TSubMenu &file = *new TSubMenu( "~F~ile", kbAltF, hcFileMenu ) +
        *new TMenuItem( "~N~ew", cmNew, kbNoKey, hxNew ) +
        *new TMenuItem( "~O~pen...", cmOpen, kbNoKey, hxOpen ) +
        *new TMenuItem( "~S~ave", cmSave, kbNoKey, hxSave ) +
        *new TMenuItem( "Save ~A~s...", cmSaveAs, kbNoKey, hxSaveAs ) +
        newLine() +
        *new TMenuItem( "~P~rint...", cmPrint, kbNoKey, hxPrint ) +
        newLine() +
        *new TMenuItem( "E~x~it", cmQuit, kbNoKey, hxExit );
    TSubMenu &edit = *new TSubMenu( "~E~dit", kbAltE, hcEditMenu ) +
        *new TMenuItem( "Cu~t~", cmCut, kbShiftDel, hxCut, "Shift+Del" ) +
        *new TMenuItem( "~C~opy", cmCopy, kbCtrlIns, hxCopy, "Ctrl+Ins" ) +
        *new TMenuItem( "~P~aste", cmPaste, kbShiftIns, hxPaste, "Shift+Ins" ) +
        *new TMenuItem( "Cl~e~ar", cmClear, kbDel, hxClear, "Del" ) +
        newLine() +
        *new TMenuItem( "New ~S~UB...", cmNewSub, kbNoKey, hxNewSub ) +
        *new TMenuItem( "New ~F~UNCTION...", cmNewFunction, kbNoKey, hxNewFunction );
    TSubMenu &view = *new TSubMenu( "~V~iew", kbAltV, hcViewMenu ) +
        *new TMenuItem( "~S~UBs...", cmSubs, kbF2, hxSubs, "F2" ) +
        *new TMenuItem( "S~p~lit", cmSplit, kbNoKey, hxSplit ) +
        newLine() +
        *new TMenuItem( "Output ~S~creen", cmOutputScreen, kbF4, hxOutput, "F4" );
    TSubMenu &search = *new TSubMenu( "~S~earch", kbAltS, hcSearchMenu ) +
        *new TMenuItem( "~F~ind...", cmFind, kbNoKey, hxFind ) +
        *new TMenuItem( "~R~epeat Last Find", cmSearchAgain, kbF3, hxRepeat, "F3" ) +
        *new TMenuItem( "~C~hange...", cmReplace, kbNoKey, hxChange );
    TSubMenu &run = *new TSubMenu( "~R~un", kbAltR, hcRunMenu ) +
        *new TMenuItem( "~S~tart", cmStart, kbShiftF5, hxStart, "Shift+F5" ) +
        *new TMenuItem( "~R~estart", cmRestart, kbNoKey, hxRestart ) +
        *new TMenuItem( "Co~n~tinue", cmContinue, kbF5, hxContinue, "F5" );
    TSubMenu &debug = *new TSubMenu( "~D~ebug", kbAltD, hcDebugMenu ) +
        *new TMenuItem( "~S~tep", cmStep, kbF8, hxStep, "F8" ) +
        *new TMenuItem( "~P~rocedure Step", cmProcStep, kbF10, hxProcStep, "F10" ) +
        newLine() +
        *new TMenuItem( "~T~race On", cmTraceOn, kbNoKey, hxTrace ) +
        newLine() +
        *new TMenuItem( "Toggle ~B~reakpoint", cmToggleBreak, kbF9, hxToggleBreak, "F9" ) +
        *new TMenuItem( "~C~lear All Breakpoints", cmClearBreaks, kbNoKey, hxClearBreaks ) +
        *new TMenuItem( "Set ~N~ext Statement", cmSetNext, kbNoKey, hxSetNext );
    TSubMenu &options = *new TSubMenu( "~O~ptions", kbAltO, hcOptionsMenu ) +
        *new TMenuItem( "~D~isplay...", cmDisplay, kbNoKey, hxDisplay ) +
        *new TMenuItem( "Help ~P~ath...", cmHelpPath, kbNoKey, hxHelpPath );
    TSubMenu &help = *new TSubMenu( "~H~elp", kbAltH, hcHelpMenu ) +
        *new TMenuItem( "~I~ndex", cmHelpIndex, kbNoKey, hxIndex ) +
        *new TMenuItem( "~C~ontents", cmHelpContents, kbNoKey, hxContents ) +
        *new TMenuItem( "~T~opic:", cmHelpTopicCmd, kbF1, hxTopic, "F1" ) +
        *new TMenuItem( "~U~sing Help", cmHelpUsing, kbShiftF1, hxUsing, "Shift+F1" ) +
        newLine() +
        *new TMenuItem( "~A~bout...", cmHelpAbout, kbNoKey, hxAbout );
    return new QbMenuBar( r, new TMenu( file + edit + view + search + run + debug + options + help ) );
}

TStatusLine *QbApp::initStatusLine( TRect r )
{
    r.a.y = r.b.y - 1;
    return new QbStatusLine( r,
        *new TStatusDef( 0, 0xFFFF ) +
            *new TStatusItem( 0, kbAltAlone, cmMenu ) +
            *new TStatusItem( 0, kbF6, cmNext ) );
}

TDeskTop *QbApp::initDeskTop( TRect r )
{
    r.a.y++;
    r.b.y--;
    TDeskTop *d = new TDeskTop( r );
    d->options &= ~ofBuffered;
    return d;
}

TPalette &QbApp::getPalette() const
{
    static TPalette palette( (const char *) qbPalette + 1, apEnd - 1 );
    palette = TPalette( (const char *) qbPalette + 1, apEnd - 1 );
    return palette;
}

void QbApp::outOfMemory()
{
    edMessage( "Out of memory", emOK );
}

void QbApp::idle()
{
    TApplication::idle();
    if (pendingHelpTopic && TopView() == this)
    {
        const char *t = pendingHelpTopic;
        pendingHelpTopic = 0;
        helpOpen( t );
    }
    static bool helpWas;
    if (helpIsOpen() != helpWas)
    {
        helpWas = helpIsOpen();
        layoutWindows();
    }
    ((QbStatusLine *) statusLine)->updatePos();
}

/* the event loop while a program is stopped (qbrun.cpp) */
void qbEventLoop( int *until )
{
    TProgram *app = TProgram::application;
    while (*until == 0)
    {
        TEvent e;
        app->getEvent( e );
        if (e.what != evNothing)
            app->handleEvent( e );
    }
}

/* F1 anywhere: help on what is under the cursor */
static bool escapingMenus;
static ushort pendingHelpCtx;

static bool isMenuCtx( ushort ctx )
{
    return ctx == hcMenuBar || (ctx >= hcFileMenu && ctx < hcDlgFind);
}

void QbApp::getEvent( TEvent &event )
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
        pendingHelpTopic = helpTopicFor( pendingHelpCtx );
    }
    TApplication::getEvent( event );
    if (event.what == evKeyDown && event.keyDown.keyCode == kbF1)
    {
        TView *top = TopView();
        ushort ctx = top ? top->getHelpCtx() : hcNoContext;
        if (isMenuCtx( ctx ))
        {
            escapingMenus = true;
            pendingHelpCtx = ctx;
            event.keyDown.keyCode = kbEsc;
        }
        else if (ctx >= hcDlgFind)
        {
            clearEvent( event );
            helpModal( helpTopicFor( ctx ) );
        }
    }
}

bool QbApp::okToDiscard()
{
    return docWindow->editor->valid( cmClose ) == True;
}

void QbApp::openFile( const char *name )
{
    QbEditor *ed = docWindow->editor;
    char buf[MAXPATH];
    strnzcpy( buf, name, sizeof buf );
    /* BASIC files: .BAS when no extension is given */
    const char *base = strrchr( buf, '\\' );
    base = base ? base + 1 : buf;
    if (!strchr( base, '.' ) && strlen( buf ) + 4 < sizeof buf)
        strcat( buf, ".BAS" );
    strnzcpy( ed->fileName, buf, sizeof ed->fileName );
    fexpand( ed->fileName );
    ed->isValid = True;
    if (!ed->loadFile())
    {
        ed->fileName[0] = 0;
        ed->setBufLen( 0 );
    }
    /* keywords in upper case, as when the lines were typed */
    for (uint p = 0; p < ed->bufLen; )
    {
        ed->recaseLine( p );
        uint nx = ed->nextLine( p );
        if (nx <= p)
            break;
        p = nx;
    }
    ed->modified = False;
    ed->setCurPtr( 0, 0 );
    ed->trackCursor( True );
    ed->drawView();
    docWindow->frame->drawView();
    runReset();
}

void QbApp::fileNew()
{
    if (!okToDiscard())
        return;
    QbEditor *ed = docWindow->editor;
    ed->fileName[0] = 0;
    ed->setBufLen( 0 );
    ed->modified = False;
    ed->drawView();
    docWindow->frame->drawView();
    runReset();
}

void QbApp::fileOpen()
{
    if (!okToDiscard())
        return;
    char name[MAXPATH] = "*.BAS";
    if (openDialog( name ))
    {
        int fd = open( name, O_RDONLY );
        if (fd < 0)
        {
            edMessage( "File not found", emOK );
            return;
        }
        ::close( fd );
        openFile( name );
    }
}

bool printText( const char *p, uint len )
{
    int fd = open( "PRN", O_WRONLY | O_BINARY );
    if (fd < 0)
        return false;
    bool ok = true;
    while (len > 0 && ok)
    {
        uint n = len > 1024 ? 1024 : len;
        ok = ::write( fd, p, n ) == (int) n;
        p += n;
        len -= n;
    }
    if (ok)
        ok = ::write( fd, "\f", 1 ) == 1;
    ::close( fd );
    return ok;
}

void QbApp::filePrint()
{
    QbEditor *ed = docWindow->editor;
    int what = printDialog( ed->hasSelection() );
    if (what == 0)
        return;
    uint from = 0, to = ed->bufLen;
    if (what == 1)
    {
        from = ed->selStart;
        to = ed->selEnd;
    }
    uint len = to - from;
    char *text = (char *) malloc( len + 3 );
    if (!text)
    {
        outOfMemory();
        return;
    }
    for (uint i = 0; i < len; ++i)
        text[i] = ed->bufChar( from + i );
    if (len == 0 || text[len - 1] != '\n')
    {
        text[len++] = '\r';
        text[len++] = '\n';
    }
    armdosCritError = -1;
    bool ok = printText( text, len );
    free( text );
    if (!ok)
        edMessage( "Device fault", emOK );
}

/* Edit / New SUB, New FUNCTION: appended to the program, cursor inside */
void QbApp::newProc( bool function )
{
    char name[41] = "";
    docWindow->editor->wordAtCursor( name, sizeof name );
    if (isKeyword( name, (int) strlen( name ), 0 ))
        name[0] = 0;
    if (!newProcDialog( function, name, sizeof name ))
        return;
    QbEditor *ed = docWindow->editor;
    char text[120];
    const char *kw = function ? "FUNCTION" : "SUB";
    bool nl = ed->bufLen > 0 && ed->bufChar( ed->bufLen - 1 ) != '\n';
    int n = snprintf( text, sizeof text, "%s\r\n%s %s\r\n", nl ? "\r\n" : "", kw, name );
    ed->lock();
    ed->setCurPtr( ed->bufLen, 0 );
    ed->insertText( text, n, False );
    uint inside = ed->curPtr;
    n = snprintf( text, sizeof text, "\r\nEND %s\r\n", kw );
    ed->insertText( text, n, False );
    ed->setCurPtr( inside, 0 );
    ed->trackCursor( True );
    ed->unlock();
    docWindow->select();
}

bool runStoppedCommand( TEvent &event );

void QbApp::handleEvent( TEvent &event )
{
    if (runStoppedCommand( event ))
        return;
    if (event.what == evCommand)
        switch (event.message.command)
        {
            case cmNew:     fileNew(); clearEvent( event ); return;
            case cmOpen:    fileOpen(); clearEvent( event ); return;
            case cmPrint:   filePrint(); clearEvent( event ); return;
            case cmSave:
            case cmSaveAs:
                /* for the program, wherever the cursor is */
                docWindow->editor->handleEvent( event );
                clearEvent( event );
                return;
            case cmNewSub:      newProc( false ); clearEvent( event ); return;
            case cmNewFunction: newProc( true ); clearEvent( event ); return;
            case cmSubs:        subsList(); clearEvent( event ); return;
            case cmOutputScreen: runOutputScreen(); clearEvent( event ); return;
            case cmStart:       runStart( false ); clearEvent( event ); return;
            case cmRestart:     runStart( true ); clearEvent( event ); return;
            case cmContinue:    runContinue(); clearEvent( event ); return;
            case cmStep:        runStep( 1 ); clearEvent( event ); return;
            case cmProcStep:    runStep( 2 ); clearEvent( event ); return;
            case cmToggleBreak: runToggleBreak(); clearEvent( event ); return;
            case cmClearBreaks: runClearBreaks(); clearEvent( event ); return;
            case cmSetNext:     runSetNext(); clearEvent( event ); return;
            case cmDisplay:
                displayDialog();
                clearEvent( event );
                return;
            case cmHelpPath:
                if (helpPathDialog( helpPath ) && helpPath[0])
                {
                    char p[80];
                    snprintf( p, sizeof p, "%s", helpPath );
                    int l = (int) strlen( p );
                    if (l > 3 && p[l - 1] == '\\')
                        p[l - 1] = 0;
                    if (p[1] == ':')
                        setdisk( toupper( (uchar) p[0] ) - 'A' );
                    if (chdir( p ) != 0)
                        edMessage( "Path not found", emOK );
                }
                clearEvent( event );
                return;
            case cmHelpIndex:    helpOpen( "Index" ); clearEvent( event ); return;
            case cmHelpContents: helpOpen( "Contents" ); clearEvent( event ); return;
            case cmHelpUsing:    helpOpen( "Using Help" ); clearEvent( event ); return;
            case cmHelpTopicCmd:
            {
                char w[40] = "";
                QbWindow *win = activeQbWindow();
                if (tvHelpIsCurrent())
                {
                    /* F1 in the help window: help on help */
                    helpOpen( "Using Help" );
                }
                else
                {
                    if (win)
                        win->editor->wordAtCursor( w, sizeof w );
                    if (!w[0] || !helpKeyword( w ))
                        helpOpen( "Survival Guide" );
                }
                clearEvent( event );
                return;
            }
            case cmHelpAbout:
                aboutDialog();
                clearEvent( event );
                return;
            case cmHelpTopic:
                helpOpen( event.message.infoPtr ? (const char *) event.message.infoPtr : "Survival Guide" );
                clearEvent( event );
                return;
            case cmHelpClose:
                helpClose();
                clearEvent( event );
                return;
            case cmNext:
                /* F6: program window -> Immediate -> (help) -> ... */
                if (deskTop->current == docWindow)
                    immWindow->select();
                else if (deskTop->current == immWindow && helpIsOpen())
                    tvHelpShow( tvHelpTopic() );
                else
                    docWindow->select();
                clearEvent( event );
                return;
        }
    TApplication::handleEvent( event );
}

/* the help window (if open) at the top, the program window, the
 * Immediate window at the bottom */
void layoutWindows()
{
    TRect r = TProgram::deskTop->getExtent();
    TRect imm( r.a.x, r.b.y - 3, r.b.x, r.b.y );
    TRect doc( r.a.x, r.a.y, r.b.x, r.b.y - 3 );
    if (helpIsOpen())
        doc.a.y = r.a.y + 11;
    docWindow->locate( doc );
    immWindow->locate( imm );
}

/* ---------------------------------------------------------------- SUBs */

void subsList()
{
    QbEditor *ed = docWindow->editor;
    static char *names[129];
    static int lines[129];
    int n = 0;
    for (int i = 0; names[i]; ++i)
    {
        free( names[i] );
        names[i] = 0;
    }
    names[n] = strdup( docWindow->getTitle( 12 ) );
    lines[n++] = 1;
    int curLine = ed->lineNumber( ed->curPtr ), current = 0;
    uint p = 0;
    int line = 1;
    while (p < ed->bufLen && n < 128)
    {
        uint e = ed->lineEnd( p );
        uint q = p;
        while (q < e && (ed->bufChar( q ) == ' ' || ed->bufChar( q ) == '\t'))
            ++q;
        char buf[80];
        int k = 0;
        for (uint s = q; s < e && k < 79; ++s)
            buf[k++] = ed->bufChar( s );
        buf[k] = 0;
        bool isSub = !strncasecmp( buf, "SUB ", 4 ), isFn = !strncasecmp( buf, "FUNCTION ", 9 );
        if (isSub || isFn)
        {
            const char *nm = buf + (isSub ? 4 : 9);
            while (*nm == ' ')
                ++nm;
            char name[48];
            int j = 0;
            while (nm[j] && nm[j] != ' ' && nm[j] != '(' && j < 40)
            {
                name[j] = nm[j];
                ++j;
            }
            name[j] = 0;
            char item[64];
            snprintf( item, sizeof item, "%s", name );
            names[n] = strdup( item );
            lines[n] = line;
            if (line <= curLine)
                current = n;
            ++n;
        }
        uint nx = ed->nextLine( p );
        if (nx <= p)
            break;
        p = nx;
        ++line;
    }
    names[n] = 0;
    int sel = subsDialog( names, n, current );
    if (sel >= 0)
    {
        docWindow->select();
        ed->gotoLine( lines[sel], false );
        /* the SUB's first line at the top of the window */
        ed->scrollTo( 0, lines[sel] - 1 );
    }
}

/* ---------------------------------------------------------------- main */

static void usage()
{
    static const char msg[] =
        "Provides a BASIC programming environment.\r\n\r\n"
        "QB [[/RUN] [drive:][path]filename] [/B] [/H] [/NOHI]\r\n\r\n"
        "  /RUN        Runs the specified BASIC program before displaying it.\r\n"
        "  [drive:][path]filename  Specifies the program file to load or run.\r\n"
        "  /B          Allows use of a monochrome monitor with a color graphics card.\r\n"
        "  /H          Displays the maximum number of lines possible for your hardware.\r\n"
        "  /NOHI       Allows the use of a monitor without high-intensity support.\r\n";
    ::write( 1, msg, sizeof msg - 1 );
}

extern "C" int qbe_init( void );

int main( int argc, char **argv )
{
    const char *fileName = 0;
    for (int i = 1; i < argc; ++i)
    {
        const char *a = argv[i];
        if (a[0] == '/' || a[0] == '-')
        {
            if (!strcasecmp( a + 1, "B" )) optBlackWhite = true;
            else if (!strcasecmp( a + 1, "G" ) || !strcasecmp( a + 1, "ED" )) ;
            else if (!strcasecmp( a + 1, "H" )) optMaxLines = true;
            else if (!strcasecmp( a + 1, "NOHI" )) optNoHighlight = true;
            else if (!strcasecmp( a + 1, "RUN" )) optRun = true;
            else if (!strcmp( a + 1, "?" )) { usage(); return 0; }
            else
            {
                static const char msg[] = "Invalid switch\r\n";
                ::write( 2, msg, sizeof msg - 1 );
                return 1;
            }
        }
        else if (!fileName)
            fileName = a;
    }
    if (!qbe_init())
    {
        static const char msg[] = "Out of memory\r\n";
        ::write( 2, msg, sizeof msg - 1 );
        return 1;
    }

    static const TScrollChars v = { '\x18', '\x19', '\xB1', '\xDB', '\xB1' };
    static const TScrollChars h = { '\x1B', '\x1A', '\xB1', '\xDB', '\xB1' };
    memcpy( TScrollBar::vChars, v, sizeof v );
    memcpy( TScrollBar::hChars, h, sizeof h );

    if (optBlackWhite)
        blackWhitePalette();
    if (optNoHighlight)
    {
        qbPalette[4] = qbPalette[2];  qbPalette[7] = qbPalette[5];
        qbPalette[40] = qbPalette[38]; qbPalette[45] = qbPalette[41];
        qbPalette[49] = qbPalette[47];
    }

    QbApp *app = new QbApp;
    if (optMaxLines)
        app->setScreenMode( TDisplay::smCO80 | TDisplay::smFont8x8 );
    TRect r = TProgram::deskTop->getExtent();
    docWindow = new QbWindow( TRect( r.a.x, r.a.y, r.b.x, r.b.y - 3 ), 0, false );
    immWindow = new QbWindow( TRect( r.a.x, r.b.y - 3, r.b.x, r.b.y ), 0, true );
    TProgram::deskTop->insert( immWindow );
    TProgram::deskTop->insert( docWindow );
    tvLineAttr = runLineAttr;
    applyDisplayOptions();
    if (optBlackWhite)
        blackWhitePalette();
    layoutWindows();
    docWindow->select();

    if (fileName)
    {
        app->openFile( fileName );
        if (optRun)
        {
            TEvent e;
            e.what = evCommand;
            e.message.command = cmStart;
            e.message.infoPtr = 0;
            app->putEvent( e );
        }
    }
    else if (welcomeDialog())
        helpOpen( "Survival Guide" );

    app->run();
    app->shutDown();
    TObject::destroy( app );
    return 0;
}
