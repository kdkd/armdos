# TCC — the Tiny C Compiler, running on ARM-DOS

A C compiler that runs *on* ARM-DOS and produces ARM-DOS programs:

```
C:\>EDLIN SQUARES.C          (or COPY CON HELLO.C ... F6)
C:\>TCC SQUARES.C
C:\>SQUARES
1 squared is 1
...
```

It is [TinyCC](https://repo.or.cz/tinycc.git) (mob branch, commit `3dc99db`,
version 0.9.28rc, LGPL-2.1), given a soft-float ARM code generator, a writer
for ARM-DOS's MZ+AR1 `.EXE` format, a DOS host port and some ARMv5 fixes, then
compiled with the SDK into `TCC.EXE`. Every compiled program is real ARM code:
TCC.EXE even compiles its own 83,000-line source on ARM-DOS, byte-identical to
the Linux cross build.

## On the disk

| where | what |
|---|---|
| `C:\DOS\TCC.EXE` | the compiler (328 KB; on the PATH, like every `build/*.EXE`) |
| `C:\TC\INCLUDE\` | 109 headers: newlib's C library (8.3 names), the SDK's DOS headers (`dos.h conio.h bios.h io.h direct.h process.h armdos.h`), TinyCC's `stddef.h stdarg.h float.h ...`, `iso646.h` |
| `C:\TC\LIB\CRT0.O` | the SDK's start-up code |
| `C:\TC\LIB\LIBC.A` | libdos + newlib-nano libc + libm + libgcc in one archive (debug info stripped), 2.7 MB |
| `C:\TC\SAMPLES\` | `HELLO.C`, `MODE13.C` (320x200x256 via `int86`, palette cycling), `TUNE.C` (PC speaker), `ASMDEMO.C` (inline ARM assembler: `mrc p15`, `mrs cpsr`, `clz`, `umull`, `svc #0x21`), `MANDEL.C` (soft-float), `BUILD.BAT` |
| `C:\TC\README.TXT`, `COPYING.TXT` | user documentation (DOS style), the LGPL |

C:\TC takes 3.1 MB, TCC.EXE 0.3 MB. TCC finds `INCLUDE` and `LIB` in `%TCCDIR%`,
else next to TCC.EXE (if there is an `INCLUDE\STDIO.H` there), else `C:\TC`.

## Usage

`TCC` alone prints a Turbo-C-flavoured banner:

```
Tiny C Compiler for ARM-DOS  Version 0.9.28rc
Copyright (c) 2001-2025 Fabrice Bellard and the TinyCC authors (LGPL)
Syntax is: TCC [ options ] file[s]
  -c        Compile only (make .O)       -o name   Name of the output file
  -run      Compile and run at once      -E        Preprocess only
  ...
```

