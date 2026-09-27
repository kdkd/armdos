# RISC Wars - RISCWARS.EXE, a space trading door for The ARM Pit

An original multi-player space trading door in the tradition of the late-1980s
trading games (sectors, warp lanes, ports, cargo holds, fighters, a daily turn
limit, a shared universe). All names, text and rules are new; no code or text
from any other game.

    RISCWARS.EXE <dir with DOOR.SYS or DORINFO1.DEF>     launched by BBS.EXE
    RISCWARS /L                                          play at the console

Built from `riscwars/riscwars.c` + the door library (`apps/doors/lib`: door.c,
the BBS's sio.c, apps/term/lib's comm/scr/vt), ~89 KB. It lives on the BBS
machine's disk only (apps/doors/hd.json keeps it out of the visitor's C:\DOS).

## The game

* **120 sectors** joined by warp lanes (up to 6 per sector, a few one-way),
  generated once from a fixed seed on the first run (a few milliseconds).
  Sectors 1-10 are **Acorn Space**: protected, no fighters, no hazards, no
  combat. Sector 1 is **Acorn Station** with **The Fab** (shipyard). Sectors
  61-75 are the **Barrel Nebula** (long-range scanners see snow).
* **40 ports**, each buying or selling **Silicon**, **Firmware** and **Coffee**
  (class `SBB` = sells Silicon, buys Firmware and Coffee). Prices follow stock:
  a seller charges 70-120% of the base price (cheaper the fuller it is), a buyer
  pays 90-140% (more the more it still wants). Ports restock a third of the way
  back every day. Sector 3 sells Silicon and sector 5 buys it, for beginners.
* **150 turns a day**; a warp or a docking costs one. Type a sector number at the
  prompt to warp; a sector that isn't adjacent gets a plotted course and the
  **autopilot** (also `C`, the course plotter, a breadth-first search).
* **The Fab**: cargo holds (200 + 10 x holds credits each, up to 150),
  fighters (60 each), a long-range scanner (3,000), ship renaming.
* **Fighters** left in a sector (`F`) claim it: anybody else entering must
  attack them or retreat; losing your last fighter to a swarm loses the ship
  (cargo and fighters gone, escape pod home, 20 turns). `A` attacks another
  ship in the sector and salvages 10% of its credits.
* **Hazards** outside Acorn Space (1 warp in 14): Rogue Interrupt swarms,
  drifting cargo pods, line noise storms, Byte Weevils in the Coffee,
  passing traders with rumours.
* **The regulars** (Karen Whitfield, Gordon Kessler, Susan Oyelaran, Maria
  Delgado, Bill Tran, Lenny Szabo, Dave Morgan, Marcus Feld, The Byte Bandit,
  Rick Lambert) fly their own ships. When the first caller of a new day opens
  the door, each regular takes a day (at most 5 missed days are caught up):
  trade runs, buying holds and fighters, claiming or raiding sectors, and the
  pirates (Gordon, the Bandit) hunt parked ships - callers' too. A caller whose
  ship or fighters were hit reads "While you were away: ..." next time. A caller
  whose name matches a regular takes over that ship.
* Commands: number = warp, `M` move, `P` port, `D` display, `C` course plotter,
  `F` deploy fighters, `A` attack, `S` scan, `B` the Fab, `I` your ship,
  `R` rankings (net worth: credits + holds + fighters + cargo), `N` news, `?`, `Q`.
* Easter eggs: warping to sector 1989, and the derelict in sector 117.

## Files (in the door's directory, C:\BBS\DOORS\RISCWARS on the BBS disk)

| file | what |
|---|---|
| `UNIVERSE.DAT` | sectors, warps, ports and stock, deployed fighters, the last day played (created on the first run) |
| `PLAYERS.DAT` | every trader, 192-byte records (created on the first run with the regulars) |
| `NEWS.TXT` | headlines, `MM-DD-YY  text` (seeded with Oct/Nov 1989 history; the BBS's news bulletin shows the tail) |
| `SCORES.TXT` | top 5 by net worth, rewritten whenever state is saved (seeded with the first-run state) |

Delete UNIVERSE.DAT and PLAYERS.DAT to start a new universe (the "Big Bang").

## Tests

`apps/doors/tests/riscwars.mjs` (in `make doors-test`): plays `RISCWARS /L`:
sign-on, warps, buying at sector 3, autopilot to sector 5 and selling, the Fab
(fighters, holds), course plotter to sector 11, deploying fighters, ship info,
rankings, news, the 1989 easter egg; checks SCORES.TXT, NEWS.TXT, PLAYERS.DAT,
UNIVERSE.DAT on disk; a second run the same day keeps sector and turns; after
`DATE 11-05-89` the regulars move (new dated headlines) and turns are reset.
Screenshots `build/term-test/door-riscwars-*.png`.
