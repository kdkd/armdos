/*
 * tcbuild.cpp - TC.EXE: Compile, Make, Link, Build all, Run and the User
 * screen. The compiler is TinyCC linked in (tcomp.c): no EXEC, the
 * "Compiling" box shows the lines as TinyCC reads them. Run EXECs the .EXE
 * with the user screen showing; the IDE, in extended memory, leaves the
 * program nearly all of conventional memory.
 *
 * Copyright (c) 2026 Europa Micro Systems. MIT License.
 */
#include "tc.h"
#include "tcomp.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <process.h>
#include <malloc.h>
#include <sys/stat.h>
#include <armdos.h>

bool buildDirty = true;
long lastLines;
unsigned long lastExeSize;
int lastExitCode;
int totalWarnings, totalErrors;

/* what the last successful build of lastTarget read */
static char lastTarget[MAXPATH];
static char *lastDeps[65];

/* ------------------------------------------------------ Compiling box */

struct CompileState
{
    char title[16];         /* Compiling / Linking */
    char mainFile[40];
    char current[50];
    long lines, fileLines;
    int warns, errors, fileWarns, fileErrors;
    unsigned long memK;
    char result[40];        /* "" while working */
};

class TcCompileView : public TView
{
public:
    TcCompileView( const TRect &r, CompileState *s ) : TView( r ), st( s ) {}
    virtual void draw()
    {
        TDrawBuffer b;
        TColorAttr c = getColor( 1 ), hi = getColor( 2 );
        char line[80];
        for (int y = 0; y < size.y; ++y)
        {
            b.moveChar( 0, ' ', c, size.x );
            line[0] = 0;
            switch (y)
            {
            case 1: snprintf( line, sizeof line, "  Main file: %s", st->mainFile ); break;
            case 2: snprintf( line, sizeof line, "  %s: %s", st->title, st->current ); break;
            case 4: snprintf( line, sizeof line, "                    Total    File" ); break;
            case 5: snprintf( line, sizeof line, "  Lines compiled: %7ld %7ld", st->lines, st->fileLines ); break;
            case 6: snprintf( line, sizeof line, "        Warnings: %7d %7d", st->warns, st->fileWarns ); break;
            case 7: snprintf( line, sizeof line, "          Errors: %7d %7d", st->errors, st->fileErrors ); break;
            case 9: snprintf( line, sizeof line, "  Available memory: %luK", st->memK ); break;
            }
            b.moveStr( 0, line, c );
            if (y == 11 && st->result[0])
            {
                int l = (int) strlen( st->result );
                b.moveStr( (size.x - l) / 2, st->result, hi );
            }
            writeLine( 0, y, size.x, 1, b );
        }
    }
    virtual TPalette &getPalette() const
    {
        /* dialog static text, dialog label highlight */
        static TPalette p( "\x06\x09", 2 );
        return p;
    }
    CompileState *st;
};

static CompileState cst;
static TDialog *compileBox;
static TcCompileView *compileView;
static unsigned long lastDrawTick;

static void boxOpen( const char *title, const char *mainFile )
{
    memset( &cst, 0, sizeof cst );
    snprintf( cst.title, sizeof cst.title, "%s", title );
    snprintf( cst.mainFile, sizeof cst.mainFile, "%s", mainFile );
    cst.memK = _memmax() / 1024;
    compileBox = new TDialog( TRect( 0, 0, 42, 15 ), title );
    compileBox->options |= ofCentered;
    compileBox->flags &= ~(wfMove | wfGrow | wfZoom | wfClose);
    compileView = new TcCompileView( TRect( 2, 1, 40, 14 ), &cst );
    compileBox->insert( compileView );
    TProgram::deskTop->insert( compileBox );
    compileBox->drawView();
    TScreen::flushScreen();
}

static void boxUpdate( bool force )
{
    if (!compileBox)
        return;
    unsigned long t = *(volatile uint32_t *) 0x46C;
    if (!force && t == lastDrawTick)
        return;
    lastDrawTick = t;
    compileView->drawView();
    TScreen::flushScreen();
}

static void boxTitle( const char *title )
{
    snprintf( cst.title, sizeof cst.title, "%s", title );
    if (compileBox)
    {
        delete[] (char *) compileBox->title;
        compileBox->title = newStr( title );
        compileBox->frame->drawView();
    }
    boxUpdate( true );
}

