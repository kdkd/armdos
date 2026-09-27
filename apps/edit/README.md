# EDIT.EXE — the ARM-DOS Editor

    EDIT [[drive:][path]filename] [/B] [/G] [/H] [/NOHI]

A full-screen text editor in the style of the MS-DOS 5 Editor, built on
**Turbo Vision** (magiblot's modern C++ port of Borland's Turbo Vision 2.0)
with an ARM-DOS platform layer. `build/EDIT.EXE` goes to `C:\DOS` (the
catch-all rule) and `C:\DOS\EDIT.HLP` comes from `hd.json`.

* One document window filling the screen: `┌── README.TXT ──┐`, the vertical
  scroll bar on the right edge, the horizontal one on the bottom row.
* Menu bar `File  Edit  Search  Options` ... `Help` (black on grey, access
  letters bright white), drop-down menus with shadows; the status line shows
  the highlighted command's description (`F1=Help │ Loads new file into
  memory`).
* Status line `ARM-DOS Editor  <F1=Help> Press ALT to choose commands` with
  `│ 00001:001` (line:column) on the right.
* **File**: New, Open..., Save, Save As..., Print..., Exit. **Edit**: Cut
  (Shift+Del), Copy (Ctrl+Ins), Paste (Shift+Ins), Clear (Del). **Search**:
  Find..., Repeat Last Find (F3), Change... **Options**: Display... (colours
  of normal and highlighted text, scroll bars, tab stops), Help Path...
  **Help**: Getting Started, Keyboard, About...
* EDIT-style dialogs: single-line frame with the title in it, fields in
  boxes, `[X]` check boxes, `(•)` option buttons, a separator line and
  `< OK >  < Cancel >  < Help >` buttons (the default one's brackets lit).
  Open: File Name (`*.TXT`), the current directory, Files and Dirs/Drives
  (`..`, subdirectories, `[-A-]`, `[-C-]`). Change: `< Find and Verify >`,
  `< Change All >`, then "Change complete". "Loaded file is not saved. Save it
  now?" before New / Open / Exit. "Match not found".
* Start-up without a file: "Welcome to the ARM-DOS Editor" with `< Press
  ENTER to see the Survival Guide >` / `< Press ESC to clear this dialog box >`.
* Help (`EDIT.HLP`): a help window above the document, cross references
  `◄Keyboard►` (Tab / Shift+Tab / first letter, Enter, double-click), Alt+F1
  back, Ctrl+F1 next topic, F6 switches windows, Esc closes. F1 in a menu or
  dialog shows the topic for it (in a dialog, on top of it). Shift+F1: using
  Help.
* Keys: the cursor, selection (Shift+movement), editing and WordStar keys of
  EDIT (Ctrl+S/D/E/X/A/F/R/C, Ctrl+W/Z scroll, Ctrl+Y delete line, Ctrl+T,
  Ctrl+Q,Y, Ctrl+Q,F find, Ctrl+Q,A change, Ctrl+L repeat find, Ctrl+P then a
  control key inserts it, Ins toggles overwrite (block cursor)). New lines
  are indented like the line above. Pressing and releasing **Alt** activates
  the menu bar (and F10).
* The mouse (MOUSE.COM): menus, dialog controls, positioning and dragging in
  the text, scroll bars, double-click on a word or a help cross reference.
* Files: loaded byte for byte (code page 437; a Ctrl+Z ends the text), line
  breaks normalised to CR LF on loading (LF-only and CR-only files are
  converted), so everything is saved with CR LF. `EDIT NEW.TXT` for a file
  that does not exist starts an empty file of that name. No `.BAK` files.
* Printing: the document or the selected text to `PRN`, then a form feed
  (the web page prints it on its dot-matrix printer).
* `/H`: 50 lines (8x8 font, INT 10h AX=1112h); the 25-line mode and the DOS
  screen come back at exit. `/B` monochrome, `/NOHI` no bright access letters,
  `/G` accepted, `/?` usage.
* Critical errors (a floppy drive without a disk) become a message box
  ("Device not ready: A:\X.TXT") instead of "Abort, Retry, Fail?" over the
  editor; Ctrl+C / Ctrl+Break do not stop it. The screen DOS showed is saved
  and restored at exit.

## Source and licence

* `tvision/` — Turbo Vision, magiblot's port, https://github.com/magiblot/tvision
  (commit b4831e2, 2026-09-18), **MIT licence** plus Borland's original
  disclaimer: see `tvision/COPYRIGHT`. The library sources are there as
  published (`include/`, `source/tvision/`, and `source/platform/colors.cpp`,
  `strings.cpp`; the Unix/Windows platform code and the 16-bit assembler are
  left out). Our changes to it are marked `__ARMDOS__`:
  * `tv.h`: ARM-DOS is not `_TV_UNIX` (DOS paths, drive letters, the Drives
    entry of directory lists); `ttypes.h`: `thread_local` is empty (no TLS);
    `util.h`, `strings.cpp`: newlib already has `strupr`/`itoa`.
  * `tfiledtr.cpp`: loading and saving with DOS handles instead of
    iostreams (which would add ~200 KB); line breaks normalised to CR LF on
    loading; a failed Save As keeps the old name. `tobjstrm.cpp`: the (never
    used) object streams write to a null buffer instead of a `streambuf`, and
    report errors without `cerr`. `tapplica.cpp`, `tindictr.cpp`: no iostreams.
  * `tdircoll.cpp`: `driveValid()` asks DOS (IOCTL AX=4408h). `tfillist.cpp`:
    `fexpand()` upper-cases, as on DOS. `teditor1.cpp`: the tab width is a
    variable (Options / Display / Tab Stops). `tbutton.cpp`, `dialogs.h`:
    one-row buttons are clickable, `drawState()` is virtual (EDIT's
    `< OK >`). `views.h`: the scroll bar characters are public.
    `tlstview.cpp`: no column dividers in lists.
* `armdos/` — the ARM-DOS platform layer (ours, MIT):
  * `hardware.cpp` — `THardwareInfo`: a shadow of the text screen, flushed
    to 0xB8000 row by row (mouse cursor hidden meanwhile); caret by INT 10h
    AH=01h/02h; keyboard by INT 16h AH=11h/10h/12h (enhanced keys, grey keys,
    Borland codes for Shift/Ctrl+Ins/Del, Alt alone); the mouse by an INT 33h
    event handler (AX=000Ch) queueing button/motion events; the BIOS tick
    count as the clock; idle in CP15 wait-for-interrupt; INT 23h/24h handlers;
    25/50 lines; screen saved and restored.
  * `ttext.cpp` — text is single-byte code page 437 (magiblot's is UTF-8).
  * `dosdir.cpp` — findfirst/findnext, fnsplit/fnmerge, getdisk/setdisk,
    getcurdir by INT 21h.
  * `cxxrt.cpp` — operator new/delete on malloc, the `std::__throw_*`
    helpers, `__cxa_pure_virtual` (no exceptions, no libstdc++ bulk).
* `edit.cpp` (application, menu bar, status line, document window,
  find/change), `dialogs.cpp`, `help.cpp`, `edit.h` — the editor (ours, MIT).
* `data/help.txt` → `tools/mkhlp.mjs` → `build/edit/EDIT.HLP` (code page 437).

C++ support was added to the SDK for this program (sdk/README.md, "C++ programs"):
`-fno-exceptions -fno-rtti`, Turbo Vision linked as an archive so only what
is used comes in (no iostreams, no locale). Static constructors run through
the SDK's `__libc_init_array`.

## Size, memory, speed

* `EDIT.EXE` 195,032 bytes (179,552 image + relocations + header); the
  program takes 215,848 bytes of conventional memory (PSP + image + bss +
  32 KB stack).
* Heap: Turbo Vision's screen buffers etc. ~67 KB at start (a screen cell
  is 24 bytes: 48 KB per full-screen buffer); plus the text (the gap buffer is
  the file size rounded up to 4 KB). The heap grows the DOS block, then
  continues in one XMS block (`_armdos_xms_kb` = 8 MB), so files larger than
  conventional memory load. With the 2000-line test file (110 KB): 249 KB
  malloc'd, the DOS block at 467 KB, 133 KB of XMS in use.
* Speed (the 2000-line file, JIT on, counted in emulated instructions; the
  machine is nominally 100 MHz): PgDn redraws the window in ~850k
  instructions (8.5 ms at 100 MHz, ~1.2 ms of host time at ~700 MIPS);
  scrolling by one line ~730k; Ctrl+End over the whole file 2.1M; loading it
  (EDIT start-up included) 760 ms emulated.

## Tests

`make edit-test` (`tests/run.mjs`, hooked to `make test`): boots ARM-DOS with
COMMAND.COM, HIMEM.SYS and (for the mouse tests) MOUSE.COM, and checks
(125 checks): the screen layout and colours, every menu and its hints, Alt
alone, Exit restoring the DOS screen; typing, tabs, Save As, the "not saved"
prompt, reloading, LF-only files saved as CR LF, File / New and Open (lists,
patterns, directories); Find (word at the cursor, case, whole word, F3,
wrapping, Match not found), Change All, Find and Verify (Change / Skip),
"Change complete"; selection, Cut / Copy / Paste / Clear, Ctrl+Y, Ctrl+P,
overwrite; the mouse on menus (Help at the right end too), dialogs, text,
scroll bars and dragging; help topics, cross references, back, F1 in menus
and dialogs, About; Display colours and scroll bars, Print (all / selection,
captured from LPT1), /H, /B, /?; a new file, drive A: without a disk,
Ctrl+Break, F6; and the size / memory / speed figures above (a test hook:
`SET EDITSTATS=1` makes EDIT write memory figures to the debug port E9h).
Screenshots of each stage: `build/edit-test/*.png`.

## Deviations from MS-DOS 5 EDIT

* Written from the behaviour of EDIT as documented and remembered; there is
  no MS-DOS 5 reference to compare with (EDIT is not part of DOS 4.00), so
  exact column positions, some message texts and the help text are ours.
* Branding: "ARM-DOS Editor", Europa Micro Systems.
* Settings (colours, scroll bars, tab stops, help path) are not saved to an
  .INI file; there is no Undo (EDIT 5 had none either; Turbo Vision's is
  switched off). Lines are not limited to 255 characters.
* Dialog boxes use Turbo Vision's controls (input lines scroll with ◄ ►
  arrows, lists scroll vertically); the Files list is three columns wide.
* Display lets you set "Normal Text" and "Highlighted Text" colours.
