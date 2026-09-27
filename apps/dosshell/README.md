# DOSSHELL - the DOS 4.00 Shell for ARM-DOS

The MS-DOS 4.00 Shell in text mode (what `SHELLC /TEXT`, CGA and monochrome
users saw), re-created for ARM-DOS from the real program's screens: Start
Programs with its groups, the File System, Change Colors, the help panels,
the action bar and pulldowns, full keyboard operation and the mouse through
INT 33h (MOUSE.COM). The Shell's source is not in Microsoft's MIT release;
this is new code, written from the Shell's documentation and from screens of
the real SHELLC captured cell by cell (text and attributes) in DOSBox-X
(`tests/ref/`).

## Files (all in C:\DOS)

| file | what |
|---|---|
| `DOSSHELL.BAT` | as SELECT wrote it for a fixed-disk install: `@C:`, `@CD C:\DOS`, `@SHELLB DOSSHELL`, `@IF ERRORLEVEL 255 GOTO END`, `:COMMON`, `@BREAK=OFF`, `@SHELLC /TEXT/TRAN/COLOR/DOS/MENU/MUL/SND/MEU:SHELL.MEU/CLR:SHELL.CLR/PROMPT/MAINT/EXIT/SWAP/DATE`, `:END`, `@BREAK=ON` |
| `SHELLB.COM` | the small resident loader (see below), 5 KB file, ~8.5 KB in memory with its stack and state block |
| `SHELLC.EXE` | the Shell |
| `SHELL.MEU` | the Main Group: Command Prompt, File System, Change Colors, DOS Utilities..., Games & Fun... |
| `DOSUTIL.MEU` | DOS Utilities: the six DOS 4 items (those whose program exists on the disk) and ten more for the ARM-DOS utilities |
| `GAMES.MEU` | Games & Fun: DOOM, Zork I-III, Colossal Cave Adventure, BASIC, DEBUG, the ARM-DOS demo |
| `SHELL.HLP` | the help texts |
| `SHELL.CLR` | the chosen colour scheme |
| `SHELL.ASC` | file associations (written when you use File > Associate) |

`.MEU` files use **the real Shell's binary format** (decoded from the shipped
SHELL.MEU/DOSUTIL.MEU; `shellc/meu.c` documents it): the menus DOS 4's Shell
wrote can be read and ours read by it. `tools/mkmeu.mjs` builds them from
`data/menus.json` (`--dump FILE` shows any .MEU). An item whose program is not
built is left out (BACKUP/RESTORE today).

## How it runs: SHELLB, SHELLC and memory

On DOS 4.00, `SHELLB` stays resident (3.9 KB) and, with `/TRAN`, SHELLC leaves
memory whenever it starts a program: SHELLB feeds the program's command lines
and a `GOTO COMMON` back into the running DOSSHELL.BAT through COMMAND.COM's
INT 2Fh AEh hook, so the program runs from the resident COMMAND.COM and SHELLC
is loaded again afterwards. Measured on the real 4.00: CHKDSK started from the
Shell reported 566,432 bytes free.