static void boxClose( bool waitKey )
{
    if (!compileBox)
        return;
    if (waitKey)
    {
        boxUpdate( true );
        tvWaitAnyKey();
    }
    TObject::destroy( compileBox );
    compileBox = 0;
    compileView = 0;
}

/* ----------------------------------------------------- compiler hooks */

static char currentSource[MAXPATH];

static const char *baseName( const char *p )
{
    const char *s = strrchr( p, '\\' ), *t = strrchr( p, '/' );
    if (t > s) s = t;
    return s ? s + 1 : p;
}

static void onMessage( void *, int kind, const char *file, int line, const char *text )
{
    char full[MAXPATH] = "";
    if (file)
        fullPath( full, file );
    messageAdd( kind == TCM_WARNING ? 'W' : 'E', full, line, text );
    if (kind == TCM_WARNING)
    {
        cst.warns++;
        cst.fileWarns++;
    }
    else
    {
        cst.errors++;
        cst.fileErrors++;
    }
    boxUpdate( true );
}

static void onProgress( void *, const char *file, long total, long fileLines )
{
    if (!file)
    {
        /* linking */
        return;
    }
    cst.lines = total;
    if (fileLines >= 0)
        cst.fileLines = fileLines;
    char up[50];
    snprintf( up, sizeof up, "%s", baseName( file ) );
    for (char *p = up; *p; ++p)
        *p = toupper( (uchar) *p );
    snprintf( cst.current, sizeof cst.current, "%s", up );
    boxUpdate( false );
}

/* ------------------------------------------------------------ targets */

static bool newer( const char *a, const char *b )
{
    struct stat sa, sb;
    if (stat( a, &sa ) != 0)
        return true;
    if (stat( b, &sb ) != 0)
        return true;
    return sa.st_mtime > sb.st_mtime;
}

/* NAME.EXT -> outDir\NAME.ext (or next to the source) */
static void outName( char *out, const char *src, const char *ext )
{
    char full[MAXPATH];
    fullPath( full, src );
    char *dot = strrchr( full, '.' );
    char *sl = strrchr( full, '\\' );
    if (dot && dot > sl)
        *dot = 0;
    if (opts.outDir[0])
    {
        char dir[MAXPATH];
        fullPath( dir, opts.outDir );
        size_t n = strlen( dir );
        snprintf( out, MAXPATH, "%s%s%s%s", dir, n && dir[n - 1] == '\\' ? "" : "\\", sl ? sl + 1 : full, ext );
    }
    else
        snprintf( out, MAXPATH, "%s%s", full, ext );
}

/* the project's items (paths relative to the project's directory), or the
 * active edit window's file */
static int targetSources( char files[][MAXPATH], int max, char *exe, bool needSaved )
{
    int n = 0;
    if (projectName[0] && projectItems && projectItems->getCount() > 0)
    {
        char dir[MAXPATH];
        strcpy( dir, projectName );
        char *s = strrchr( dir, '\\' );
        if (s) s[1] = 0; else dir[0] = 0;
        for (int i = 0; i < projectItems->getCount() && n < max; ++i)
        {
            const char *it = (const char *) projectItems->at( i );
            if (it[1] == ':' || it[0] == '\\')
                fullPath( files[n++], it );
            else
            {
                char buf[MAXPATH];
                snprintf( buf, sizeof buf, "%s%s", dir, it );
                fullPath( files[n++], buf );
            }
        }
        outName( exe, projectName, ".EXE" );
        return n;
    }
    TcEditWindow *w = currentEditor();
    if (!w)
    {
        messageBox( "No file to compile: open a C source file first.", mfError | mfOKButton );
        return 0;
    }
    if (!w->editor->fileName[0])
    {
        if (!needSaved || !w->editor->save())
            return 0;
    }
    strcpy( files[n++], w->editor->fileName );
    outName( exe, w->editor->fileName, ".EXE" );
    return n;
}

void targetName( char *exe, const char *ext )
{
    char files[1][MAXPATH];
    exe[0] = 0;
    targetSources( files, 1, exe, false );
}

