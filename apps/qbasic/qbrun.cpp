/*
 * qbrun.cpp - QB.EXE: running the program. The text of the program window
 * is loaded into the interpreter (bw/qbengine.inc) line by line, and the
 * number the interpreter gives every line is kept, so that errors, breaks
 * and steps come back as editor lines.
 *
 *   Shift+F5  Start        F5   Continue (or Start)     F4  output screen
 *   F8        Step         F10  Procedure Step          F9  breakpoint
 *   Ctrl+Break stops a running program; F5 continues it.
 *
 * While the program runs, Turbo Vision is suspended: the screen is the
 * program's ("output screen"), which Turbo Vision saves when it comes back
 * and shows again for F4. "Press any key to continue" at the end.
 *
 * Copyright (c) 2026 Europa Micro Systems. MIT License.
 */
#include "qb.h"
#include "qbengine.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <armdos.h>

/* interpreter line number -> editor line */
static int *mapNumber, *mapLine;
static int nMap, capMap;
static bool changed = true;         /* the program was edited since loaded */
static bool started;                /* a run was started (variables exist) */
void qbEventLoop( int *until );     /* qb.cpp */

/* breakpoints: editor lines */
static int breaks[64];
static int nBreaks;
bool traceOn;

static int editorLineOf( int number )
{
    /* numbers are ascending */
    int lo = 0, hi = nMap - 1;
    while (lo <= hi)
    {
        int m = (lo + hi) / 2;
        if (mapNumber[m] == number)
            return mapLine[m];
        if (mapNumber[m] < number)
            lo = m + 1;
        else
            hi = m - 1;
    }
    return 0;
}

static int numberOfLine( int line )
{
    for (int i = 0; i < nMap; ++i)
        if (mapLine[i] >= line)
            return mapNumber[i];
    return 0;
}

static bool isBreakLine( int line )
{
    for (int i = 0; i < nBreaks; ++i)
        if (breaks[i] == line)
            return true;
    return false;
}

extern "C" int isBreakNumber( int number )
{
    return nBreaks && isBreakLine( editorLineOf( number ) );
}

/* ------------------------------------------------------------ loading */

static void mapAdd( int number, int line )
{
    if (nMap == capMap)
    {
        capMap = capMap ? capMap * 2 : 256;
        mapNumber = (int *) realloc( mapNumber, capMap * sizeof( int ) );
        mapLine = (int *) realloc( mapLine, capMap * sizeof( int ) );
    }
    mapNumber[nMap] = number;
    mapLine[nMap] = line;
    ++nMap;
}

static void loadProgram()
{
    QbEditor *ed = docWindow->editor;
    qbe_load_begin();
    nMap = 0;
    static char line[512];
    /* the SUB names first: "Name args" is a CALL */
    for (uint p = 0; p < ed->bufLen; )
    {
        uint e = ed->lineEnd( p ), q = p;
        while (q < e && (ed->bufChar( q ) == ' ' || ed->bufChar( q ) == '\t'))
            ++q;
        int n = 0;
        for (uint s = q; s < e && n < 80; ++s)
            line[n++] = ed->bufChar( s );
        line[n] = 0;
        if (!strncasecmp( line, "SUB ", 4 ))
        {
            char name[48];
            const char *s = line + 4;
            while (*s == ' ')
                ++s;
            int k = 0;
            while (s[k] && (isalnum( (uchar) s[k] ) || s[k] == '_' || s[k] == '.') && k < 40)
            {
                name[k] = s[k];
                ++k;
            }
            name[k] = 0;
            qbe_declare_sub( name );
        }
        uint nx = ed->nextLine( p );
        if (nx <= p)
            break;
        p = nx;
    }
    int lineNo = 1;
    for (uint p = 0; p < ed->bufLen; ++lineNo)
    {
        uint e = ed->lineEnd( p );
        int n = 0;
        for (uint s = p; s < e && n < (int) sizeof line - 1; ++s)
        {
            char c = ed->bufChar( s );
            line[n++] = c == '\t' ? ' ' : c;
        }
        line[n] = 0;
        int number = qbe_load_line( line );
        if (number > 0)
            mapAdd( number, lineNo );
        uint nx = ed->nextLine( p );
        if (nx <= p)
            break;
        p = nx;
    }
    changed = false;
}

/* ------------------------------------------------------- the screens */

static void flushKeys()
{
    while (tvKeyWaiting())
        tvGetKey();
}

static void toOutput( bool newRun )
{
    TProgram::application->suspend();
    qbe_output_begin( newRun ? 1 : 0 );
}

