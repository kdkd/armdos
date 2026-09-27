                     Tiny C Compiler for ARM-DOS
                     ===========================

TCC is a complete C compiler that runs on this computer and makes ARM-DOS
programs: it preprocesses, compiles to ARM machine code, assembles and links
in one go, and writes an ordinary .EXE you can run straight away.

    C:\>EDLIN HELLO.C                (or COPY CON HELLO.C, end with F6/Ctrl-Z)
    C:\>TCC HELLO.C
    C:\>HELLO
    Hello, world!

Files
-----
    C:\DOS\TCC.EXE       the compiler (on the PATH)
    C:\TC\INCLUDE\       header files: the C library (stdio.h, stdlib.h,
                         string.h, math.h ...) and the DOS ones (dos.h,
                         conio.h, bios.h, io.h, direct.h, process.h, armdos.h)
    C:\TC\LIB\CRT0.O     program start-up code
    C:\TC\LIB\LIBC.A     the run-time library (C library, maths, DOS calls,
                         software floating point)
    C:\TC\SAMPLES\       example programs; BUILD.BAT compiles them all

TCC finds INCLUDE and LIB in the directory named by the TCCDIR environment
variable, else next to TCC.EXE, else in C:\TC.

Using it
--------
    TCC PROG.C                 compile and link PROG.EXE
    TCC -o GAME.EXE A.C B.C    several source files, name the program
    TCC -c UTIL.C              compile only: UTIL.O
    TCC MAIN.C UTIL.O          link with an object file
    TCC -run PROG.C [args]     compile in memory and run at once
    TCC -Wall PROG.C           more warnings
    TCC -E PROG.C              show the preprocessed source
    TCC -ar rcs LIBX.A A.O B.O make a library; use it with -lX (LIBX.A)
    TCC -Wl,--stack=65536 ...  give the program a bigger stack (16 KB default)
    TCC                        list the options

Error messages name the file and line:

    HELLO.C:5: error: ';' expected (got "}")

The C dialect is C99 with most GNU extensions (inline, __attribute__, statement
expressions, inline assembler with GCC syntax) and parts of C11.

About the machine
-----------------
* int, long and pointers are 32 bits; long long is 64 bits.
* A pointer is a flat address: the text screen is at (char *)0xB8000, the
  VGA 256-colour frame buffer at (char *)0xA0000. There are no segments;
  "far", "near" and "huge" are accepted and ignored.
* int86()/intdos() from dos.h raise software interrupts exactly as on a PC
  (INT 21h is the ARM instruction "svc #0x21"); inp()/outp() reach the
  PC's I/O ports.
* The processor (an ARM926) has no floating point unit: float and double
  work through the software floating point in LIBC.A. printf("%f") is
  linked in automatically when a program uses floating point.
* printf has no %lld / %llu (the small C library leaves them out).
* Programs grow their heap into extended memory through HIMEM.SYS (XMS),
  so malloc() can use megabytes.

TinyCC (C) 2001-2025 Fabrice Bellard and the TinyCC contributors, licensed
under the GNU Lesser General Public License 2.1 (COPYING.TXT). ARM-DOS port:
soft-float code generator, .EXE output and the DOS version of the compiler.