ARM-DOS's COMMAND.COM has no INT 2Fh AEh hook, so SHELLB does the same job as
a small parent: it reads the SHELLC line out of the batch file it is given
(that is why it takes the batch file's name, as the real one did: "Shell
batch filename missing or incorrect"), runs SHELLC, and when SHELLC asks for a
program (through the block it finds with INT 2Fh AX=1900h - the real SHELLB's
multiplex id) SHELLC exits, SHELLB runs the lines and starts SHELLC again,
which comes back to the same screen. A line naming a .COM/.EXE is EXECed
directly (as COMMAND would); internal commands, batch files and redirection go
to `%COMSPEC% /C`. When the Shell is left, SHELLB returns 255 so the batch
file goes to `:END`. The batch file text is exactly DOS 4's.

A program started from the Shell gets all the memory but ~9 KB (SHELLB, its
environment copy and the batch context): e.g. 571,440 bytes against 580,560
at the prompt (tests). (CHKDSK started from the real 4.00 Shell reported 566,432 bytes free.) Started without SHELLB (typing `SHELLC /MENU/DOS...`,
or without `/TRAN`), SHELLC stays in memory and runs the programs itself.

## What is there

* **Start Programs**: the Main Group and sub-groups (Esc goes back), Program
  (Start, Add, Change, Delete, Copy), Group (Add, Change, Delete, Reorder),
  Exit; program startup commands with the F4 marker and the `[...]` dialogs
  (`/T /I /P /D /R /L /F`, `%1`); passwords; item help from the .MEU; up to 16
  titles per group; groups only in the Main Group; unavailable actions shown
  with `*` in place of the mnemonic, as the real Shell.
* **Command Prompt** / **Shift+F9**: the screen with ` When ready to return to
  the DOS Shell, type EXIT then press enter.` on row 0 and COMMAND.COM below.
* **File System**: drive letters, directory tree, file list; Single, Multiple
  and System file list; File (Open, Print (needs PRINT), Associate, Move, Copy,
  Delete, Rename, Change attribute, View with F9 hex/ASCII, Create directory,
  Select all, Deselect all), Options (Display options: name filter and sort;
  File options: confirm on delete/replace, select across directories; Show
  information), Arrange, Exit; Ctrl+letter selects a drive; the list cursor,
  Space to select, Enter to open; after a program started from the File
  System: ` Press Enter (<──┘) to return to File System.`
* **Change Colors**: the four schemes, live preview, saved in SHELL.CLR.
* **Help**: F1 everywhere (context help), F11/Alt+F1 index, F9 keys, PgUp/PgDn.
* **Mouse**: click to select, double-click to start/open, the action bar and
  pulldowns, the function-key labels on the bottom line (`F10=Actions`,
  `Esc=Cancel`, ...), the arrows after `More:`, clicking outside a pulldown
  closes it. Works without a mouse too.
* The clock in the title bar (with `/DATE`) runs.

## Tests

`make dosshell-test` (`tests/run.mjs`, 155 checks, ~15 s) boots ARM-DOS with
the Shell set up as SELECT left it (AUTOEXEC.BAT ends with DOSSHELL) and
compares **every cell - character and attribute - of 88 screens** with
captures of the real SHELLC in DOSBox-X (`tests/ref/`); where
a capture caught the blinking text cursor its position is checked too:

* Start Programs in all four colour schemes, its three pulldowns, Change
  Colors 1-4, DOS Utilities and its dialogs (Diskcopy, Diskcomp, Format, Set
  Date with its 8-character field), Add/Change Program, Add Group, Delete
  Item, Reorder and Copy instructions, the password prompt and the "Password
  incorrect." box, an empty group, help panels (item help, an item without
  help text, the index and its scrolling, a topic), the command prompt screen;
* the File System: drive focus, a selected file, the four pulldowns, Display
  options (sort by date and the resulting order), File options, Show
  information, System and Multiple file lists, File View (ASCII and hex),
  Copy, Move, Delete (and its per-file confirmation), Rename, Change attribute
  (both dialogs), Create directory, Rename/Delete Directory, Associate, Open,
  "Filename already exists", "File cannot be copied to itself", Associate (both
  steps), the Multiple file list under Tab, the action bar coming back after
  an action, Drive Help, the return prompt after a program; the File System in
  schemes 2, 3 and 4; the help stack (F9 Key Assignments, F1 Help on Help,
  F11 index, Esc).

The only differences allowed are the intended ones (the extra Main Group
item, the extra DOS Utilities items, the ARM-DOS COMMAND.COM banner, ARM-DOS's
help texts, and a machine that has A: and C: where the captures show B:). The
diskette used has the names, sizes and dates of the captured one
(`tests/lib.mjs makeFloppy`).

Then the behaviour: transient mode (SHELLB runs TREE from the File System and
SHELLC comes back to the same directory and file), Shift+F9 and EXIT, a
startup command with a `[...]` dialog and a PAUSE line, the memory a program
gets, F3 back to DOS and all the memory free again, a game from Games & Fun
(Zork I) and back, group maintenance written to the .MEU files in the real
format (read back with `tools/mkmeu.mjs`), file operations checked on the
disk image afterwards (copy, rename, delete, create directory, move, change
attribute), and the mouse (MOUSE.COM): selecting, double-clicking,
the bottom-line labels, the action bar and pulldowns.

## Deviations

* Text mode only; `/CO1 /CO2 /CO3` are accepted and ignored.
* No "Press any key to return to MS-DOS Shell" after programs: that is the
  DOS 5 Shell. DOS 4 returns to Start Programs at once (its startup commands
  end with PAUSE where the output must stay) and prints ` Press Enter (<──┘)
  to return to File System.` after a program started from the File System -
  both as here.
* SHELLB is a parent process instead of a TSR hooking COMMAND.COM (see above).
* SHELL.CLR and SHELL.ASC use small ARM-DOS formats (the real SHELL.CLR is a
  table of panel colour records; the colours are built in, as measured).
  SHELL.HLP is ARM-DOS's own format and text (the real help file is not in the
  MIT release): the key assignments are the documented DOS 4 ones, the other
  topics are written for this Shell with the same titles.
* The mouse pointer is the INT 33h driver's text cursor (an inverted cell).
* A few colours were not captured (the error box in schemes 2 and 3, the
  dialog selection bar outside scheme 1); they follow the captured schemes.
* Not verified against the real Shell (no capture): Print (it needs PRINT
  resident; the Shell submits through INT 2Fh AX=0101h), the Program
  Parameters dialog of a bare `[]`, Home/End in Start Programs (the real one
  ignores End; here both work).