`TCC PROG.C` writes `PROG.EXE` (the output name follows the source's case);
`-c` writes `PROG.O` (ELF object); `.C .S .ASM .O .A` are recognised in any case;
libraries can be named anywhere on the line (searched after the objects, as
DOS linkers do); `TCC -ar rcs LIBX.A A.O B.O` makes a library; `TCC -run PROG.C
args` compiles into memory and runs; `-Wl,--stack=N` (default 16384),
`-Wl,--min-extra=N`, `-Wl,--max-extra=N` set the AR1 header fields;
`-Wl,--no-gc-sections` keeps unused library functions; `-Wl,--oformat=elf32-littlearm`
writes an ELF instead (debugging). Errors: `HELLO.C:5: error: ';' expected (got 'return')`,
exit code 1, no `.EXE` left behind.

## Building

`apps/tcc/app.mk` (in `make` / `make tcc`):

* `build/TCC.EXE` — `src/tcc.c` (TinyCC's one-source build) with
  `-DTCC_HOST_ARMDOS`, SDK flags, `-lm`, float printf, `--stack 65536`, linked
  twice (see `-run` below).
* `build/tcc/host/armdos-tcc` — the same compiler for Linux (host `cc`), used by
  the tests; `build/tcc/sys/` is its include/lib tree (long names).
* `build/tcc/libc.a` — `ar -M` merge + `strip -g`.
* `build/tcc/disk/TC/` — C:\TC (`tools/mkdisk.mjs`, `tools/mksys.mjs`);
  `hd.json` puts it on the hard disk (`DISK_DEPS` has the stamp).
* `src/` is the patched TinyCC tree (only the files the ARM target needs);
  `tinycc-armdos.patch` is every change as a diff against upstream
  (`tools/mkpatch.sh CLONE`, or `TINYCC_SRC=CLONE`, regenerates it from an upstream clone).

## Design decisions

### 1. Output format: TCC writes MZ+AR1 itself

A new output format, `TCC_OUTPUT_FORMAT_ARMDOS` (the default), next to TCC's
ELF writer (`tccelf.c`):

* layout: all sections linked at address 0 (`text_addr = 0`, 8-byte section
  alignment, no page gaps); read-only data, code, data, then every NOBITS
  section, the first one 8-aligned; the image is `[0, end of the last PROGBITS)`
  rounded up to 8 (as `sdk/link.ld` does: crt0 clears .bss a word at a time from
  `__bss_start__`, and from an unaligned address that cleared the last bytes of
  .data - seen with Arm's newlib-nano, whose layout put the program's own .data
  last), the rest is `bss_size`.
* relocations: while TCC applies relocations (`relocate_section`) it records
  the address of every `R_ARM_ABS32`/`R_ARM_TARGET1` word in an allocated
  section whose symbol is not absolute or weak-undefined — exactly
  `sdk/elf2exe.mjs`'s rule; PC-relative types need nothing; absolute types that
  cannot be expressed as "add the load base" (MOVW/MOVT_ABS, ABS16/8, GOT,
  TLS) are errors naming the section and symbol. Sorted, de-duplicated,
  checked (aligned, inside the image).
* the writer emits the MZ header, the 8086 stub (`sdk/mzstub.asm`'s bytes),
  the 64-byte AR1 header, the image and the table: the same layout as
  `sdk/armexe.mjs`'s `buildExe` (checked with `sdk/exeinfo.mjs`).
* `__bss_start__`, `__bss_end__`, `__image_end`, `__end__`, `__exidx_start/end`
  (what `sdk/link.ld` provides; `_end`/`end` TCC defines itself) are
  section-relative, so references to them are relocated.
* section garbage collection like GNU ld's `--gc-sections` (libdos is built
  with `-ffunction-sections`): hello world is 21 KB (GCC with `-fno-builtin`:
  25 KB; TCC does not turn `printf("...\n")` into `puts`).

### 2. Run-time library: the SDK's, shipped as-is

TCC links GCC-built objects: `CRT0.O` and one merged `LIBC.A` (libdos first,
so its definitions win; then newlib-nano's libc, libm, libgcc). One archive
means one resolution pass handles the libc↔libdos↔libgcc cycles. What TCC's
ARM linker needed for newlib/libgcc objects:

* `.ARM.exidx` sections are not loaded (TCC already skips them);
  `__exidx_start/end` are defined empty for libgcc's unwinder.
* `R_ARM_TARGET2` is handled as `REL32` (GNU ld `--target2=rel`, what the SDK
  uses); `R_ARM_V4BX` keeps the `bx` (upstream rewrote it to `mov pc` for
  ARMv4 — we are ARMv5TE).
* weak undefined references no longer pull archive members, and an undefined
  symbol stays strong if any reference is strong (GNU ld semantics). Without
  this, newlib-nano's weak reference to `_printf_float` dragged in dtoa & co.
* headers: newlib's (newlib-nano's `newlib.h`), minus ones that make no sense
  here (pthread, termios, pwd, elf ...), with DOS 8.3 names on the disk (what
  DOS itself makes of `_default_types.h` when TCC opens it: `_DEFAULT.H`;
  `mksys.mjs` checks for collisions); the SDK's `fcntl.h`/`malloc.h` use
  `#include_next`, so they are merged with newlib's into one file. One newlib
  header is patched (`machine/_default_types.h`: take the GCC branch for TCC,
  avoiding a `limits.h` include cycle). TCC predefines, for ARM-DOS, the GCC
  type macros newlib looks for (`__INT8_TYPE__`, `__SCHAR_MAX__`, ...),
  `__USER_LABEL_PREFIX__`, `__ASMNAME`, and `__ARMDOS__ __MSDOS__ __DOS__
  __arm__ __ARM_EABI__ __ARM_ARCH_5TE__ __SOFTFP__` (no `__linux__`).
* printf float support is linked automatically: a compilation unit that uses
  floating point gets undefined references to `_printf_float`/`_scanf_float`
  (the ARM-DOS analogue of `__fltused`), so programs without floats stay small
  and `printf("%f")` never silently prints nothing.

### 3. Floating point: a real soft-float code generator

TinyCC's ARM backend only knew VFP (and old FPA) instructions. Rather than
touching the emulator, `arm-gen.c` got `TCC_ARM_SOFTFLOAT`: there are no float
registers (`RC_FLOAT` = `RC_INT`, 5 allocatable core registers); a float lives
in one core register, a double in a pair like a long long (the generic
two-word machinery in `tccgen.c`, as RISC-V uses for long double); `+ - * /`,
comparisons and all conversions are calls to the run-time ABI helpers
(`__aeabi_dadd`, `__aeabi_dcmplt`, `__aeabi_i2d`, `__aeabi_d2lz`, ...) in
libgcc; negation flips the sign bit (IEEE: `-x` is not `0-x`); arguments and
results follow the base AAPCS, so TCC code and GCC `-mfloat-abi=soft` code
call each other freely. `long double` = `double`.

### 4. Memory and speed

TCC.EXE: 317 KB image + 83 KB bss + 64 KB stack = 457 KB of conventional
memory (580 KB are free at the prompt). Its heap grows into extended memory
through XMS (SDK run-time); without HIMEM.SYS it uses raw extended memory
(`_armdos_raw_extmem = 1`). Emulated time at 100 MHz (the tests measure from
Enter to the next prompt):

| | |
|---|---|
| `TCC HELLO.C` (5,300 lines with stdio.h, link against LIBC.A, write the EXE) | **0.37 s** |
| `TCC MANDEL.C` | 0.44 s |
| `BUILD.BAT` (5 samples) | 2.1 s |
| `TCC -run ARGS.C one two` | 0.14 s |
| `TCC TCC.C` (TinyCC itself, 82,719 lines, 514 KB output) | 2.7 s |

### 5. `TCC -run`

Works. The program is compiled into a block of TCC's heap (extended memory:
code runs from anywhere, no MMU) and `main` is called. It must not get its own
copy of the C library: two newlib/libdos run-times would each grow a heap in
the same DOS memory block. So TCC.EXE exports its own C library: it is linked
twice, and the second link adds a table (`tools/mkhostsyms.mjs`) of the 420
global symbols the first link took from libdos/newlib/libgcc; `-run` defines
them as weak absolute symbols, so a program may still define its own
`strdup`, and `LIBC.A` supplies what TCC.EXE lacks (e.g. `sqrt`). TCC.EXE
links the float printf for this reason. `exit()` in the program ends TCC.EXE
with that code, as the program's `main` returning does. `TCC -run TCC.C
HELLO.C` works: TinyCC, compiled on ARM-DOS, runs from extended memory and
compiles HELLO.C (its `.EXE` build, 514 KB, is too big for 640 KB and gets
DOS's "Program too big to fit in memory").

### 6. DOS feel

Banner as above; `FILE.C:LINE: error: ...`; Ctrl-Z ends a source file (COPY
CON and EDLIN write one); `HELLO.C` → `HELLO.EXE`; paths with `\` or `/`,
case-insensitive, `;` path separator; `TCCDIR`; `-bench` timing from the BIOS
tick count; `ar`'s temporary file is `LIBX.$$$`; `BUILD.BAT`; `README.TXT`.

## Other TinyCC fixes (bugs that ARM-DOS exposed)

* **Inline asm operands in r4-r8 were loaded into the wrong register**
  (`arm-asm.c` allocates hardware register numbers but called `load()` with
  them, where 4 means r12): any asm with enough clobbers broke. Fixed by
  translating (raw hardware registers encoded as 0x20+n for `load/store`).
* `mrs`/`msr` added to the assembler (the SDK's `_enable()/_disable()` use them).
* **No unaligned access on ARMv5**: struct returns in r0 loaded a word from
  a global whose own address may be unaligned (upstream assumed only the
  offset mattered — fine on ARMv7); and members of packed structs (`#pragma
  pack`, `__attribute__((packed))` — e.g. `struct find_t`'s `size` at offset
  26) are now read and written byte-wise (as packed bit-fields already were),
  `&member` still gives a normal pointer.
* `o()`'s prototype (`uint32_t` is `unsigned long` in newlib), `ldexpl`
  (-lm), `.C` in upper case, 8.3-safe temporary names.

## Tests

`make tcc-test` (`node apps/tcc/tests/run.mjs`, ~60 s host time) — everything
compiled **by TCC.EXE running on ARM-DOS** in the emulator:

* **samples**: `BUILD.BAT` builds all five; HELLO's text; ASMDEMO's CP15 ID
  `41069265`, CPSR, `clz`, `umull`, `svc #0x21`; MANDEL's output identical to
  the GCC build of MANDEL.C; TUNE's 30 notes on the PC speaker (frequencies
  checked); MODE13 in mode 13h with 400 sampled pixels right, back to text
  mode on a key; screenshots in `build/tcc/test/`.
