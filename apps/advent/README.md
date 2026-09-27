# Colossal Cave Adventure (Open Adventure) for ARM-DOS

`C:\GAMES\ADVENT\ADVENT.EXE` (also `A:\ADVENT\` on the Games floppy).

## What it is

* **Open Adventure 1.22** by Eric S. Raymond: a forward port of Crowther and
  Woods' *Adventure 2.5* (1995), released with the original authors'
  permission. Source: `https://gitlab.com/esr/open-adventure` at commit
  `e6160529e70a8c7a06b9a5f190d69aa35c4c62f5` (2026-07-08, "Ready to ship 1.22").
* **Licence:** BSD-2-Clause for the code *and* the dungeon data
  (`src/COPYING`, copied to the disk as `LICENSE.TXT`; SPDX headers in every
  file, including the generated `dungeon.c`).
* Note: this is the **430-point** 2.5 version (the final one by the original
  authors), not the 350-point 1977 version most early DOS ports were based on.
  `ADVENT -o` gives the "oldstyle" 1970s feel (upper case, no prompt).

## Files

* `src/main.c init.c actions.c score.c misc.c saveresume.c advent.h` - upstream,
  unmodified except one line in `main.c`: `-r savefile` opens with `"rb"`
  (was `"r"`, i.e. text mode on DOS).
* `src/dungeon.c`, `src/dungeon.h` - **generated on the host** from
  `adventure.yaml` by upstream's `make_dungeon.py` (Python 3 + PyYAML):
  `python3 make_dungeon.py` in a copy of the clone; vendored as generated.
  Regenerate the same way if the clone is updated.
* `compat/editline/readline.h` + `dosline.c` - replace libedit: `readline()`
  prints the prompt and reads a line with `fgets` from stdin, i.e. DOS
  buffered input (Backspace, Esc, F1/F3 template keys), and redirection works;
  `add_history()` is a no-op.

Built with `-D_DEFAULT_SOURCE -DVERSION='"1.22"'`; `getopt`, `signal` and
`<sys/time.h>` come from newlib. ADVENT.EXE is 169,600 bytes (most of it the
dungeon tables). Save files (`SAVE` / `SUSPEND`, then `RESUME` in a new game,
or `ADVENT -r FILE`) are binary and portable between runs.

## Tests (headless emulator)

* Test image with `SHELL=C:\GAMES\ADVENT\ADVENT.EXE`: "Welcome to Adventure!!
  Would you like instructions?", `no`, `enter building` (well house),
  `take lamp`, `take keys`, `take food`, `inventory`; `save` to `ADV1.SAV`
  (the game then exits, as upstream); new game, `resume`, `ADV1` -> inventory
  and location restored.
* Full `hd.img` with COMMAND.COM: `CD \GAMES\ADVENT`, `ADVENT`, `n`,
  `enter building`, `quit`, `y` -> score shown, back at `C>`.
