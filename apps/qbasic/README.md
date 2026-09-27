# QB.EXE — ARM QuickBASIC

    QB [[/RUN] [drive:][path]filename] [/B] [/H] [/NOHI]

A BASIC programming environment in the style of the one DOS 5 came with in
1991 (a homage: no Microsoft name), built on Turbo Vision with the look of the
ARM-DOS Editor, and **Bywater BASIC** (the interpreter of `apps/basic`, in a
QuickBASIC-flavoured copy) linked in. `build/QB.EXE` goes to `C:\DOS`; two
sample games are in `C:\QB`.

* The screen: menu bar `File Edit View Search Run Debug Options ... Help`
  (black on grey, access letters bright, Help at the right end; Alt alone or
  Alt+letter), the program window (`Untitled` or the file name, grey on blue,
  scroll bars), the **Immediate window** below it (`├── Immediate ──┤`, two
  lines), the status line `<Shift+F1=Help> <F6=Window> <F2=Subs> <F5=Run>
  <F8=Step>` with `│ 00001:001`, or the hint of the highlighted menu item.
* Start-up: "Welcome to ARM QuickBASIC" with `< Press Enter to see the
  Survival Guide >` / `< Press ESC to clear this dialog box >`.
* **Keywords are upper-cased** when the cursor leaves a line (and when a
  file is loaded), outside strings and remarks.
* **Run**: Start (Shift+F5), Restart (stop at the first statement), Continue
  (F5: continues a stopped program, else starts it). The program's screen is
  the **output screen**; at the end "Press any key to continue" on its bottom
  line, then back to the editor. **F4** shows the output screen again.
  **Ctrl+Break** stops a running program with the next statement highlighted
  (F5 goes on). A run-time error is a dialog (`Division by zero`, `< OK >
  < Help >`) and the cursor goes to the statement; structure errors (`DO
  without LOOP`, `FOR without NEXT` ...) are found before the run starts.
* **Debug**: Step (F8), Procedure Step (F10, over SUB / FUNCTION / GOSUB),
  Toggle Breakpoint (F9, the line turns red), Clear All Breakpoints, Set Next
  Statement. Stepping works inside SUBs and FUNCTIONs; while stopped, the
  Immediate window sees and changes the program's variables.
* **Immediate window** (F6): Enter executes the line the cursor is on
  (`PRINT total`, `x = 5`, `CALL Show(3)`) with the program's variables; if
  it shows something the output screen appears with "Press any key to
  continue".
* **View / SUBs (F2)**: the main module and every SUB / FUNCTION; Enter moves
  to it. **Edit / New SUB, New FUNCTION** append `SUB name` ... `END SUB`.
* File: New, Open (`*.BAS`, directories, drives), Save, Save As (`.BAS`
  added), Print, Exit ("Loaded file is not saved. Save it now?"); Edit: Cut,
  Copy, Paste, Clear; Search: Find, Repeat Last Find (F3), Change; Options:
  Display (colours, scroll bars, tab stops), Help Path (the program
  directory). The Editor's keys, WordStar keys and mouse.
* **Help**: the help window opens at the top of the screen (the program window
  below it). F1 on a keyword (`PRINT`, `INKEY$`, `SELECT` ...) shows its topic
  (syntax, explanation, an example, "See also"), F1 in a menu or dialog its
  topic, Shift+F1 Using Help, Help / Index (generated) and Contents, Tab /
  Enter / double-click on cross references, Alt+F1 back. 122 topics
  (`data/qbhelp.txt`, ours) compiled into QB.EXE.
* `/RUN` runs the program at once; `/B` monochrome; `/H` 50 lines; `/NOHI`.

## The language

bwBASIC 3.40 runs in its Bywater dialect (structured BASIC) restricted to
GW-BASIC's keywords plus the structured ones — Bywater's many extra functions
(`COUNT`, `PI`, `MAX`, `STR` ...) are switched off so they do not take variable
names away. What works (tested): programs without line numbers, labels
(`Retry:`) for GOTO / GOSUB, `SUB` / `FUNCTION` (called with `CALL Name(a)` or
`Name a`), `EXIT SUB/FUNCTION/DO/FOR`, `DO ... LOOP` with `WHILE` / `UNTIL` at
either end, `WHILE ... WEND`, block `IF / ELSEIF / ELSE / END IF`, `SELECT CASE`
(`CASE 1, 2 TO 4, IS > 5, CASE ELSE`, strings), `CONST`, `DIM` (with `AS`
types, `SHARED` accepted), `DECLARE`/`SHARED`/`COMMON`/`STATIC` accepted and
ignored, the string functions (`LEFT$ MID$ INSTR UCASE$ LTRIM$ STRING$ SPACE$`
...), `PRINT USING`, `RANDOMIZE TIMER`, `INKEY$`, `SLEEP [n]`, files, and the
PC statements of BASIC.EXE: `SCREEN 0/1/2/13`, `COLOR`, `LOCATE`, `CLS`,
`PSET`, `LINE ... B/BF`, `CIRCLE`, `PAINT`, `DRAW`, `POINT`, `PALETTE`,
`SOUND`, `PLAY`, `BEEP`, `PEEK/POKE/DEF SEG`, `INP/OUT`. `7 \ 2` is 3; numbers
print with 7 significant digits; `DATE$` is `mm-dd-yyyy`.

The QuickBASIC spellings are translated before bwBASIC sees a line
(`bw/qbengine.inc`, `qbe_translate`): `DECLARE`, `SHARED`, `COMMON` and `STATIC`
statements become remarks, `DIM SHARED` is `DIM`, `SUB x STATIC` loses `STATIC`,
`Name args` (a SUB called without CALL, also after THEN / ELSE) becomes
`CALL Name(args)`, `CONST A = 1, B = 2` two CONSTs, `WIDTH 80, 25` is `WIDTH 80`,
`LOCATE , , 0` gets `CSRLIN` / `POS(0)` for the empty arguments, `SLEEP` alone
waits for a key.