* **session**: `TCC` banner; `COPY CON HELLO.C` + `TCC HELLO.C` + `HELLO`;
  `DIR`; `-c`, linking an `.O`, `-v`; `EDLIN SQUARES.C` (its ^Z) + compile + run;
  `TCC -ar` and `-L. -lutil`.
* **errors**: syntax error and undeclared identifier messages with file:line,
  ERRORLEVEL 1, no `.EXE`; link error; missing file.
* **suite**: 92 programs of TinyCC's `tests/tests2` (`tests/tests2/`, the
  ones GCC can build too) + ours (`tests/progs/`: soft-float, packed
  structs, the DOS/BIOS API of the SDK headers) are compiled on ARM-DOS by
  TCC.EXE; each is also built by the host GCC SDK; both run in the same
  machine: **95/95 identical outputs**. Left out: x86/arm64/riscv asm, threads,
  TLS, bounds checker/backtrace (not built), tests of TCC's own diagnostics,
  multi-file tests, and four that cannot compare with GCC (see `SKIP2` in
  `run.mjs`: `%ld` of a long long, a `__TINYC__`-only part, `__ILP32__`, a
  self-include by long name).
* **run**: `TCC -run ARGS.C one two` (argv, `sqrt` from LIBC.A, `%f`, exit code).
* **selfhost**: TCC.EXE compiles TCC.C (82,719 lines, 2.7 s); `TCC2.EXE` is
  byte-identical to the Linux cross compiler's build; it does not fit in
  640 KB; `TCC -run TCC.C ... HELLO.C` compiles HELLO.C and HELLO2 runs.

