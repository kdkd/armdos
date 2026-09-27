# Door games for The ARM Pit BBS

Three original door games for the BBS on the second ARM-PC (555-1989,
apps/bbs), next to the board's first door, Legend of the RISC Dragon
(apps/bbs/door/dragon.c). All original programs and text; the genres are an
homage to the door games of the late 1980s.

| door | EXE | dir on the BBS disk | what |
|---|---|---|---|
| 2 | RISCWARS.EXE | C:\BBS\DOORS\RISCWARS | **RISC Wars**: space trading in 120 sectors around the Barrel Nebula, ports, holds, fighters, 150 turns a day, a shared universe with the board's regulars flying bot ships ([riscwars/README.md](riscwars/README.md)) |
| 3 | TRIVIA.EXE | C:\BBS\DOORS\TRIVIA | **Byte-Sized Trivia**: 108 questions on 1970s-80s computing (and a few about the board), speed bonus, 3 games a day, hall of fame ([trivia/README.md](trivia/README.md)) |
| 4 | PITPOKER.EXE | C:\BBS\DOORS\POKER | **Pit Poker**: 9/6 Jacks or Better video poker, dealt by Parity the cat, daily stake, high rollers board ([poker/README.md](poker/README.md)) |

They live only on the BBS machine's disk (apps/bbs/bbs.json; hd.json keeps them
out of the visitor's C:\DOS). Seed data (the regulars' scores, news from
1989) is in data/<door>/ and goes into the door's directory.

## The door library (lib/)

`lib/door.h` / `door.c`: `door_init(argc, argv, status)` reads DOOR.SYS (the
52-line GAP/WildCat! format the BBS writes) or DORINFO1.DEF from the directory
named on the command line (`/L` = play locally), opens the COM port itself with
DTR kept up, starts the session I/O and the time limit; `door_exit()` flushes
and releases the port without dropping DTR. The session I/O is the BBS's own
(apps/bbs/sio.c: `@Xbf` colour codes, caller + local mirror, More prompts,
carrier/time/idle checks by longjmp to `sio_drop`), included through
lib/lib_sio.c together with the comm and screen code of apps/term/lib.

Conventions the BBS relies on: every door appends headlines to **NEWS.TXT**
(`MM-DD-YY  text`, `door_news()`) and rewrites **SCORES.TXT** (at most 8 lines)
in its directory; bulletin 9 on the BBS ("Today's news from the doors") shows
the latest of both for every door.

## Tests

`make doors-test` (tests/local.mjs): each door played locally (`DOOR /L`) on a
headless ARM-PC, one tests/<door>.mjs per door. The same doors played over the
phone line through the BBS (DOOR.SYS on COM2, back to the BBS with the line up)
are in apps/bbs/tests/doors.mjs (`make bbs-test`).