Fixed in our copy of the interpreter (the same bugs are in `apps/basic`):
strings compare with their lengths (bwBASIC stopped at a NUL, so `INKEY$ =
CHR$(0) + "H"` was equal to `""`), also in `SELECT CASE`; a third number
argument (`LOCATE r, c, cursor`, `COLOR f, b, border`) no longer gives "Illegal
function call"; `VAL("")` is 0; division by zero and overflow are errors.

## Samples (`samples/` -> `C:\QB`, ours)

| file | what |
|---|---|
| `BANANAS.BAS` | a banana-throwing duel between two chimps on the rooftops of a random city skyline (SCREEN 13): angle and velocity, gravity, wind, explosions that take bites out of buildings, scores |
| `SERPENT.BAS` | a snake game with levels: arrow keys, numbered food, walls per level, lives, speed |

## How it works

* The program window's text is loaded into bwBASIC line by line for each run
  (`qbrun.cpp`); the number bwBASIC gives each line is mapped back to the
  editor line, so errors, breaks and steps show in the editor.
* Before every statement the engine calls the IDE's hook. To stop (a
  breakpoint, a step, Ctrl+Break, `STOP`) the environment comes back on the
  screen and runs its own event loop *right there*, inside the interpreter
  (which may be deep inside a SUB call); Continue / Step return from the hook,
  Start / New / Exit abort the run (a longjmp to the engine's top) and then do
  what was asked. END, SYSTEM and errors come back through bwBASIC's longjmp
  (`My->mark`) instead of printing and returning to a BASIC prompt.
* While a program runs Turbo Vision is suspended; the screen it restores is
  the output screen, which it saves again when it comes back. The "Press any
  key" line is taken off again afterwards.
* QB.EXE is 630 KB (Turbo Vision + bwBASIC); like TC.EXE it runs from
  extended memory (`apps/tvlib/xload.c`: a 15 KB stub in conventional memory),
  with its heap in XMS (up to 4 MB). bwBASIC is built `-O2` with the VFP
  (hardware floating point) enabled.

## Source and licences

* `qb.cpp` (application, windows, menus, keyword casing), `qbdlg.cpp`
  (dialogs; it started as a copy of `apps/edit/dialogs.cpp`), `qbrun.cpp`
  (running, stopping, stepping, breakpoints, Immediate), `qbhelp.cpp`, `qb.h`
  — ours, MIT. `data/qbhelp.txt` — ours.
* `bw/` — **Bywater BASIC 3.40, GPL-2** (`bw/COPYING`): a copy of `apps/basic`'s
  modified sources (`src/`, `bwx_dos.c`, `dosvid.c`) with the changes for QB
  under `#ifdef QBIDE` (bwx_terminate / break / END / errors back to the IDE,
  the statement hook, STOP as a break, `main` left out, the Bywater dialect,
  `dv_reset`) and the engine interface `bw/qbengine.h` + `bw/qbengine.inc`.
  QB.EXE as a whole is therefore under the GPL-2; this directory (plus
  `apps/tvlib` and `apps/edit`'s Turbo Vision, MIT) is its source.
* Turbo Vision and the ARM-DOS platform layer: `apps/edit` via `apps/tvlib`
  (see `apps/tc/README.md`).

## Tests

`make qb-test` (`tests/run.mjs`, hooked to `make test`; 96 checks, all passing): boots ARM-DOS with
COMMAND.COM, HIMEM.SYS, MOUSE.COM, QB.EXE and the samples, and checks: the
welcome dialog, the screen layout and colours, all eight menus with their
items and hints, Alt alone, About, Exit restoring the DOS screen; typing a
program in lower case (upper-cased on leaving each line), F5 and the output
screen with "Press any key to continue", F4, the Immediate window (a variable
of the program, no output screen for an assignment), Save As; a run-time error
dialog and the cursor on its statement, a structure error; F8 into and out of
a SUB, F10 over it, F9 (red line), Ctrl+Break in a loop, PRINT and assignment
in the Immediate window while stopped, F5 to the breakpoint and to the end,
Clear All Breakpoints, F2 SUBs, New SUB; help (Survival Guide above the
program, F1 on PRINT, Shift+F1, Alt+F1, the index and a cross reference, F1 in
a menu); the mouse (a menu, Start, a click on the output screen, Help at the
right, placing the cursor, the Immediate window); both games started with
F5 and played; and breaks at several points of both games followed by F5 /
Shift+F5 and a second break and restart (a regression test: after a stopped
run was restarted, the old program's label table turned `Title:` into a call,
"Syntax error"; `qbe_load_begin` now clears it). Screenshots in
`build/qb-test/`.

## Deviations

* The program is one text in one window: SUBs are not kept in separate views
  (F2 moves the cursor to them); View / Split and Debug / Trace On are dimmed.
* No syntax check while typing (errors appear when the program runs), and no
  reformatting of lines beyond keyword casing.
* bwBASIC semantics: all variables are global (a SUB sees the main program's
  variables; parameters are passed by value); no `TYPE`, `GET`/`PUT`
  graphics, `VIEW`/`WINDOW`, `ON KEY`/`ON TIMER` events; `PLAY`/`SOUND` wait
  until done; programs are saved as text only. Programs that mix numbered and
  unnumbered lines must keep the numbers ascending.
* It is slower than the real thing's compiled-to-p-code interpreter: about
  10,000-15,000 statements per second on the 100 MHz ARM.
* While stopped in graphics mode, the output screen's graphics are lost when
  the environment is shown (text-mode output is kept).
