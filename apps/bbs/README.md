# The ARM Pit BBS - BBS.EXE, DRAGON.EXE, build/bbs.img

A bulletin board system for ARM-DOS, running on the **second ARM-PC** behind
phone number **555-1989** (docs/MODEM.md). Original software; the look is an
homage to Mustang Software's WildCat! 2/3 (the sysop's waiting-for-call screen,
"What is your FIRST name?", the new-user questionnaire, `@X` colour codes in
display files, bracketed hotkey menus, "More [Y,n,=]?"). No WildCat! code or
text is used.

`make bbs-image` builds **build/bbs.img** (32 MB hard disk, from `bbs.json` with
disk/mkimage.mjs directly; also part of `make all`): IO.SYS, ARMDOS.SYS,
COMMAND.COM, CONFIG.SYS, an AUTOEXEC.BAT that runs `C:\BBS\BBS`, a few DOS
utilities for the sysop, and the board in `C:\BBS\` (tree from `data/`).
The visitor's normal C: does not get BBS.EXE/DRAGON.EXE (`hd.json`
excludeFromDos).

## The call

* **Waiting for call** (sysop console): board name, date/time, last caller,
  calls today / total, users, messages, files, port, the modem log (ATZ, init,
  RING, ATA, CONNECT 2400). F1 = local logon (the line is busied out with
  ATH1), F2 = answer now, Alt-X = exit to DOS.
* Modem: `ATZ`, then `ATE0V1Q0S0=0&C1&D2&B14400` (BBS.CFG `INIT=`; `&B14400` lets the card train
  at up to 14400 V.32bis - the modem picks the slower of the two ends, so 2400 callers still
  get 2400); on `RING` it answers
  itself with `ATA` (WildCat! style), waits for `CONNECT <rate>`, runs the
  session, then drops DTR (hang-up via &D2; `+++`/`ATH0` if DCD stays up) and
  re-initialises. Boot to "Waiting for call" takes ~2.7 s of emulated time, so a
  call that rings while the machine powers on is answered.
* Logon: ANSI detection (`ESC[6n`, the terminal's cursor report), the ANSI logo
  (DISPLAY\WELCOME.ANS, plain WELCOME.TXT for TTY callers), FIRST / LAST name,
  password (3 tries); unknown names: "Did you enter your name correctly?" then the
  **new-user questionnaire** (city, phone, birthdate, computer, screen lines,
  password twice, summary). Welcome screen with caller number, last caller,
  time for the call; bulletins offered.
* **Main menu**: [M]essages [F]iles [B]ulletins [D]oors [W]ho's online
  [U]serlog [C]omment to sysop [S]ettings [G]oodbye [?] (DISPLAY\MAIN.ANS;
  expert mode hides it).
* **Messages**: three areas (MSGS\*.MSG, plain text records `@@n` / From / To /
  Subj / Date / body) with pre-seeded, clearly fictional 1989 conversations
  about ARM PCs; read (next / previous / reply / again), scan, enter (line editor
  with /S /A /L), last-read pointers per user; comments to the sysop go to
  MSGS\COMMENTS.MSG.
* **Files**: areas in FILES\<area>\ with a FILES.BBS list: ARM-DOS utilities
  (TERM.EXE, ARMINFO.EXE, POPUP.COM - built by this project), games (Zork I and
  its MIT-licensed story file, the demo), text files and ANSI art (tips file,
  Hayes command sheet, the board logo, apps/ansi's ANSI.ART), uploads.
  Download and upload with ZMODEM, YMODEM or XMODEM (apps/term/lib); estimated
  time; uploads are added to FILES.BBS with the uploader's name.
* **Doors**: "Legend of the RISC Dragon" (door/dragon.c, DRAGON.EXE in
  DOORS\DRAGON\). The BBS writes **DOOR.SYS** (52-line GAP/WildCat! format) and
  **DORINFO1.DEF** into C:\BBS, keeps DTR up, closes its own hold on the port
  (IRQ hook, OUT2) and EXECs the door with the drop-file directory as argument;
  the door opens COM2 itself, as doors did, and never drops DTR on exit.
* Who's online, userlog (seeded with the board's 1989 regulars), settings (ANSI,
  screen lines, expert mode, city, password), time limit per call (BBS.CFG
  `TIMELIMIT=`, 60), 5-minute inactivity timeout, carrier-loss detection
  anywhere, Alt-H on the console hangs up on the caller.
* Goodbye: "Log off now?", DISPLAY\GOODBYE.ANS ("Thank you for calling The ARM
  Pit!"), DTR drop -> the caller's modem says NO CARRIER. CALLERS.LOG and
  STATS.DAT record the calls; the waiting screen shows the new last caller.

## The board's lore

The ARM Pit has a history now, told by its sysop in the bulletins and by its
callers in the message bases. Everyone in it is fictional: the sysop is known
only by the handle **Europa** (named after Jupiter's moon, "cold on the outside,
an ocean underneath"), an amateur astronomer and diesel-locomotive fan who
dials the time service every night and still loses two seconds a week, with a
grey co-sysop cat called **Parity** who sleeps on the modem.

* **1987**: "The Pit", on a prototype ARM/AT (engineering sample rev X2, serial
  00017, borrowed from a friend in Europa Micro Systems' Boca Raton lab), 1200
  baud, handles allowed. **02-29-88**: the 2:10 AM freight train shakes the
  house and the 10 MB disk dies ("on a day that doesn't exist, so technically
  it never happened"). **06-17-88**: reopened as The ARM Pit, real names only.
  1989: ARM-DOS 4.00, the RISC Dragon door, caller #1,000, FidoNet node
  1:1989/1 (a fictional net). **04-01-90**: the "DIGITAL LINE, 56,000 bps"
  announcement, an April Fool that the welcome screen still winks at.
* Rival and friendly boards (The 386 Fortress, The Floating Point, Byte
  Bandit's Lair), the user group **SFARM** (second Tuesday, Sal's Pizza, the
  newsletter *The Barrel Shifter*), and a cast of regulars who post, flame,
  trade and play the doors: Karen Whitfield, Dave Morgan, Bill Tran, Susan
  Oyelaran, Rick Lambert, Marcus Feld, Gordon Kessler, Lenny Szabo, Maria
  Delgado, Harold Pruitt, Doc Mantissa and The Byte Bandit (on probation).

**Bulletins** (BULLETIN\BULLET.ANS / .TXT and BULLETn.TXT): 1 rules, 2 ARM-DOS
4.00 news, 3 ZMODEM how-to, 4 other boards (with a hint at 555-0199), **5 A
history of The ARM Pit**, **6 Meet the sysop**, **7 The Wall of Fame** (the 1987
handles and the hall of records), **8 The last 10 callers** and **9 Today's news
from the doors** (both generated: a BULLETn.TXT whose first line is
`@@LASTCALLERS` or `@@DOORNEWS`), **10 SFARM**, the user group.

**Last callers**: LASTCALL.TXT (`name|city|MM-DD-YY HH:MM|bps`, newest last,
10 kept, seeded with the regulars' calls of 11-04-89); every logon adds a line;
shown by bulletin 8 and **[L]ast callers** on the main menu.

**Welcome back**: on a repeat call the welcome screen adds a note from the sysop
(a dozen rotating ones, milestones at calls 10/25/50/100/1000, one for the
guest) and "New since your last call": new messages per area from the caller's
last-read pointers.

**Message areas** (AREAS.CFG): 1 Main Board, 2 ARM/AT Hardware, 3 Programmers'
Corner (C and ARM assembly; the snippets run on this machine), 4 Games &
Adventures, 5 For Sale / Trade, 6 Sysop Announcements, 7 The Flame Pit (RISC vs
CISC), with threaded 1988-1990 conversations, "> " quoting, taglines, and a few
hints at things hidden on the machine.

**Doors** (AREAS.CFG `[DOOR]`: `number|name|dir|exe|description`): 1 Legend of the
RISC Dragon (DRAGON.EXE, here), and three more in apps/doors (shared door
library apps/doors/lib): 2 **RISC Wars** (space trading), 3 **Byte-Sized
Trivia**, 4 **Pit Poker**. Each door keeps NEWS.TXT and SCORES.TXT in its
directory; bulletin 9 shows the latest headlines and score tables of every door.

**DEFRAG**: file area 1 (ARM-DOS Utilities) carries DEFRAG11.ZIP (the ARM Disk
Optimizer, apps/defrag: DEFRAG.EXE + DEFRAG.DOC + FILE_ID.DIZ) when
build/DEFRAG11.ZIP can be built: bbs.img tries `make build/DEFRAG11.ZIP` and
takes it if it is there (optional in bbs.json, so the board builds without it);
FILES.BBS has its description and Sysop Announcements has "New in the file
area: a disk optimizer!".

The console mirrors the session through the same ANSI emulator TERM uses
(sio.c: every byte to the caller also goes to the local screen), with a
two-line sysop status bar (caller, city, security, calls, time left, online time).

## Legend of the RISC Dragon

A daily-turns door RPG in the tradition of Legend of the Red Dragon (1989); all
names, monsters and text are new: the village of Acorn Vale, the Silicon Forest
(15 fights a day, 33 monsters from Stray Pointer and Byte Weevil to the
Legal Department Wyrm), Hacker Hank's weapons, Doris's armour, the healer,
the bank, the Floating Point Inn (rumours, gems), a master to beat for each of
11 levels, player rankings, the daily news (DRAGNEWS.TXT, seeded), and the RISC
Dragon at level 12. Players are kept in DRAGON.DAT. `DRAGON /L` plays locally.

## Files

| file | what |
|---|---|
| bbs.c | main: config, waiting screen, modem, logon, new users, main menu, session |
| sio.c/.h | session I/O shared with the door: caller + local screen, @X codes, input, More, carrier/time checks |
| users.c, msgs.c, files.c, misc.c | users, message bases, file areas + transfers, bulletins/who/userlog/settings/doors |
| door/dragon.c | the door game |
| lib_*.c | include apps/term/lib (comm, scr, vt, zmodem, xmodem) |
| data/ | C:\ of the BBS machine (AUTOEXEC.BAT, CONFIG.SYS, BBS\...) |
| tools/mkans.py | generates the ANSI screens (WELCOME, GOODBYE, MAIN, BULLET) |
| bbs.json | the disk manifest for build/bbs.img |

The sysop account is "Europa Sysop", password ACORN; the seeded 1989 users have
the password PASSWORD (it is a demo).

**Guest account**: FIRST name `GUEST`, LAST name empty (just Enter), password
`GUEST` (security 10). It is seeded with the regulars, and added to an older
USERS.DAT that lacks it; the logon screen tells visitors about it. TERM's
scripts use it (apps/keen/data/GETKEEN.SCR logs on as the guest to fetch Keen
Dreams).

**Keen Dreams**: file area 2 (Games) carries `KEENDRMS.ZIP`, the unmodified
shareware Commander Keen in Keen Dreams v1.13 archive (358,438 bytes, SHA-1
`9b1bed87bc1e0b091f90703e9463377725e8d61e`, from apps/keen/data, dated
09-10-92), with a FILE_ID.DIZ-style description in FILES.BBS. Its LICENSE.DOC
lets BBS SysOps distribute the archive as long as nothing is added to or removed
from it; provenance in apps/keen/README.md.

**Port speed**: BBS.CFG `BAUD=19200` locks the serial port at 19200, as
high-speed boards did; the modem trains at whatever the caller asks for (2400
by default, up to 14400 with the caller's `AT&B14400`), and the BBS takes the
session rate (time estimates, the status line) from the `CONNECT` result.
After a file transfer the 5-minute inactivity timer restarts (a 25-minute
download at 2400 is not "idle").

## Tests

`make bbs-test` (tests/run.mjs): two ARM-PCs on one PhoneExchange with the real
modems - the visitor boots a private C: with TERM, the BBS machine boots
build/bbs.img. TERM's dialing directory dials 555-1989; the BBS answers the RING
with ATA; the caller registers as a new user, reads bulletin 1, reads messages
1 and 2, posts a message (checked in MAIN.MSG on the BBS disk), lists a file
area, downloads ARMTIPS.TXT with ZMODEM (TERM auto-starts; compared byte for
byte on the visitor's disk), downloads MODEMS.TXT with YMODEM, uploads a file
with ZMODEM (checked on the BBS disk and in FILES.BBS), opens the door (DOOR.SYS
checked), plays forest turns, returns to the BBS with the line up, logs off
("Thank you for calling The ARM Pit!", NO CARRIER); the BBS recycles and shows
the new last caller; a second call logs on with the password, uses Who's online,
Userlog and Comment to sysop, and is dropped with Alt-H (carrier loss logged);
finally the sysop logs on locally with F1. Screenshots `build/term-test/bbs-*.png`.

## Deviations / notes

* One node, one line (Who's online jokes about node 2).
* The lore is fiction: people, boards, stores, the FidoNet address and the
  user group are invented; real history (Hayes, XMODEM, ZMODEM, FidoNet,
  Infocom) only appears in passing.
* No QWK, no FidoNet mail (the node number is lore), no conferences beyond the seven message areas, no sysop
  chat (F10) - the console only mirrors and can hang up.
* The message base is plain text rather than a binary index: small, and a sysop
  could fix it with EDLIN.

## Fast lines (docs/MODEM.md)

BBS.CFG: `BAUD=115200` (so the port is never slower than a V.90 line) and `INIT=...&B56000`, so the BBS answers at whatever the caller trains at, up to 56K. "Who's online" says "You are on at <rate> bps" (the session's rate, not the port's).
