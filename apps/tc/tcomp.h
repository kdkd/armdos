/*
 * tcomp.h - TC.EXE's compiler: TinyCC (apps/tcc, LGPL-2.1) linked in as
 * libtcc and driven in-process (tcomp.c).
 */
#ifndef TCOMP_H
#define TCOMP_H

#ifdef __cplusplus
extern "C" {
#endif

enum { TCM_ERROR = 0, TCM_WARNING = 1, TCM_NOTE = 2 };

typedef struct TcJob
{
    const char *files[64];  /* sources (.C), objects (.O), libraries (.A) */
    int nfiles;
    const char *out;        /* output file: .EXE, or .O with objonly */
    int objonly;            /* Compile to OBJ (Alt-F9) */
    const char *tcdir;      /* where LIB\CRT0.O and LIB\LIBC.A are (C:\TC) */
    const char *incdirs;    /* ';'-separated include directories */
    const char *libdirs;    /* ';'-separated library directories */
    const char *defines;    /* "NAME=VALUE;NAME" */
    int warnings;           /* 0 none, 1 TinyCC's defaults, 2 -Wall */
    int warnerror;          /* treat warnings as errors */
    unsigned stack;         /* stack size of the program (0 = default) */
    /* called for every error / warning; file may be 0 (link errors) */
    void (*message)(void *ctx, int kind, const char *file, int line, const char *text);
    /* called while compiling: the file being read, the lines so far */
    void (*progress)(void *ctx, const char *file, long total, long filelines);
    void *ctx;
    /* results */
    long lines;             /* lines compiled (all files, headers included) */
    int errors, warns;
    unsigned long outsize;  /* size of the output file */
    /* the files the build read (sources and the user's headers), for Make:
     * a list of strings, 0-terminated (valid until the next tc_compile) */
    char **deps;
} TcJob;

/* compile (and link); returns the number of errors */
int tc_compile(TcJob *job);

#ifdef __cplusplus
}
#endif

#endif
