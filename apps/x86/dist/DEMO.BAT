@ECHO OFF
CLS
ECHO   ELBOW - Emulated Legacy Binaries On Workstation
ECHO   "Your old programs, now with a little ELBOW grease."
ECHO.
ECHO   The ARM PC runs programs made for the IBM PC's Intel processor.
ECHO   First, FILE tells you what the programs in this directory are:
ECHO.
FILE *.COM *.EXE
ECHO.
PAUSE
CLS
ELBOW HELLO86.COM
ECHO.
ECHO   Next, a fire effect in VGA mode 13h.  Like a 1993 demo it programs
ECHO   the timer, the keyboard and the colour palette itself.
ECHO   Press any key to put the fire out.
PAUSE
ELBOW /STATS FIRE.COM
ECHO.
ECHO   Now the SAME C program, compiled once for the ARM (BENCHARM.EXE,
ECHO   gcc) and once for the 8086 (BENCH86.EXE, OpenWatcom), runs natively,
ECHO   then under ELBOW interpreted, then under ELBOW translated to ARM code.
PAUSE
IF EXIST BENCH.DAT DEL BENCH.DAT
BENCHARM ARM
ELBOW /NOJIT BENCH86 INTERP
ELBOW BENCH86 ELBOW
ECHO.
ECHO   Benchmarks are one thing.  Here is a real game from 1987 that nobody
ECHO   recompiled: Kingdom of Kroz, Scott Miller's first Apogee game (freeware
ECHO   since 2009).  Answer C, pick Novice, move with the arrow keys, and
ECHO   press Q then Y to leave.
PAUSE
CD \ELBOW\APPS\KROZ\KINGDOM
ELBOW KINGDOM
CD \ELBOW
CLS
ECHO   And Sopwith (1984, now GPL) in CGA graphics: S, N, K to start, then
ECHO   X faster, comma climbs, slash dives, space fires, B bombs.
ECHO   Break (Scroll Lock) ends the game.
PAUSE
CD \ELBOW\APPS\SOPWITH
ELBOW SOPWITH
CD \ELBOW
CLS
ECHO   More in APPS: all of Kroz, ZZT, Beyond the Titanic, Supernova, Arctic
ECHO   Adventure, Monuments of Mars, Pharaoh's Tomb...  CD APPS then MENU.
ECHO.
ECHO   In MSDOS20 there are genuine MS-DOS 2.0 tools (Microsoft's MIT
ECHO   release) - an assembler, a linker, DEBUG, EDLIN.  Try:
ECHO       CD MSDOS20
ECHO       ASM HELLO
ECHO   FREEDOS has FreeDOS's TREE, FIND, MORE and CHOICE (GPL, with sources).
ECHO   See README.TXT for more.
