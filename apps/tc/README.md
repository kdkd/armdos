# TC.EXE — ARM Turbo C

    TC [file[.C] | project.PRJ]

An integrated C development environment in the style of the Turbo C 2.0 /
Turbo C++ 1.0 IDE of 1990 (a homage: no Borland name or logo): the blue
Turbo Vision desktop with editor windows, the Message window, one-key
compile / make / run, context help, with **TinyCC** (apps/tcc) linked in as
the compiler. `build/TC.EXE` goes to `C:\DOS` (the catch-all rule); the
compiler's headers and library are TCC's `C:\TC\INCLUDE` and `C:\TC\LIB`.

* Menu bar `≡ File Edit Search Run Compile Debug Project Options Window
  Help`, drop-down menus with shortcuts, the status line shows the keys of
  the active window or the hint of the highlighted menu item.
* Editor windows (numbered, double frame when active, `line:col` and a `*`
  for a modified file in the bottom frame, scroll bars, zoom F5, Ctrl+F5
  size/move, F6 next, Alt+F3 close, Alt+0 list, Alt+1..9), the editing and
  WordStar keys of Turbo Vision's editor, blocks, clipboard (Edit / Show
  clipboard), Find / Replace / Search again (Ctrl+L) / Go to line / Locate
  function; **syntax highlighting** (keywords white, comments grey, strings
  and numbers cyan, preprocessor lines green; Options / Environment turns
  it off); auto indent; `.BAK` backups.
* **F9 Make** (compiles and links NAME.EXE if a source, or one of your own
  headers it included, is newer than the .EXE; saves modified edit windows
  first), **Ctrl+F9 Run** (make, then run on the user screen and come back),
  **Alt+F5** user screen, **Alt+F9** Compile to OBJ, Link EXE file, Build
  all, Run / Arguments.
* The classic **Compiling box**: Main file, Compiling (the file being read,
  headers too), Lines compiled (Total / File), Warnings, Errors, Available
  memory, then `Success`, `Warnings` or `Errors : Press any key`.
* **Message window** (cyan, at the bottom): `Compiling HELLO.C:`, `•Error
  HELLO.C 7: 'count' undeclared`, `Linker Error: undefined symbol ...`;
  moving through it shows each line highlighted in its editor ("tracking"),
  **Enter** (or a double click) goes to the line to fix it, Space views it,
  Alt+F7 / Alt+F8 previous / next error from anywhere.
* **Project** (`.PRJ`: a text file, one source / `.O` / `.A` per line):
  Open / Close project, Add / Delete item, the Project window (Enter opens
  a file, Ins adds, Del deletes); F9 makes `PROJECT.EXE` from all of them.
  `TC NAME.PRJ` starts with it.
* **Options**: Compiler (defines, warnings none / standard / all, warnings
  as errors), Make (break on errors / warnings), Linker (stack size),
  Directories (Include `C:\TC\INCLUDE`, Library `C:\TC\LIB`, Output),
  Environment (backups, auto save, highlighting, auto indent, tab size),
  Save (`TCCONFIG.TC`, read from the current directory or `C:\TC`; with
  auto save the open files are remembered too).
* **Help** (F1 anywhere: the topic of the window, menu item or dialog;
  Ctrl+F1 on the word at the cursor: `printf`, `#include`, `malloc` ...;
  Shift+F1 the index; Alt+F1 back; Tab / Enter cross references; Edit /
  Copy example): the IDE and a concise C library reference (stdio, stdlib,
  string, ctype, math, time, conio, dos, bios, io, direct, armdos.h), in
  `data/tchelp.txt` (ours), compiled into TC.EXE.
* The mouse (MOUSE.COM) everywhere: menus, windows, scroll bars, dialogs,
  the Message and Project lists, the text; a click also dismisses the
  Compiling box.
* First start (no `TCCONFIG.TC`): `C:\TC\SAMPLES\HELLO.C` is opened.
* File / DOS shell, Get info (directory, file, project, lines, warnings,
  errors, exit code of the last run, program size, available memory),
  Print (to PRN), Change dir.

The Debug menu (Inspect, Evaluate, Call stack, Watches, breakpoints) and
Run / Program reset, Go to cursor, Trace, Step are there, dimmed: there is
no integrated debugger.

## How it works

**The compiler is in-process.** `tcomp.c` compiles TinyCC's one-source build
(`apps/tcc/src/libtcc.c`, the ARM-DOS port with its MZ+AR1 writer) into TC.EXE
and drives it as libtcc: `tcc_new`, include/library paths from Options,
`tcc_add_file` for the sources then `.O` then `.A` (DOS linker order),
`tcc_output_file`; errors and warnings come through `tcc_set_error_func` and
are parsed into file / line / text for the Message window; TinyCC's `-MD`
dependency list (`gen_deps`) is kept, so Make knows the headers. The
"Lines compiled" counter watches TinyCC read its source buffers (a wrapper
around `read()`). No EXEC, no temporary files.

