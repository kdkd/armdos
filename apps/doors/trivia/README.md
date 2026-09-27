# Byte-Sized Trivia - TRIVIA.EXE (a door for The ARM Pit BBS)

A quiz door: ten multiple-choice questions a game about the computers, modems,
BBSes, games and programming languages of the 1970s and 80s, plus a few
"[ARM Pit]" questions about the board itself (the answers are in the bulletins).
Original program for ARM-DOS, built on the door library (`../lib`, see door.h).

    TRIVIA <dir with DOOR.SYS or DORINFO1.DEF>     (the BBS starts it like this)
    TRIVIA /L                                     (play locally)

* **Scoring**: 100 a right answer, a speed bonus of up to 50 (full inside 3 s,
  none after 20 s, BIOS ticks), +25 a right answer once a streak of three is
  running, +250 for a perfect game (maximum 1,950). 30 seconds a question.
* **Three games a day** per caller (a game counts from its first question, so
  hanging up doesn't reset it).
* **Hall of fame** (best single game, games, perfect games, total points).
* Answers are shuffled every time; no question repeats within a game.

## Files (in the door's directory, C:\BBS\DOORS\TRIVIA on the BBS disk)

| file | what |
|---|---|
| `QUESTION.TXT` | the question box, 108 questions. Sysops can add: one or more `Q ` lines, four answers (`* ` right, `- ` wrong), optional `! ` fact lines shown after the answer, a blank line between questions, `;` comments. Up to 300. |
| `TRIVIA.DAT` | players, one per line: `name|best|total|games|perfect|bestdate|day|today` (seeded with the board's 1989 regulars) |
| `SCORES.TXT` | title + top five + the all-time points leader, rewritten after every game (the BBS's news bulletin shows it) |
| `NEWS.TXT` | headlines: new board records, perfect games |

The seeded data is in `apps/doors/data/trivia/`.

## Tests

`node apps/doors/tests/local.mjs trivia` (part of `make doors-test`): a perfect
game (the test looks each right answer up in QUESTION.TXT), the 1,950 score,
the record in TRIVIA.DAT / SCORES.TXT / NEWS.TXT, two more games, and the
three-a-day limit. Screenshots `build/term-test/door-trivia-*.png`.
