SECOND REALITY                                    Future Crew, 1993
=======================================================================

The demo that won the Assembly '93 demo competition (Helsinki, summer
1993) - a 386 program for DOS, VGA and a sound card - running here on the
ARM PC.  Not one instruction of it was written for this machine: ELBOW
translates its x86 code into ARM code as it runs, and the ARM PC's VGA
does the unchained "Mode X" and EGA planar graphics the demo programs
straight into the card's registers.

RUNNING IT

  From the ELBOW menu:  CD \ELBOW\APPS, then MENU 17.  Or:
      CD \ELBOW\DEMOS\SECOND
      ELBOW SECOND           (SECOND 2 ... SECOND 5 start at a later part)

  The SETUP screen comes first.  The ARM PC has a Sound Blaster 16 (which
  plays Sound Blaster Pro stereo) but no Gravis UltraSound, so press the
  Left arrow once - "Sound Blaster Pro" - and then Enter.  "No sound" works
  too (the demo still keeps time with the silent music).  Esc skips the
  part on the screen; holding Ctrl down ends the whole demo.

  With a Sound Blaster the demo needs 1 MB of expanded memory: ELBOW gives
  x86 programs 2.5 MB of LIM EMS 4.0 (the page frame at E000h).

  SPEED: translated by ELBOW, the 100 MHz ARM PC is about a 386DX-33 for
  this demo, which was made for a 486-33.  The music always plays at full
  speed; the light parts run at the VGA's 70 frames a second, the heavy
  3-D scenes at 6-15.  The parts follow cues in the music, and as on a 386
  a part that misses its cue waits for the next one on its last picture
  (a white screen for a while).  "No sound", or the 133 MHz clock jumper
  (Open the box), keeps it on time.

WHAT IS HERE

  SECOND.EXE     the demo's loader and music player (U2.EXE of the source
                 release, MAIN\U2.EXE); it finds the parts below by name
  REALITY.FC     the music (two Scream Tracker 3 modules)
  *.EXE, U2A.*, U2E.*, TROLL.UP, O2.SCI, U2END.BIN, ...
                 the parts and their data (MAIN\DATA of the release)
  FCINFO10.TXT, README.1ST   Future Crew's own texts from 1993
  UNLICENS.TXT   the licence of the source release

LICENCE

  Future Crew released the source code and data of Second Reality for its
  20th anniversary (2013, github.com/mtuomi/SecondReality) into the public
  domain under the Unlicense - UNLICENS.TXT.  These are the files of that
  release, unmodified.  The 1993 texts in FCINFO10.TXT/README.1ST predate
  it (they speak of freeware).

CREDITS (from the demo)

  Code             Psi (Sami Tammilehto), Trug (Mika Tuomi),
                   Wildfire (Arto Vuori)
  Music            Purple Motion (Jonne Valtonen), Skaven (Peter Hajba)
  Graphics         Marvel (Aki Maatta), Pixel (Mikko Iho)
  Additional design  Abyss (Jussi Laakkonen), Gore (Samuli Syvahuoko)
