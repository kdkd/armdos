ELBOW - Emulated Legacy Binaries On Workstation              Version 1.00
========================================================================

"Your old programs, now with a little ELBOW grease."

ELBOW is the joint that lets the ARM reach back to the x86.  It runs
programs written for the IBM PC's Intel processors (8086, 80186, 80286 and
80386 in real mode) on the ARM PC, as if they were ARM-DOS programs:

    ELBOW [/JIT | /NOJIT] [/STATS] [/MEM:n] program [parameters]

(X86 works too, as another name for ELBOW.)  The program gets an x86
processor of its own, 640 KB of x86 memory, the real screen (text at
B800h, VGA mode 13h at A000h), the keyboard, the timer, the speaker and
the other ports - and ARM-DOS itself: every file, handle, redirection and
device it uses is ARM-DOS's own.  Its x86 code is translated into ARM code
as it runs (/NOJIT interprets it instruction by instruction instead).

WHAT IS HERE

  DEMO.BAT      a guided tour (type DEMO)
  HELLO86.COM   which processor does an x86 program see?  (8086 code)
  FIRE.COM      a fire effect: VGA mode 13h, its own timer at 70 Hz, its
                own keyboard handler, the palette through ports 3C8h/3C9h
  BENCH.C       one benchmark in C ...
  BENCHARM.EXE  ... compiled for the ARM (gcc, the ARM-DOS SDK)
  BENCH86.EXE   ... and for 16-bit x86 DOS (OpenWatcom v2).  Run
                  BENCHARM ARM
                  ELBOW /NOJIT BENCH86 INTERP
                  ELBOW BENCH86 ELBOW
                and the last one prints how much slower the x86 build is
                under ELBOW than the ARM build running natively.
  MSDOS20\      genuine MS-DOS 2.0 programs: MASM, LINK, EXE2BIN, DEBUG,
                EDLIN, SORT, FIND, MORE.  ASM HELLO assembles, links and
                runs HELLO.ASM.  Try ELBOW DEBUG, then "a 100", "mov ah,2",
                "mov dl,41", "int 21", "int 20", an empty line, "g", "q".
  FREEDOS\      FreeDOS programs (GPL, sources in FREEDOS\SOURCE): TREE,
                FIND, MORE, CHOICE.
  APPS\         the gallery: real, unmodified x86 games from the rights
                holders' own freeware (or MIT/GPL) releases - the whole
                Kroz series, ZZT, Sopwith, Beyond the Titanic, Supernova,
                Word Whiz, Arctic Adventure, Monuments of Mars, Pharaoh's
                Tomb.  CD APPS, then MENU; APPS\README.TXT has where each
                came from and its licence.

  DEMOS\SECOND  Second Reality, Future Crew's 1993 demo (a 386 program:
                Mode X, EGA planar graphics, Sound Blaster music from EMS).
                MENU 17 in APPS, or DEMOS\SECOND.BAT; README.TXT there.

WHAT DOES NOT WORK

  Protected mode (DOS extenders, Windows), XMS for x86 programs, a math
  coprocessor (programs that need an 8087 say so; most check and do
  without), the Gravis UltraSound.  Text modes, CGA, EGA/VGA planar
  graphics (modes 0Dh-12h), VGA mode 13h and Mode X, 2.5 MB of LIM EMS 4.0
  expanded memory (ELBOW /NOEMS: none) and the Sound Blaster are fine.

LICENCES

  The MS-DOS 2.0 programs in MSDOS20 are Microsoft's, released under the
  MIT licence (MSDOS20\LICENSE.TXT); they print their own 1982/1983
  copyright notices.  The programs in APPS keep their own licences (see
  APPS\README.TXT).  BENCH86.EXE was built with OpenWatcom v2.  The rest
  is part of ARM-DOS, by Europa Micro Systems.
