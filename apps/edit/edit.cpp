/*
 * edit.cpp - EDIT.EXE, the ARM-DOS Editor: the application, menu bar,
 * status line and the document window.
 *
 * Looks and behaves like the MS-DOS 5 Editor: the menu bar File Edit Search
 * Options ... Help, one document window filling the screen, the status line
 * "ARM-DOS Editor  <F1=Help> Press ALT to choose commands" with the cursor
 * position on the right. Built on Turbo Vision (tvision/, magiblot's port,
 * MIT) with the ARM-DOS platform layer in armdos/.
 *
 *   EDIT [/B] [/H] [/NOHI] [[drive:][path]filename]
 *
 * Copyright (c) 2026 Europa Micro Systems. MIT License.
 */
#include "edit.h"
#include "edplat.h"

#define Uses_TFindDialogRec
#define Uses_TReplaceDialogRec
#include <tvision/tv.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include <malloc.h>
#include <armdos.h>

EdWindow *docWindow;
static const char *pendingHelpTopic;     /* F1 in a menu: shown when idle */

/* the heap continues in XMS when conventional memory is full: take up to
 * 8 MB of it (not all of it) */
extern "C" { unsigned _armdos_xms_kb = 8192; }
static bool optBlackWhite, optMaxLines, optNoHighlight;

/* -------------------------------------------------------------- palette */

/* Colours of the MS-DOS 5 Editor. Layout: Turbo Vision's cpAppColor
 * (app.h) for 1..135, then ours (edit.h). */
uchar edPalette[apEnd] = {
    0,
    /* 1 desktop */ 0x17,
    /* 2-7 menus: normal, disabled, shortcut, selected, sel. disabled, sel. shortcut */
    0x70, 0x78, 0x7F, 0x07, 0x08, 0x0F,
    /* 8-15 document window: frame, frame active, title (active), scroll bar
     * track, scroll bar arrows, text, selected text, reserved */
    0x17, 0x17, 0x70, 0x70, 0x70, 0x17, 0x71, 0x1F,
    /* 16-23 cyan window, 24-31 gray window (unused) */
    0x30, 0x30, 0x3F, 0x31, 0x31, 0x30, 0x3F, 0x3F,
    0x70, 0x70, 0x7F, 0x70, 0x70, 0x70, 0x7F, 0x7F,
    /* 32-63 dialogs (gray): frame passive, active, icon, scroll bar page,
     * controls, static text, label normal, selected, shortcut, button
     * normal, default, selected, disabled, shortcut, shadow, cluster normal,
     * selected, shortcut, input line normal, selected text, arrows, history
     * arrow, sides, history scroll bar page, controls, list normal, focused,
     * selected, divider, info pane, cluster disabled, reserved */
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
    /* ours: status line, status position, help text, help link, link under cursor */
    0x30, 0x30, 0x17, 0x1F, 0x71,
};

/* EDIT /B: monochrome */
static void blackWhitePalette()
{
    for (int i = 1; i < apEnd; ++i)
    {
        uchar a = edPalette[i];
        uchar fg = a & 0x0F, bg = (a >> 4) & 7;
        bool reverse = bg == 7 || bg == 3;
        uchar nfg = fg & 8 ? 0x0F : 0x07;
        edPalette[i] = reverse ? ((fg & 8) ? 0x7F : 0x70) : nfg;
    }
    edPalette[14] = 0x70;          /* selected text */
}

DisplayOptions displayOpts = { 7, 1, 1, 7, true, 8 };

extern int editTabSize;

void applyDisplayOptions()
{
    edPalette[13] = (uchar) ((displayOpts.normalBg << 4) | displayOpts.normalFg);
    edPalette[14] = (uchar) ((displayOpts.highBg << 4) | displayOpts.highFg);
    edPalette[8] = edPalette[9] = edPalette[13];
    edPalette[1] = edPalette[13];
    edPalette[apHelpText] = edPalette[13];
    edPalette[apHelpLink] = (uchar) ((displayOpts.normalBg << 4) | 0x0F);
    edPalette[apHelpLinkSel] = edPalette[14];
    editTabSize = displayOpts.tabStops < 1 ? 1 : displayOpts.tabStops > 99 ? 99 : displayOpts.tabStops;
    if (docWindow)
    {
        docWindow->showScrollBars(displayOpts.scrollBars);
        docWindow->redraw();        /* windows draw from a buffer: refresh it */
    }
    helpRedraw();
    if (TProgram::application)
        TProgram::application->redraw();
}