/* a screen fingerprint: did an Immediate statement show something? */
static uint32_t screenHash()
{
    uint32_t h = *(volatile uint16_t *) 0x450 ^ (*(volatile uint8_t *) 0x449 << 20);
    volatile uint16_t *v = (volatile uint16_t *) 0xB8000;
    for (int i = 0; i < 80 * 25; ++i)
        h = h * 31 + v[i];
    return h;
}

static void backToIde( bool pressKey )
{
    if (pressKey)
    {
        flushKeys();
        qbe_output_message( "Press any key to continue" );
        tvWaitAnyKey();
        qbe_output_unmessage();
    }
    qbe_output_end();
    uint8_t mode = *(volatile uint8_t *) 0x449;
    uint16_t cols = *(volatile uint16_t *) 0x44A;
    if ((mode != 3 && mode != 7 && mode != 2) || cols != 80)
    {
        struct armregs r = {};
        r.r0 = 0x0003;
        _armdos_int10( &r );
    }
    TProgram::application->resume();
    tvInvalidateScreen();
    TProgram::application->redraw();
}

/* ------------------------------------------------------------- marks */

static void clearMark()
{
    if (tvMarkEditor)
    {
        tvMarkEditor = 0;
        docWindow->editor->drawView();
    }
}

/* a line highlighted, the cursor on it */
static void markLine( int line, uint8_t attr )
{
    QbEditor *ed = docWindow->editor;
    docWindow->select();
    ed->gotoLine( line, false );
    tvMarkEditor = ed;
    tvMarkLine = ed->lineStartOf( line );
    tvMarkAttr = attr;
    ed->drawView();
}

static void showError()
{
    int number;
    const char *msg = qbe_error( &number );
    int line = number ? editorLineOf( number ) : 0;
    char text[160];
    snprintf( text, sizeof text, "%s", msg && *msg ? msg : "Syntax error" );
    if (line > 0)
        markLine( line, 0x70 );
    edMessage( text, emOK, hcDlgError );
    if (line > 0)
    {
        docWindow->select();
        docWindow->editor->gotoLine( line, false );
    }
}

/* ------------------------------------------------------ stopped runs */

/* While the program runs, qbe_hook (onStatement) is called before every
 * statement. To stop there - a breakpoint, a step, Ctrl+Break, STOP - the
 * environment comes back on the screen and runs its event loop right
 * there, until Continue / Step / Procedure Step (the program goes on) or
 * something that ends the program (Start, New, Exit ...: qbe_abort). */
enum { goNone, goContinue, goStep, goProcStep, goAbort };
static int stepMode;                /* 0 run, 1 step, 2 procedure step */
static int stepCalls;
static int stoppedLine;             /* the editor line we are stopped at, 0 = running */
static int stoppedCalls;
static int pauseAction;
static TEvent abortEvent;           /* a command that ended the stopped program */
static bool haveAbortEvent;

bool runSuspended()
{
    return stoppedLine != 0;
}

/* the event loop while stopped (qb.cpp routes Run/Debug commands here) */
bool runStoppedCommand( TEvent &event )
{
    if (!stoppedLine || event.what != evCommand)
        return false;
    switch (event.message.command)
    {
        case cmContinue:
            pauseAction = changed ? goAbort : goContinue;
            if (changed)
            {
                /* edited: start again */
                abortEvent = event;
                abortEvent.message.command = cmStart;
                haveAbortEvent = true;
            }
            break;
        case cmStep:
        case cmProcStep:
            if (changed)
            {
                pauseAction = goAbort;
                abortEvent = event;
                haveAbortEvent = true;
            }
            else
                pauseAction = event.message.command == cmStep ? goStep : goProcStep;
            break;
        case cmStart:
        case cmRestart:
        case cmNew:
        case cmOpen:
        case cmQuit:
            /* end the program first, then do it */
            pauseAction = goAbort;
            abortEvent = event;
            haveAbortEvent = true;
            break;
        default:
            return false;
    }
    TProgram::application->clearEvent( event );
    return true;
}

static int prevLine;                 /* the line of the statement before */