Result: 48 checks, all passing (57 s host time).

## Deviations / limitations

* newlib-nano's printf/scanf: no `%lld`/`%llu`/`%llx` (they print `ld`),
  no positional arguments. The full newlib would need a differently built
  libdos (`struct _reent` differs), so TCC programs use what SDK programs use.
* TCC does not optimise: code is ~1.5x larger and slower than GCC `-Os`.
* No `_Complex`, atomics, VLAs in structs, thread-local storage; no bounds
  checker (`-b`) or backtraces (`-bt`); `-g` debug info is not stored in the
  `.EXE`; no C++.
* Packed-struct members of pointer/float type are still accessed with word
  instructions (only short/int/long members are handled byte-wise).
* `TCC -run`: the program shares TCC.EXE's heap, stdio and exit; `atexit`
  handlers run when the program exits through `exit()`.
* The ARM-DOS changes are all under `TCC_TARGET_ARMDOS` / `TCC_ARM_SOFTFLOAT` /
  `TCC_HOST_ARMDOS` except the inline-asm register fix and `mrs`/`msr`, which
  apply to every ARM target.

No kernel requests; no SDK changes.

## Licences

TinyCC: LGPL-2.1 (`src/COPYING`, on the disk as `C:\TC\COPYING.TXT`);
`tests/tests2/*.c` are TinyCC's tests. LIBC.A contains newlib (BSD-style
licences) and libgcc (GPL with the GCC Runtime Library Exception), as every
SDK-built program does.