/* save the modified edit windows of the build (Turbo C's "auto save") */
static bool saveSources( char files[][MAXPATH], int n )
{
    TView *first = TProgram::deskTop->first(), *p = first;
    if (!p)
        return true;
    do
    {
        if (p->helpCtx == hcEditorWin && p != clipWindow)
        {
            TcEditor *e = ((TcEditWindow *) p)->editor;
            if (e->modified)
            {
                bool part = !e->fileName[0];
                for (int i = 0; i < n && !part; ++i)
                    if (!strcasecmp( files[i], e->fileName ))
                        part = true;
                /* headers are part of it too: save every modified file */
                const char *dot = strrchr( e->fileName, '.' );
                if (dot && !strcasecmp( dot, ".H" ))
                    part = true;
                if (part && !e->save())
                    return false;
            }
        }
        p = p->next;
    } while (p != first);
    return true;
}

static void fillJob( TcJob &job )
{
    memset( &job, 0, sizeof job );
    /* {B} = the parent of the first library directory (LIB\CRT0.O, LIB\LIBC.A) */
    static char tcdir[MAXPATH];
    char first[MAXPATH];
    snprintf( first, sizeof first, "%s", opts.libDirs[0] ? opts.libDirs : "C:\\TC\\LIB" );
    char *semi = strchr( first, ';' );
    if (semi) *semi = 0;
    fullPath( tcdir, first );
    char *sl = strrchr( tcdir, '\\' );
    if (sl && sl > tcdir + 2) *sl = 0;
    job.tcdir = tcdir;
    job.incdirs = opts.incDirs;
    job.libdirs = opts.libDirs;
    job.defines = opts.defines;
    job.warnings = opts.warnings;
    job.warnerror = opts.warnError;
    job.stack = opts.stackKB * 1024u;
    job.message = onMessage;
    job.progress = onProgress;
}

static void messagesFor( const char *what, const char *name )
{
    char t[120];
    snprintf( t, sizeof t, "%s %s:", what, baseName( name ) );
    messageAdd( ' ', "", 0, t );
}

static void showErrors()
{
    messageShow( true );
    messageSelectFirst();
}

/* ---------------------------------------------------------- commands */

int buildCompile( bool objOnly )
{
    (void) objOnly;
    TcEditWindow *w = currentEditor();
    if (!w)
    {
        messageBox( "No file to compile: open a C source file first.", mfError | mfOKButton );
        return 1;
    }
    if ((w->editor->modified || !w->editor->fileName[0]) && !w->editor->save())
        return 1;
    char src[MAXPATH], obj[MAXPATH];
    strcpy( src, w->editor->fileName );
    outName( obj, src, ".O" );
    messagesClear();
    messagesFor( "Compiling", src );
    TcJob job;
    fillJob( job );
    job.files[0] = src;
    job.nfiles = 1;
    job.out = obj;
    job.objonly = 1;
    boxOpen( "Compiling", baseName( src ) );
    snprintf( cst.current, sizeof cst.current, "%s", baseName( src ) );
    int errs = tc_compile( &job );
    cst.lines = job.lines;
    cst.fileLines = job.lines;
    lastLines = job.lines;
    totalWarnings = job.warns;
    totalErrors = job.errors;
    snprintf( cst.result, sizeof cst.result, "%s : Press any key",
              errs ? "Errors" : job.warns ? "Warnings" : "Success" );
    boxClose( true );
    if (errs || job.warns)
        showErrors();
    return errs;
}

