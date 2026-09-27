/*
 * process.h - running other programs (Microsoft C style), via INT 21h
 * AX=4B00h. P_WAIT runs the child and returns its exit code; P_OVERLAY
 * (and the exec* functions) run it and then exit with its code, since DOS
 * cannot replace a running program. P_NOWAIT is not supported (-1, EINVAL).
 * No extension: tries the name as given, then .COM, .EXE, .BAT (a .BAT runs
 * through %COMSPEC% /C). The ...p variants search PATH.
 */
#ifndef _ARMDOS_PROCESS_H
#define _ARMDOS_PROCESS_H
#include <stdlib.h>
#include <unistd.h>
#ifdef __cplusplus
extern "C" {
#endif

#define P_WAIT    0
#define P_NOWAIT  1
#define P_OVERLAY 2
#define _P_WAIT    P_WAIT
#define _P_NOWAIT  P_NOWAIT
#define _P_OVERLAY P_OVERLAY

int spawnl(int mode, const char *path, const char *arg0, ...);
int spawnle(int mode, const char *path, const char *arg0, ... /*, NULL, char *const envp[] */);
int spawnlp(int mode, const char *file, const char *arg0, ...);
int spawnlpe(int mode, const char *file, const char *arg0, ...);
int spawnv(int mode, const char *path, char *const argv[]);
int spawnve(int mode, const char *path, char *const argv[], char *const envp[]);
int spawnvp(int mode, const char *file, char *const argv[]);
int spawnvpe(int mode, const char *file, char *const argv[], char *const envp[]);

/* exec*: declared by <unistd.h>; they spawn, then exit with the child's code. */
int execlpe(const char *file, const char *arg0, ...);
int execvpe(const char *file, char *const argv[], char *const envp[]);

/* Run a program with a raw DOS command tail (no argv joining); envseg 0 =
 * inherit our environment. Returns the exit code (AL) or -1. */
int _armdos_exec(const char *path, const char *tail, unsigned envseg);

int getpid(void);
#define _getpid getpid
#define _spawnl spawnl
#define _spawnv spawnv
#define _spawnlp spawnlp
#define _spawnvp spawnvp
#define _spawnle spawnle
#define _spawnve spawnve
#define _cexit() ((void)0)
#define _c_exit() ((void)0)

#ifdef __cplusplus
}
#endif
#endif
