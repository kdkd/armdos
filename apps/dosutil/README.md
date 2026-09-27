# dosutil — shared code of the DOS 4.00 external commands

Not a program: the helpers compiled into FIND, SORT, MORE, TREE, COMP, XCOPY,
LABEL, REPLACE and PRINT (each app has a `u4lib.c` that `#include`s these
files, so every program is self-contained and builds its own objects), and
the shared test harness.

| file | what |
|---|---|
| `u4.h`, `u4.c` | INT 21h helpers through DOS handles; the 4.0 *message retriever* conventions (class 1 extended-error texts, class 2 parse-error texts, `" - "` + parameter, `%1`..`%9`); a C re-implementation of the common command-line parser **SysParse** (`INC/PARSE.ASM`): delimiters (blank, tab, comma, the caller's extra ones), end of line (CR, LF, NUL), `/` ending an operand, switches with `:` values, keywords, positionals, the type checks in PARSE.ASM's order (date, number with ranges, drive, file spec with its special characters `[]|<>+=;"`, quoted string with `""`, simple string), file-table capitalisation, and the same return codes (1 too many parameters ... 9 parameter format not correct) |
| `u4crt.c` | a minimal start-up that replaces the SDK's (`__armdos_start`): record the PSP, shrink the memory block to image + stack (+ `u4_heap_bytes` if a program defines it) as a 1988 program would, call `main()`, exit with its value. No argv/environ/malloc/stdio, which keeps the programs close to the size of the 8086 originals (FIND.EXE 11 KB, MORE.COM 2 KB). |
| `u4int.S` | `svc` stubs for INT 10h/16h/17h/25h/26h/28h/2Fh (the SDK's generic `_armdos_intr` would add a 2 KB table) |
| `tests/harness.mjs` | boots ARM-DOS headless with the kernel's test shell (interactive `T>`), types commands like a user, reads the screen and files from the live disk image. The disk carries the utilities in `\DOS`, `\WORK` with the files of the real-DOS reference disk, label `ARMDOS`, serial `4069-12FF`. |
| `tests/cls.c`, `tests/redir.c` | `\T\CLS.EXE` (the test shell has no CLS) and `\T\REDIR.EXE in out PROG args` (runs a program with stdin and stdout redirected) — built as `U4CLS`/`U4REDIR` in `build/u4test/` |

Reference outputs in the apps' `tests/expected/` come from the genuine MS-DOS
4.00 binaries run in DOSBox-X (apps/mslib/tools/dos400run.sh, same disk layout).

Licence: written for ARM-DOS from the behaviour of Microsoft's MS-DOS 4.0
source (MIT licence) and the real binaries; no Microsoft code is copied.