int buildMake( bool all, bool quiet )
{
    static char files[64][MAXPATH];
    char exe[MAXPATH];
    int n = targetSources( files, 64, exe, true );
    if (n == 0)
        return 1;
    if (!saveSources( files, n ))
        return 1;
    /* up to date? */
    bool need = all || buildDirty || strcasecmp( exe, lastTarget ) != 0 || access( exe, 0 ) != 0;
    for (int i = 0; i < n && !need; ++i)
        if (newer( files[i], exe ))
            need = true;
    for (int i = 0; lastDeps[i] && !need; ++i)
        if (newer( lastDeps[i], exe ))
            need = true;
    if (!need)
    {
        if (!quiet)
        {
            boxOpen( "Make", baseName( exe ) );
            snprintf( cst.current, sizeof cst.current, "%s is up to date", baseName( exe ) );
            cst.lines = cst.fileLines = 0;
            snprintf( cst.result, sizeof cst.result, "Success : Press any key" );
            boxClose( true );
        }
        return -1;
    }
    messagesClear();
    for (int i = 0; i < n; ++i)
        messagesFor( "Compiling", files[i] );
    TcJob job;
    fillJob( job );
    for (int i = 0; i < n; ++i)
        job.files[i] = files[i];
    job.nfiles = n;
    job.out = exe;
    boxOpen( "Compiling", baseName( n == 1 ? files[0] : exe ) );
    snprintf( cst.current, sizeof cst.current, "%s", baseName( files[0] ) );
    /* Linking comes after compiling: tcomp.c calls onProgress(0) first */
    int errs = tc_compile( &job );
    cst.lines = job.lines;
    lastLines = job.lines;
    totalWarnings = job.warns;
    totalErrors = job.errors;
    if (errs == 0)
    {
        messagesFor( "Linking", exe );
        boxTitle( "Linking" );
        snprintf( cst.current, sizeof cst.current, "%s", baseName( exe ) );
        lastExeSize = job.outsize;
        buildDirty = false;
        strcpy( lastTarget, exe );
        for (int i = 0; lastDeps[i]; ++i)
        {
            free( lastDeps[i] );
            lastDeps[i] = 0;
        }
        int k = 0;
        for (int i = 0; job.deps && job.deps[i] && k < 64; ++i)
        {
            char full[MAXPATH];
            fullPath( full, job.deps[i] );
            lastDeps[k++] = strdup( full );
        }
        lastDeps[k] = 0;
    }
    else
    {
        lastTarget[0] = 0;
        remove( exe );
    }
    bool stop = errs || (opts.makeBreak == 1 && job.warns);
    snprintf( cst.result, sizeof cst.result, "%s : Press any key",
              errs ? "Errors" : job.warns ? "Warnings" : "Success" );
    boxClose( !quiet || stop || job.warns );
    if (errs || job.warns)
        showErrors();
    return stop ? 1 : 0;
}

void buildLinkOnly()
{
    static char files[64][MAXPATH];
    char exe[MAXPATH];
    int n = targetSources( files, 64, exe, true );
    if (n == 0)
        return;
    static char objs[64][MAXPATH];
    TcJob job;
    fillJob( job );
    messagesClear();
    for (int i = 0; i < n; ++i)
    {
        const char *dot = strrchr( files[i], '.' );
        if (dot && (!strcasecmp( dot, ".O" ) || !strcasecmp( dot, ".A" )))
            strcpy( objs[i], files[i] );
        else
            outName( objs[i], files[i], ".O" );
        if (access( objs[i], 0 ) != 0)
        {
            char t[120];
            snprintf( t, sizeof t, "Unable to open %s (compile it first: Alt+F9)", baseName( objs[i] ) );
            messageAdd( 'E', "", 0, t );
            showErrors();
            return;
        }
        job.files[i] = objs[i];
    }
    job.nfiles = n;
    job.out = exe;
    messagesFor( "Linking", exe );
    boxOpen( "Linking", baseName( exe ) );
    snprintf( cst.current, sizeof cst.current, "%s", baseName( exe ) );
    int errs = tc_compile( &job );
    lastExeSize = job.outsize;
    snprintf( cst.result, sizeof cst.result, "%s : Press any key", errs ? "Errors" : "Success" );
    boxClose( true );
    if (errs)
        showErrors();
}

/* back from a program: text mode again, then redraw the IDE */
static void backToIde()
{
    uint8_t mode = *(volatile uint8_t *) 0x449;
    if (mode != 3 && mode != 7 && mode != 2)
    {
        struct armregs r = {};
        r.r0 = 0x0003;
        _armdos_int10( &r );
    }
    TProgram::application->resume();
    tvInvalidateScreen();
    TProgram::application->redraw();
}

void userScreen()
{
    TProgram::application->suspend();
    tvWaitAnyKey();
    backToIde();
}

void buildRun()
{
    int r = buildMake( false, true );
    if (r > 0)
        return;
    char exe[MAXPATH];
    targetName( exe, ".EXE" );
    if (access( exe, 0 ) != 0)
    {
        messageBox( mfError | mfOKButton, "Unable to execute %s.", exe );
        return;
    }
    /* the arguments, split like the DOS command line */
    static char argbuf[sizeof opts.args + 1];
    char *argv[24];
    int argc = 0;
    argv[argc++] = exe;
    strcpy( argbuf, opts.args );
    for (char *p = strtok( argbuf, " \t" ); p && argc < 23; p = strtok( 0, " \t" ))
        argv[argc++] = p;
    argv[argc] = 0;

    TProgram::application->suspend();
    int code = spawnv( P_WAIT, exe, argv );
    backToIde();
    if (code < 0)
        messageBox( mfError | mfOKButton, "Unable to execute %s.", exe );
    else
        lastExitCode = code;
}