**The IDE lives in extended memory.** TC.EXE is 540 KB of code (Turbo Vision
+ TinyCC) plus its heap; in conventional memory it would leave nothing for the
program being developed. So `build/TC.EXE` is `apps/tvlib/xload.c`, an 11 KB
ARM-DOS loader stub, with the IDE's own EXE image appended: the stub allocates
an XMS block (HIMEM.SYS), loads and relocates the image there exactly as DOS
would, and enters it with its own PSP; the IDE's heap continues in XMS
(`_armdos_xms_kb` = 4 MB). The stub's 15 KB stay in conventional memory, and
the IDE frees its XMS block at exit (`ximage.c`). Measured in the tests: a
program run with Ctrl+F9 finds **514,192 bytes** free (INT 21h AH=48h), 16 KB
less than at the DOS prompt (530,592); the Compiling box shows the largest
free block (542K). Running a program is `spawnv` of the .EXE with Turbo Vision
suspended (the DOS screen, which is the user screen, comes back), then the
text mode is restored if the program left another, and the IDE redraws.

Without an XMS driver TC.EXE says "This program requires an extended memory
manager (HIMEM.SYS)."

## Source and licences

* `tc.cpp` (application, menus, status line, options, configuration file),
  `tcwin.cpp` (edit windows, C highlighting, Message window), `tcbuild.cpp`
  (compile / make / link / run, Compiling box), `tcdlg.cpp` (dialogs,
  project), `tchelp.cpp`, `tc.h` — ours, **MIT**.
* `tcomp.c`, `tcomp.h` — the libtcc driver; `tcomp.c` includes TinyCC's
  sources and is **LGPL-2.1** like TinyCC. TC.EXE therefore contains LGPL
  code: the source of all of it is here and in `apps/tcc/src`.
* `data/tchelp.txt` — the help text (ours), compiled in by
  `apps/tvlib/tools/mkhelp.mjs`.
* Turbo Vision (magiblot's port, MIT + Borland's original disclaimer) and its
  ARM-DOS platform layer come from `apps/edit` through `apps/tvlib` (below).

### apps/tvlib — shared by TC.EXE and QB.EXE

* `tvlib.mk` — included by `apps/tc/app.mk` and `apps/qbasic/app.mk`; uses
  EDIT's `libtv.a` and compiles EDIT's platform layer (`apps/edit/armdos/*.cpp`,
  included unchanged by `plat*.cpp`), so `make edit-test` is not affected.
* `plat.cpp` — extras: invalidate the screen after a program ran, key /
  "any key or click" waits. `edits_hl.cpp` — Turbo Vision's `edits.cpp`
  with colouring hooks in `TEditor::formatLine` (syntax highlighting, a
  marked line, per-line colours for breakpoints), linked before `libtv.a`.
* `tvhelp.cpp` — the help window (topics, `{cross references}`, examples,
  a generated index, history, modal use from dialogs); `tools/mkhelp.mjs`
  turns a help text into a C string (and checks the cross references).
* `xload.c` / `ximage.c` — the extended-memory loader (above).
* `tests/lib.mjs` — boots a test C: image for the IDE tests.

## Tests

`make tc-test` (`tests/run.mjs`, hooked to `make test`): boots ARM-DOS with
COMMAND.COM, HIMEM.SYS, MOUSE.COM, TC.EXE and TCC's C:\TC tree, and checks
(69 checks, all passing): the screen (menu bar, window frame, status line,
colours, syntax colours), every main menu, Options / Directories defaults,
About, Alt+X restoring the DOS screen, File / DOS shell and EXIT; F9 (the Compiling box with its
fields, Linking, Success, HELLO.EXE written, "up to date" the second time),
Ctrl+F9 and Alt+F5 (the output on the user screen), Get info; a compile
error (`'count' undeclared`: Errors 1, the Message window, the error line
tracked and highlighted, Enter to line 7), editing, a second error (`';'
expected`), F2 with CR LF and `.BAK`, the fixed program running, Alt+F9 to
`.O`; a two-file project with a header (`TC DEMO.PRJ`, make, run, open from
the Project window); help (F1 in the editor and in a menu, Ctrl+F1 on
`printf`, the index, cross references, Alt+F1); the mouse (menus, Make from
the menu, a click closing the box, the Help menu, placing the cursor); the
memory a program gets when run from TC; MODE13.C (mode 13h) and MANDEL.C
run from the IDE. Screenshots: `build/tc-test/*.png`.

## Deviations

* TinyCC stops a file at its first error (Turbo C listed up to 25); all
  warnings are listed. Messages are TinyCC's, prefixed `Error` / `Warning`
  in Turbo C style; there are no column positions.
* No integrated debugger (the Debug items are dimmed), no Transfer menu,
  no `.OBJ`/`.LIB` (the formats are TCC's ELF `.O` and `.A`), no memory
  models (flat 32-bit ARM).
* The Compiling box has one "Compiling" line and counts TinyCC's lines
  (headers included, as Turbo C's "Lines compiled" did).
* `TCCONFIG.TC` is a text file (`name=value`), not Turbo C's binary.
* Needs HIMEM.SYS (the IDE runs from extended memory).

The SDK's `rename()` did not work (newlib's link+unlink fallback); fixed in
`sdk/libdos/syscalls.c`, found by the `.BAK` backups.