/* -------------------------------------------------------------- frames */

/* The document window's frame: ┌── Title ──┐, │ down both sides; the
 * scroll bars sit on the right edge and on the bottom row as in EDIT. */
class EdFrame : public TFrame
{
public:
    EdFrame( const TRect &r ) : TFrame( r ) {}
    virtual void draw();
    virtual TPalette &getPalette() const;
    virtual void handleEvent( TEvent &event );
};

#define cpEdFrame "\x01\x03"

TPalette &EdFrame::getPalette() const
{
    static TPalette palette( cpEdFrame, sizeof( cpEdFrame ) - 1 );
    return palette;
}

void EdFrame::handleEvent( TEvent &event )
{
    TView::handleEvent( event );        /* not movable, no icons */
}

void EdFrame::draw()
{
    TDrawBuffer b;
    TColorAttr cFrame = getColor( 1 );
    TColorAttr cTitle = cFrame;
    if (state & sfActive)
        cTitle = getColor( 2 );
    bool bars = !docWindow || owner != docWindow || displayOpts.scrollBars;

    b.moveChar( 0, '\xDA', cFrame, 1 );
    b.moveChar( 1, '\xC4', cFrame, size.x - 2 );
    b.moveChar( size.x - 1, '\xBF', cFrame, 1 );
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

    for (int y = 1; y < size.y - 1; ++y)
    {
        b.moveChar( 0, '\xB3', cFrame, 1 );
        b.moveChar( 1, ' ', cFrame, size.x - 2 );
        b.moveChar( size.x - 1, '\xB3', cFrame, 1 );
        writeLine( 0, y, size.x, 1, b );
    }
    if (size.y > 1)
    {
        if (bars)
        {
            b.moveChar( 0, '\xB3', cFrame, 1 );
            b.moveChar( 1, ' ', cFrame, size.x - 2 );
            b.moveChar( size.x - 1, '\xB3', cFrame, 1 );
        }
        else
        {
            b.moveChar( 0, '\xC0', cFrame, 1 );
            b.moveChar( 1, '\xC4', cFrame, size.x - 2 );
            b.moveChar( size.x - 1, '\xD9', cFrame, 1 );
        }
        writeLine( 0, size.y - 1, size.x, 1, b );
    }
}

/* -------------------------------------------------------------- editor */

EdEditor::EdEditor( const TRect &bounds, TScrollBar *h, TScrollBar *v, const char *fileName ) :
    TFileEditor( bounds, h, v, 0, fileName ? TStringView( fileName ) : TStringView() )
{
    autoIndent = True;
    helpCtx = hcEditor;
}

void EdEditor::updateCommands()
{
    TFileEditor::updateCommands();
    setCmdState( cmUndo, False );
    setCmdState( cmPrint, True );
}

static bool literalNext;

/* Find / Repeat Last Find / Change, as EDIT does them: from the cursor to
 * the end of the file, then on from the beginning ("wrapping"); Change
 * reports "Change complete". (TEditor's own search stops at the end.) */