static void onStatement( int number, int calls, int why )
{
    bool stop = false;
    int line = editorLineOf( number );
    if (why)
        stop = true;                                /* Ctrl+Break, STOP */
    else if (stepMode == 1)
        stop = true;
    else if (stepMode == 2 && calls <= stepCalls)
        stop = true;
    else if (nBreaks && line != prevLine && isBreakLine( line ))
        stop = true;
    prevLine = line;
    stoppedLine = 0;
    if (!stop)
        return;
    backToIde( false );
    stoppedLine = line ? line : 1;
    stoppedCalls = calls;
    markLine( stoppedLine, 0x1F );
    pauseAction = goNone;
    haveAbortEvent = false;
    qbEventLoop( &pauseAction );
    clearMark();
    switch (pauseAction)
    {
        case goContinue:  stepMode = 0; break;
        case goStep:      stepMode = 1; break;
        case goProcStep:  stepMode = 2; stepCalls = calls; break;
        default:
            stoppedLine = 0;
            qbe_abort();
    }
    toOutput( false );
}

/* ---------------------------------------------------------------- run */

static void runProgram( int mode )
{
    clearMark();
    loadProgram();
    started = true;
    if (qbe_start() == QBE_ERROR)
    {
        showError();
        return;
    }
    stepMode = mode;
    stepCalls = 0;
    stoppedLine = 0;
    prevLine = 0;
    qbe_hook = onStatement;
    toOutput( true );
    int r = qbe_run();
    qbe_hook = 0;
    stoppedLine = 0;
    switch (r)
    {
        case QBE_END:
            backToIde( true );
            break;
        case QBE_SYSTEM:
            backToIde( false );
            break;
        case QBE_ERROR:
            backToIde( true );
            showError();
            break;
        case QBE_ABORT:
            backToIde( false );
            if (haveAbortEvent)
            {
                haveAbortEvent = false;
                TProgram::application->putEvent( abortEvent );
            }
            break;
    }
}

void runStart( bool restart )
{
    runProgram( restart ? 1 : 0 );
}

void runContinue()
{
    runProgram( 0 );
}

void runStep( int mode )
{
    /* the first statement, not yet executed */
    runProgram( 1 );
    (void) mode;
}

void runImmediate( const char *line )
{
    const char *p = line;
    while (*p == ' ')
        ++p;
    if (!*p)
        return;
    if (changed && !stoppedLine)
    {
        /* the program's SUBs and labels, keeping the variables */
        loadProgram();
        changed = true;
    }
    toOutput( false );
    uint32_t before = screenHash();
    int r = qbe_immediate( p );
    bool shown = screenHash() != before || qbe_basic_mode() != 0;
    if (r == QBE_ERROR)
    {
        backToIde( shown );
        int n;
        const char *msg = qbe_error( &n );
        edMessage( msg && *msg ? msg : "Syntax error", emOK, hcDlgError );
        immWindow->select();
        return;
    }
    backToIde( shown && r != QBE_SYSTEM );
    if (stoppedLine)
        markLine( stoppedLine, 0x1F );
    immWindow->select();
}

void runOutputScreen()
{
    TProgram::application->suspend();
    flushKeys();
    tvWaitAnyKey();
    TProgram::application->resume();
    tvInvalidateScreen();
    TProgram::application->redraw();
}

void runReset()
{
    changed = true;
    clearMark();
    nBreaks = 0;
}

void runEditorChanged()
{
    if (!changed)
    {
        changed = true;
        if (!stoppedLine)
            clearMark();
    }
}

/* ------------------------------------------------------ breakpoints */

void runToggleBreak()
{
    QbEditor *ed = docWindow->editor;
    int line = ed->lineNumber( ed->curPtr );
    for (int i = 0; i < nBreaks; ++i)
        if (breaks[i] == line)
        {
            breaks[i] = breaks[--nBreaks];
            ed->drawView();
            return;
        }
    if (nBreaks < 64)
        breaks[nBreaks++] = line;
    ed->drawView();
}

void runClearBreaks()
{
    nBreaks = 0;
    docWindow->editor->drawView();
}

void runSetNext()
{
    QbEditor *ed = docWindow->editor;
    if (!runSuspended() || changed)
    {
        edMessage( "Cannot continue", emOK );
        return;
    }
    int line = ed->lineNumber( ed->curPtr );
    int number = numberOfLine( line );
    if (number && qbe_set_next( number ))
    {
        stoppedLine = editorLineOf( number );
        markLine( stoppedLine, 0x1F );
    }
}

/* breakpoint lines in red (tvlib's line colour hook) */
uint8_t runLineAttr( TEditor *ed, unsigned linePtr )
{
    if (!nBreaks || !docWindow || ed != docWindow->editor)
        return 0;
    int line = ((QbEditor *) ed)->lineNumber( linePtr );
    return isBreakLine( line ) ? 0x47 : 0;
}
