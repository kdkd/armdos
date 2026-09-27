# mslib - running Microsoft's MS-DOS 4.0 C utilities on ARM-DOS

The shared layer under the utilities compiled from Microsoft's MIT-licensed
MS-DOS 4.0 source (`apps/fc`, `apps/mem`, `apps/attrib`, `apps/fdisk`). The
original programs link 8086 assembly services and binary-only tools; this
directory re-implements them in C, with the same interfaces, so the
Microsoft C code compiles for ARM nearly unchanged.

| file | replaces | what |
|---|---|---|
| `src/msgret.c` | `INC/MSGSERV.ASM` (`_MSGRET.ASM` glue) | the **message retriever**: `sysloadmsg`, `sysdispmsg`, `sysgetmsg` - classes (utility / extended error / parse error), `%1`-`%9` substitution by sublist id, the `%0` " - parameter" rule, char/ASCIIZ/decimal/hex/date/time, widths, alignment, pad (a `,` pad adds thousands separators), automatic CR LF for classes 1/2, output through the handle (redirectable) or AH=02h, the input functions |
| `src/parse.c` | `INC/PARSE.ASM`, `INC/PSDATA.INC` | **SysParse** - `parse(&in, &out)` with the original control blocks (PARMS, PARMSX, CONTROL, RESULT, value lists), return codes and quirks (operand packing, "=" compression, `/` rules, CAPS through INT 21h AH=65h, comma = missing positional, date/time/number/drive/filespec/quoted/complex); the per-program assembly switches through `_mslib_parse_features` |
| `src/mapper.c` | `MAPPER/*.ASM` | the OS/2-style calls FDISK uses: `DOSBEEP DOSEXIT KBDCHARIN KBDFLUSHBUFFER VIOSCROLLUP VIOSETCURPOS VIOWRTCHARSTRATT` |
| `src/msc.c` | MS C 5.1 run-time | `DOS_TopOfMemory` (PSP:0002 at load), `sys_errlist`/`sys_nerr` with MS C's texts |
| `tools/msgtab.py` | `TOOLS/BUILDMSG.EXE` (binary only) | resolves a utility's `.SKL` against `msg/USA-MS.MSG` into the C message table; `--common` makes the EXTEND/PARSE tables COMMAND.COM normally serves |
| `msg/USA-MS.MSG` | - | Microsoft's US message file, vendored unchanged |

Layouts on ARM: every pointer is a flat 32-bit address (ARCH.md 5); the
11-byte sublist keeps its size with a 4-byte value field; the parser reads the
programs' control blocks with ARM natural alignment and 32-bit pointer fields
(`src/parse.c` has the exact table), value lists as packed bytes.
`include/mslib_msg.h` and `include/mslib.h` declare it all.

`mslib.mk` builds `build/obj/mslib/libmslib.a` and provides
`$(call mslib_exe,NAME,DIR,SOURCES,CFLAGS,SKL,MSGTAB-OPTIONS,ELF2EXE-OPTIONS,EXTRA-OBJECTS)`
plus `MSLIB_MSC_CFLAGS` (gnu89, `far`/`near` empty, signed char, the
warnings 1987 C code produces switched off).

## Testing against the real MS-DOS 4.00

* `tests/harness.mjs` - boots ARM-DOS headless with the kernel's test shell
  running a script (hard disk, or a 1.44 MB diskette plus any hard disk
  image), and returns the disk afterwards, the exit codes and the screen.
* `tools/dos400run.sh` - runs a batch file under the **real** MS-DOS 4.00
  (the PCjs disks) in DOSBox-X and collects its redirected output;
  `tools/scrdump.asm` (SCRDUMP.COM, 8086) saves the text screen for what goes
  to stderr; with `KEYS=` it types into DOSBox-X on an Xvfb display and keeps
  a decoded screen per second (`tools/uniqscreens.py`). Every
  `apps/*/tests/expected/` file was made with it.

## Licence

`msg/USA-MS.MSG` and the behaviour re-implemented here are Microsoft's
MS-DOS 4.0, MIT License (`LICENSE`: Copyright (c) IBM and Microsoft
Corporation). Portions (C) Microsoft Corp., MIT License.