void EdEditor::searchReplace( bool replace )
{
    ushort opts = editorFlags;
    uint origStart = selStart, origEnd = selEnd, origPtr = curPtr;
    uint limit = curPtr;            /* the second pass stops here */
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

void EdEditor::handleEvent( TEvent &event )
{
    if (event.what == evKeyDown)
    {
        if (literalNext)
        {
            /* Ctrl+P, then a control key: insert the character itself */
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
        else if (keyState == 0 && (event.keyDown.keyCode == kbCtrlW || event.keyDown.keyCode == kbCtrlZ))
        {
            /* WordStar: scroll one line */
            int dy = event.keyDown.keyCode == kbCtrlW ? -1 : 1;
            lock();
            scrollTo( delta.x, delta.y + dy );
            if (curPos.y < delta.y)
                setCurPtr( lineMove( curPtr, 1 ), 0 );
            else if (curPos.y >= delta.y + size.y)
                setCurPtr( lineMove( curPtr, -1 ), 0 );
            unlock();
            clearEvent( event );
            return;
        }
        else if (keyState == 0 && event.keyDown.keyCode == kbCtrlA)
        {
            event.what = evCommand;             /* WordStar: word left */
            event.message.command = cmWordLeft;
            event.message.infoPtr = 0;
        }
    }
    if (event.what == evKeyDown && (state & sfFocused))
    {
        /* ^Q F, ^Q A, ^L: our Find / Change / Repeat Last Find */
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
        clearEvent( event );        /* no context menu in EDIT */
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
    TFileEditor::handleEvent( event );
}

/* ------------------------------------------------------ document window */

#define cpEdWindow "\x08\x09\x0A\x0B\x0C\x0D\x0E\x0F"

EdWindow::EdWindow( const TRect &bounds, const char *fileName ) :
    TWindowInit( &EdWindow::initFrame ),
    TWindow( bounds, 0, wnNoNumber )
{
    flags = 0;
    state &= ~sfShadow;
    palette = wpBlueWindow;
    options |= ofTileable;
    TRect r = getExtent();
    vScroll = new TScrollBar( TRect( r.b.x - 1, 1, r.b.x, r.b.y - 1 ) );
    hScroll = new TScrollBar( TRect( 1, r.b.y - 1, r.b.x - 1, r.b.y ) );
    vScroll->growMode = gfGrowLoX | gfGrowHiX | gfGrowHiY;
    hScroll->growMode = gfGrowLoY | gfGrowHiX | gfGrowHiY;
    insert( vScroll );
    insert( hScroll );
    editor = new EdEditor( TRect( 1, 1, r.b.x - 1, r.b.y - 1 ), hScroll, vScroll, fileName );
    editor->growMode = gfGrowHiX | gfGrowHiY;
    insert( editor );
}

TFrame *EdWindow::initFrame( TRect r )
{
    return new EdFrame( r );
}

TPalette &EdWindow::getPalette() const
{
    static TPalette palette( cpEdWindow, sizeof( cpEdWindow ) - 1 );
    return palette;
}

const char *EdWindow::getTitle( short )
{
    static char buf[16];
    if (!editor || editor->fileName[0] == 0)
        return "Untitled";
    const char *p = editor->fileName, *q;
    if ((q = strrchr( p, '\\' )) != 0) p = q + 1;
    if ((q = strrchr( p, ':' )) != 0) p = q + 1;
    strnzcpy( buf, p, sizeof( buf ) );
    return buf;
}

void EdWindow::sizeLimits( TPoint &min, TPoint &max )
{
    TWindow::sizeLimits( min, max );
    min.x = 20;
    min.y = 4;
}

void EdWindow::changeBounds( const TRect &bounds )
{
    TWindow::changeBounds( bounds );
}

void EdWindow::handleEvent( TEvent &event )
{
    TWindow::handleEvent( event );
    if (event.what == evBroadcast && event.message.command == cmUpdateTitle)
    {
        if (frame)
            frame->drawView();
        clearEvent( event );
    }
}

void EdWindow::showScrollBars( bool on )
{
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

/* TMenuBar with the last item (Help) at the right end, as in EDIT. */
class EdMenuBar : public TMenuBar
{
public:
    EdMenuBar( const TRect &r, TMenu *m ) : TMenuBar( r, m ) {}
    virtual void draw();
    virtual TRect getItemRect( TMenuItem *item );
    virtual void handleEvent( TEvent &event )
    {
        /* Alt / F10 always starts at File, as in EDIT */
        if (event.what == evCommand && event.message.command == cmMenu && menu)
            menu->deflt = menu->items;
        TMenuBar::handleEvent( event );
    }
};

TRect EdMenuBar::getItemRect( TMenuItem *item )
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

void EdMenuBar::draw()
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

class EdStatusLine : public TStatusLine
{
public:
    EdStatusLine( const TRect &r, TStatusDef &d ) : TStatusLine( r, d ) {}
    virtual void draw();
    virtual TPalette &getPalette() const;
    virtual const char *hint( ushort ctx );
    void updatePos();
    TPoint shownPos { -1, -1 };
};

static const struct { ushort ctx; const char *text; } hints[] = {
    { hxNew, "Removes currently loaded file from memory" },
    { hxOpen, "Loads new file into memory" },
    { hxSave, "Saves current file" },
    { hxSaveAs, "Saves current file with specified name" },
    { hxPrint, "Prints specified text" },
    { hxExit, "Exits editor and returns to DOS" },
    { hxCut, "Deletes selected text and copies it to buffer" },
    { hxCopy, "Copies selected text to buffer" },
    { hxPaste, "Inserts buffer contents at current location" },
    { hxClear, "Deletes selected text without copying it to buffer" },
    { hxFind, "Finds specified text" },
    { hxRepeat, "Finds next occurrence of text specified in previous search" },
    { hxChange, "Finds and changes specified text" },
    { hxDisplay, "Changes display attributes" },
    { hxHelpPath, "Sets search path for Help files" },
    { hxGetStarted, "Displays information about loading and using the ARM-DOS Editor" },
    { hxKeyboard, "Displays navigation and editing keystrokes" },
    { hxAbout, "Displays product version and copyright information" },
};

const char *EdStatusLine::hint( ushort ctx )
{
    for (auto &h : hints)
        if (h.ctx == ctx)
            return h.text;
    return "";
}

#define cpEdStatusLine "\x88\x89"

TPalette &EdStatusLine::getPalette() const
{
    static TPalette palette( cpEdStatusLine, sizeof( cpEdStatusLine ) - 1 );
    return palette;
}

void EdStatusLine::draw()
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
    else if (helpCtx >= hcFileMenu && helpCtx < hcDlgFind)
        text = " F1=Help  Enter=Display Menu  Esc=Cancel  Arrow=Next Item";
    else if (helpCtx == hcMenuBar)
        text = " F1=Help  Enter=Display Menu  Esc=Cancel  Arrow=Next Item";
    else if (helpCtx >= hcDlgFind || helpCtx == hcDialog)
        text = " F1=Help  Enter=Execute  Esc=Cancel  Tab=Next Field  Arrow=Next Item";
    else if (helpCtx == hcHelp)
    {
        text = " <Shift+F1=Help> <F6=Window> <Esc=Cancel> <Ctrl+F1=Next> <Alt+F1=Back>";
        showPos = false;
    }
    else
    {
        text = " ARM-DOS Editor  <F1=Help> Press ALT to choose commands";
        showPos = true;
    }
    b.moveStr( 0, text, c, showPos ? posCol : size.x );
    if (showPos && docWindow)
    {
        TPoint p = docWindow->editor->curPos;
        snprintf( buf, sizeof buf, "\xB3 %05d:%03d", p.y + 1, p.x + 1 );
        b.moveStr( posCol, buf, getColor( 2 ) );
        shownPos = p;
    }
    writeLine( 0, 0, size.x, 1, b );
}

void EdStatusLine::updatePos()
{
    if (docWindow && docWindow->editor->curPos != shownPos && helpCtx <= hcEditor)
        drawView();
}

/* ----------------------------------------------------------- application */

class EditApp : public TApplication
{
public:
    EditApp();
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
};

EditApp::EditApp() :
    TProgInit( &EditApp::initStatusLine, &EditApp::initMenuBar, &EditApp::initDeskTop ),
    TApplication()
{
    TEditor::editorDialog = doEditDialog;
    TEditor::editorFlags = efPromptOnReplace;       /* no .BAK files */

    /* the clipboard: a hidden editor */
    TEditor *clip = new TFileEditor( TRect( 0, 0, 1, 1 ), 0, 0, 0, TStringView() );
    clip->hide();
    deskTop->insert( clip );
    TEditor::clipboard = clip;
}

TMenuBar *EditApp::initMenuBar( TRect r )
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
        *new TMenuItem( "Cl~e~ar", cmClear, kbDel, hxClear, "Del" );
    TSubMenu &search = *new TSubMenu( "~S~earch", kbAltS, hcSearchMenu ) +
        *new TMenuItem( "~F~ind...", cmFind, kbNoKey, hxFind ) +
        *new TMenuItem( "~R~epeat Last Find", cmSearchAgain, kbF3, hxRepeat, "F3" ) +
        *new TMenuItem( "~C~hange...", cmReplace, kbNoKey, hxChange );
    TSubMenu &options = *new TSubMenu( "~O~ptions", kbAltO, hcOptionsMenu ) +
        *new TMenuItem( "~D~isplay...", cmDisplay, kbNoKey, hxDisplay ) +
        *new TMenuItem( "Help ~P~ath...", cmHelpPath, kbNoKey, hxHelpPath );
    TSubMenu &help = *new TSubMenu( "~H~elp", kbAltH, hcHelpMenu ) +
        *new TMenuItem( "~G~etting Started", cmHelpGetStarted, kbNoKey, hxGetStarted ) +
        *new TMenuItem( "~K~eyboard", cmHelpKeyboard, kbNoKey, hxKeyboard ) +
        newLine() +
        *new TMenuItem( "~A~bout...", cmHelpAbout, kbNoKey, hxAbout );
    return new EdMenuBar( r, new TMenu( file + edit + search + options + help ) );
}

TStatusLine *EditApp::initStatusLine( TRect r )
{
    r.a.y = r.b.y - 1;
    return new EdStatusLine( r,
        *new TStatusDef( 0, 0xFFFF ) +
            *new TStatusItem( 0, kbAltAlone, cmMenu ) +
            *new TStatusItem( 0, kbF10, cmMenu ) +
            *new TStatusItem( 0, kbF6, cmNext ) );
}

TDeskTop *EditApp::initDeskTop( TRect r )
{
    r.a.y++;
    r.b.y--;
    TDeskTop *d = new TDeskTop( r );
    /* the desktop is always covered by the document window, which keeps a
     * buffer of its own: no need for another 48 KB copy of the screen */
    d->options &= ~ofBuffered;
    return d;
}

TPalette &EditApp::getPalette() const
{
    static TPalette palette( (const char *) edPalette + 1, apEnd - 1 );
    /* the palette is copied: refresh it (Options / Display changes colours) */
    palette = TPalette( (const char *) edPalette + 1, apEnd - 1 );
    return palette;
}

void EditApp::outOfMemory()
{
    edMessage( "Out of memory", emOK );
}

void EditApp::idle()
{
    TApplication::idle();
    if (pendingHelpTopic && TopView() == this)
    {
        const char *t = pendingHelpTopic;
        pendingHelpTopic = 0;
        helpOpen( t );
    }
    ((EdStatusLine *) statusLine)->updatePos();
}

/* F1 anywhere: help on what is under the cursor. In a menu the menus are
 * closed first (by feeding them Esc), then the topic is shown; in a dialog
 * the help window runs on top of the dialog. */
static bool escapingMenus;
static ushort pendingHelpCtx;

static bool isMenuCtx( ushort ctx )
{
    return ctx == hcMenuBar || (ctx >= hcFileMenu && ctx < hcDlgFind);
}

void EditApp::getEvent( TEvent &event )
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
        /* the menus are closed: show the topic once the main loop idles */
        escapingMenus = false;
        pendingHelpTopic = helpTopicFor( pendingHelpCtx );
    }
    TApplication::getEvent( event );
    if (event.what == evKeyDown && event.keyDown.keyCode == kbShiftF1)
    {
        clearEvent( event );
        event.what = evCommand;
        event.message.command = cmHelpTopic;
        event.message.infoPtr = (void *) "Using Help";
        return;
    }
    if (event.what == evKeyDown && event.keyDown.keyCode == kbF1)
    {
        TView *top = TopView();
        ushort ctx = top ? top->getHelpCtx() : hcNoContext;
        if (ctx <= hcEditor || ctx == hcHelp)
        {
            event.what = evCommand;
            event.message.command = cmHelpTopic;
            event.message.infoPtr = (void *) (ctx == hcHelp ? "Using Help" : "Survival Guide");
            return;
        }
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

bool EditApp::okToDiscard()
{
    return docWindow->editor->valid( cmClose ) == True;
}

void EditApp::openFile( const char *name )
{
    EdEditor *ed = docWindow->editor;
    strnzcpy( ed->fileName, name, sizeof ed->fileName );
    fexpand( ed->fileName );
    ed->isValid = True;
    if (!ed->loadFile())
    {
        ed->fileName[0] = 0;
        ed->setBufLen( 0 );
    }
    ed->modified = False;
    ed->setCurPtr( 0, 0 );
    ed->trackCursor( True );
    ed->drawView();
    docWindow->frame->drawView();
}

void EditApp::fileNew()
{
    if (!okToDiscard())
        return;
    EdEditor *ed = docWindow->editor;
    ed->fileName[0] = 0;
    ed->setBufLen( 0 );
    ed->modified = False;
    ed->drawView();
    docWindow->frame->drawView();
}

void EditApp::fileOpen()
{
    if (!okToDiscard())
        return;
    char name[MAXPATH] = "*.TXT";
    if (openDialog( name ))
    {
        int fd = open( name, O_RDONLY );
        if (fd < 0)
        {
            edMessage( "File not found.", emOK );
            return;
        }
        close( fd );
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
        ok = write( fd, p, n ) == (int) n;
        p += n;
        len -= n;
    }
    if (ok)
        ok = write( fd, "\f", 1 ) == 1;
    close( fd );
    return ok;
}

void EditApp::filePrint()
{
    EdEditor *ed = docWindow->editor;
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
    /* end with a line break */
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

void EditApp::handleEvent( TEvent &event )
{
    if (event.what == evCommand)
        switch (event.message.command)
        {
            case cmNew:     fileNew(); clearEvent( event ); return;
            case cmOpen:    fileOpen(); clearEvent( event ); return;
            case cmPrint:   filePrint(); clearEvent( event ); return;
            case cmDisplay:
                displayDialog();
                clearEvent( event );
                return;
            case cmHelpPath:
                helpPathDialog( helpPath );
                clearEvent( event );
                return;
            case cmHelpGetStarted:
                helpOpen( "Getting Started" );
                clearEvent( event );
                return;
            case cmHelpKeyboard:
                helpOpen( "Keyboard" );
                clearEvent( event );
                return;
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
        }
    TApplication::handleEvent( event );
}

/* Lay out the document window (and the help window, if open). */
void layoutWindows()
{
    TRect r = TProgram::deskTop->getExtent();
    if (helpIsOpen())
        r.a.y = r.b.y - 8 < r.a.y + 3 ? r.a.y + 3 : r.b.y - 8;
    docWindow->locate( r );
}

/* ---------------------------------------------------------------- main */

/* SET EDITSTATS=1: memory figures to the debug port (E9h) at exit, for
 * the tests. */
static void stats( const char *when = "exit" )
{
    if (!getenv( "EDITSTATS" ))
        return;
    { char b[64]; snprintf( b, sizeof b, "EDIT: at %s\n", when ); armdos_debug( b ); }
    struct mallinfo mi = mallinfo();
    uint8_t *psp = (uint8_t *) _armdos_psp;
    uint8_t *brk = (uint8_t *) sbrk( 0 );
    /* the heap is the DOS block above the stack, then (when that is full)
     * one XMS block; HIMEM gives the first one out at 1 MB + 64 KB */
    unsigned long conv = (unsigned long) (_armdos_blockend - _armdos_heap_start);
    unsigned long xmsUsed = (uintptr_t) brk >= 0x110000 ? (uintptr_t) brk - 0x110000 : 0;
    unsigned long hole = xmsUsed ? 0x110000 - (uintptr_t) _armdos_blockend : 0;
    char b[240];
    snprintf( b, sizeof b, "EDIT: program %lu bytes (PSP+image+bss+stack), DOS block %lu bytes "
              "(heap part %lu), XMS heap used %lu of %lu KB, malloc'd %lu bytes, free %lu bytes\n",
              (unsigned long) (_armdos_heap_start - psp), (unsigned long) (_armdos_blockend - psp),
              xmsUsed ? conv : (unsigned long) (brk - _armdos_heap_start),
              (xmsUsed + 1023) / 1024, armdos_xms_heap_size() / 1024,
              (unsigned long) mi.uordblks - hole, (unsigned long) mi.fordblks );
    armdos_debug( b );
}

static void usage()
{
    static const char msg[] =
        "Starts the ARM-DOS Editor, which creates and changes ASCII files.\r\n\r\n"
        "EDIT [[drive:][path]filename] [/B] [/G] [/H] [/NOHI]\r\n\r\n"
        "  [drive:][path]filename  Specifies the ASCII file to edit.\r\n"
        "  /B                      Allows use of a monochrome monitor with a color\r\n"
        "                          graphics card.\r\n"
        "  /G                      Provides the fastest update of a CGA screen.\r\n"
        "  /H                      Displays the maximum number of lines possible for\r\n"
        "                          your hardware.\r\n"
        "  /NOHI                   Allows the use of a monitor without high-intensity\r\n"
        "                          support.\r\n";
    write( 1, msg, sizeof msg - 1 );
}

int main( int argc, char **argv )
{
    const char *fileName = 0;
    for (int i = 1; i < argc; ++i)
    {
        const char *a = argv[i];
        if (a[0] == '/' || a[0] == '-')
        {
            if (!strcasecmp( a + 1, "B" )) optBlackWhite = true;
            else if (!strcasecmp( a + 1, "G" )) ;
            else if (!strcasecmp( a + 1, "H" )) optMaxLines = true;
            else if (!strcasecmp( a + 1, "NOHI" )) optNoHighlight = true;
            else if (!strcmp( a + 1, "?" )) { usage(); return 0; }
            else
            {
                static const char msg[] = "Invalid switch\r\n";
                write( 2, msg, sizeof msg - 1 );
                return 1;
            }
        }
        else if (!fileName)
            fileName = a;
    }

    /* EDIT's scroll bars: arrows, a shaded track and a solid box */
    static const TScrollChars v = { '\x18', '\x19', '\xB1', '\xDB', '\xB1' };
    static const TScrollChars h = { '\x1B', '\x1A', '\xB1', '\xDB', '\xB1' };
    memcpy( TScrollBar::vChars, v, sizeof v );
    memcpy( TScrollBar::hChars, h, sizeof h );

    if (optBlackWhite)
        blackWhitePalette();
    if (optNoHighlight)
    {
        /* /NOHI: access letters in the colour of the text around them */
        edPalette[4] = edPalette[2];  edPalette[7] = edPalette[5];
        edPalette[40] = edPalette[38]; edPalette[45] = edPalette[41];
        edPalette[49] = edPalette[47];
    }

    /* the help file lives next to EDIT.EXE unless Help Path says otherwise */
    if (argv[0] && argv[0][0])
    {
        strnzcpy( helpPath, argv[0], sizeof helpPath );
        char *s = strrchr( helpPath, '\\' );
        if (s) *s = 0; else helpPath[0] = 0;
    }

    stats( "start" );
    EditApp *app = new EditApp;
    stats( "app" );
    if (optMaxLines)
        app->setScreenMode( TDisplay::smCO80 | TDisplay::smFont8x8 );   /* 50 lines */
    TRect r = TProgram::deskTop->getExtent();
    docWindow = new EdWindow( r, 0 );
    TProgram::deskTop->insert( docWindow );
    applyDisplayOptions();
    if (optBlackWhite)
        blackWhitePalette();

    if (fileName)
        app->openFile( fileName );
    else if (welcomeDialog())
        helpOpen( "Survival Guide" );
    stats( "loaded" );

    app->run();
    stats();
    app->shutDown();
    TObject::destroy( app );
    return 0;
}
