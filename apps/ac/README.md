# AC — the ARM Commander

    AC            start the Commander (C:\DOS\AC.EXE)

A two-panel file manager for ARM-DOS in the style of the 1989 **Norton
Commander 3.0**, written from scratch for ARM-DOS (no Symantec / Peter Norton
code or data; the look and keys were recreated from public screenshots and
from memory of the manual). It is called "ARM Commander" so it does not use
the Norton trademark; everything else should feel familiar to anyone who
used NC in 1989: two blue panels with double cyan frames, the path on the top
border, yellow column headers, cyan file names (directories upper case, files
lower case with the extensions aligned), the black-on-cyan cursor bar, the
mini status line, the DOS prompt under the panels and the function key bar.

## Files

| file | what |
|---|---|
| `AC.EXE` (5.7 KB) | the resident loader: runs ACMAIN, runs the commands ACMAIN asks for, starts ACMAIN again |
| `ACMAIN.EXE` | the Commander itself |
| `AC.MNU` | a sample user menu (F2), in C:\DOS |
| `AC.INI` | written by Shift-F9 / Configuration "Save" next to ACMAIN.EXE |

Like NC.EXE + NCMAIN.EXE, the Commander leaves memory whenever it runs a
program: AC.EXE (9,152 bytes including its PSP, stack and the shared state
block) is all that stays. MEM run from AC's command line reports **9,168
bytes less** than at the bare prompt (561,936 vs 571,104 with MOUSE and HIMEM
loaded); DOOM runs from a panel and the panels come back in text mode
afterwards. The state (panel modes, paths, cursor, sort, filter, options,
16-entry command history) is kept in AC.EXE's block, so the panels come back
exactly as they were. ACMAIN.EXE can also be run on its own; it then stays in
memory and runs commands with `%COMSPEC% /C`.

## What works

* **Panels**: brief (3 columns "Name"), full (Name Size Date Time, `►SUB-DIR◄`,
  `►UP--DIR◄`), info (memory, the memory a program gets, disk space, files in
  the other panel), tree (the whole drive with `├──` lines; moving in an active
  tree panel shows that directory in the other panel). Tab, Ctrl-F1/F2 panel
  off/on, Ctrl-P other panel, Ctrl-U swap, Ctrl-L info, Ctrl-O all panels off
  (the user screen with the output of the commands), Ctrl-R re-read, Ctrl-F3..F7
  sort (name, extension, time, size, unsorted), Ctrl-PgUp parent, Ctrl-PgDn /
  Enter into a directory (the cursor lands on the directory you came from),
  Ctrl-\ root, Alt-F1/F2 drive menu (a drive that is not ready says so, with
  Retry/Cancel), Alt+letter quick search, hidden files as an option.
* **Command line**: typing goes to the prompt line (the real `PROMPT`, `$P$G`
  etc.), Enter runs it, Esc clears it, Ctrl-Enter pastes the file name,
  Ctrl-[ / Ctrl-] the left/right path, Ctrl-E / Ctrl-X history, Alt-F8 the
  history list. `CD dir` and `X:` are done by AC itself (so the panel follows);
  everything else is run with AC out of memory; the panels come straight back
  when it ends (no "press any key", as NC). Enter on a .COM/.EXE/.BAT runs it.
* **F3 viewer**: text (tabs, horizontal scrolling, Col, size, percentage), F4
  hex, F7 search. **F4 editor** (and Shift-F4 for a new file): insert/overwrite,
  Enter/BS/Del joining and splitting lines, Ctrl-Y, word moves, F2 save,
  F7 search, "The file has been modified" on Esc/F10; CR LF lines, a missing
  final newline is kept.
* **F5 copy / F6 rename-move / F7 mkdir / F8 delete** with NC-style dialogs
  (grey boxes with shadows, `Copy "file" to:` / `Copy 3 files to`, red delete
  and overwrite questions with Overwrite/All/Skip/Cancel, a progress box with
  a bar for big files, Esc cancels). Directories are copied / moved / deleted
  with their contents ("The following directory is not empty"). Copies keep
  the date and time. Moves on one drive are renames.
* **Selection**: Ins, Gray + / Gray - (group by wildcard), Gray * (invert);
  selected files in yellow, "N bytes in M selected files" on the separator line.
* **F9 pull-down menus** Left / Files / Commands / Options / Right with hot
  letters in yellow and √ marks: Brief, Full, Info, Tree, On/Off, Name,
  eXtension, tiMe, Size, Unsorted, Re-read, fiLter..., Drive...; Help, User
  menu, View, Edit, Copy, Rename or move, Make directory, Delete, File
  attributes, Select/Unselect group, Invert selection, Quit; Find file,
  History, Swap panels, Panels on/off, Compare directories; Configuration...
  (hidden files, confirm delete, mini status, key bar, clock), Key bar, Mini
  status, Clock, Save setup.
* **F1** help, **F2** user menu (`AC.MNU` in the current directory, else next
  to ACMAIN.EXE; NC.MNU syntax: `K: title` then indented commands, `!.!` = the
  file under the cursor, `!` = its name without extension), **Alt-F7** find
  file (whole drive, Enter goes there), **F10** "Do you want to quit the ARM
  Commander?".
* **Key bar**: black-on-cyan labels, the Alt / Ctrl / Shift variants while the
  key is held.
* **Mouse** (INT 33h, when MOUSE.COM is loaded): click moves the cursor and
  activates a panel, double click = Enter, right click selects, the top line
  opens the menus, the key bar presses the function key, dialogs, menus and
  lists take clicks.
* Its own INT 24h (a missing floppy fails quietly and is reported) and INT 23h
  handlers; screens are composed in memory and only the changed cells are
  written to B800.

## Deviations from NC 3.0

* Name and texts: "ARM Commander", AC.EXE/ACMAIN.EXE/AC.MNU/AC.INI.
* Left out: quick View and linK panels, Commander Mail/Link, EGA lines, NCD,
  the screen saver, NC.EXT extension associations, the menu/extension file
  editors, editor block operations.
* A .COM/.EXE without redirection is EXECed by AC.EXE directly (as COMMAND
  would), everything else through `%COMSPEC% /C`; so a program gets the
  memory it would get at the prompt minus AC's 9 KB (COMMAND /C would cost
  another ~65 KB here). A multi-line user-menu entry runs as
  `C:\DOS\AC$MENU.BAT`.
* `SET` in a command changes only that command's environment (NC patched the
  master environment).
* The viewer's key bar has 4Hex / 7Search / 10Quit; the viewer reads the file
  into memory (the SDK's heap reaches into XMS, so large files are fine).
* Quick search keeps going with plain letters after the first Alt+letter.
* 80x25 only.

## Tests

`make ac-test` (`tests/run.mjs`, 95 checks, ~4 s): layout and colours of the
panels, key bar and prompt; running MEM from the command line and comparing
the largest executable program size with the bare prompt; Ctrl-O; Ins / Gray
+ - * selection; F5 of two files and of a directory tree, the overwrite
question, F6 move and rename, F7, F8 of a file / an empty / a non-empty
directory (all checked on the disk image); viewer (text, End, search, hex) and
editor round trips (save, don't save, Shift-F4 new file); menus, full / tree
modes, Ctrl-F1, Ctrl-U; quick search, Ctrl-Enter, history, Ctrl-E, find file,
sorting, user menu, drive menu; mouse click / right click / double click /
key bar; DOOM started from a panel and back; F10 and all memory free again.
Screenshots in `build/ac-test/`.
