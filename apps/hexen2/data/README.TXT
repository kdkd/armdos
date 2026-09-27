===============================================================================
                  HEXEN II  for ARM-DOS            H2.EXE
              "Hammer of Thyrion" engine, VGA 320x200, Sound Blaster
===============================================================================

WHAT IS HEXEN II?
-----------------

Hexen II is Raven Software's 1997 fantasy action game, built on id
Software's Quake technology and published by id Software and Activision.
You pick one of four heroes - the Paladin, the Crusader, the Necromancer
or the Assassin - and fight through castles, ruins and swamps against
the Serpent Rider Eidolon.  Unlike Quake, heroes gain experience and
levels, carry an inventory of artifacts, and the worlds are hubs of
connected maps full of puzzles.

H2.EXE in this directory is the Hexen II program: the "engine" that
draws the 3-D world, plays the sounds and runs the game rules.  Raven
released its source code under the GNU General Public License, and
the Hammer of Thyrion (uHexen2) project has kept it up ever since.
This copy was compiled for the ARM PC's processor from that source.

WHY IS THE GAME NOT HERE?
-------------------------

The program is free; the game's DATA is not.  Everything you see and
hear - maps, monsters, textures, sounds - lives in one big file,
PAK0.PAK, that belongs to Raven and Activision.  Even the free demo
comes with a licence ("HEXEN II SUBLICENSE.doc") that lets you install
and use one copy for yourself but does not let anybody else pass it on.
So ARM-DOS cannot include it.

You can fetch the demo yourself, free, and give H2.EXE its PAK0.PAK.
It takes a few minutes.  Here is how.

WHAT YOU NEED
-------------

The Hexen II demo, version 1.11, of November 1997.  Its official file
is a self-extracting archive for Windows 95:

    H2DEMO.EXE     15,334,912 bytes
                   SHA-1  dcb5ab7c92352a8af4975151bd089354723d9417

One place it can still be found is the gamers.org mirror of the
official releases:

    https://www.gamers.org/pub/games/hexen2/official/demo/h2demo.exe

Inside it is the only file you need:

    Install\Hexen2\data1\pak0.pak
                   27,750,257 bytes
                   SHA-1  a8cfd8790819c47998141adb97c14d1680cf8738
                   CRC-32 BB755DBC

The older demo of August 1997 (H2DEMO.EXE of 12,244,480 bytes, "v0.42")
will NOT do: its PAK0.PAK is 23,537,707 bytes and H2.EXE refuses it.

H2CHECK also knows the full game's PAK0.PAK and PAK1.PAK (version 1.11),
but H2.EXE has only been tried with the demo: the full game's bigger
maps may not fit in the ARM PC's 16 megabytes.

STEP 1 - TAKE PAK0.PAK OUT OF THE DEMO, ON YOUR OWN COMPUTER
-----------------------------------------------------------

H2DEMO.EXE is a WinZip self-extractor: a ZIP archive with a small
Windows program in front.  You do not have to run it (its installer is
from 1997 and is not needed) - open it as an archive instead:

  Windows:        Install 7-Zip (www.7-zip.org, free).  Right-click
                  h2demo.exe, choose 7-Zip, then "Open archive".  Go
                  into Install, Hexen2, data1 and drag pak0.pak out to
                  your desktop.
  Mac, Linux:     In a terminal:
                      unzip h2demo.exe Install/Hexen2/data1/pak0.pak
                  pak0.pak is then in Install/Hexen2/data1/ .
                  (The Unarchiver and other ZIP programs work too.)

The other files in the archive (h2.exe, progs.dat, hexen.rc, ...) are
not needed: ARM-DOS has its own H2.EXE, PROGS.DAT and HEXEN.RC.

STEP 2 - BRING PAK0.PAK INTO THE ARM PC
---------------------------------------

Not on a diskette: at 27 megabytes PAK0.PAK would need nineteen 1.44 MB
diskettes, and one file cannot be split across them.  There are two
good ways instead.

  WAY A - the Files panel (quickest: a few seconds)

    1. Switch the ARM PC OFF with its power switch.  (Drive C: only
       takes files while the machine is off.)
    2. Under the disk box, in the Files panel, click the "C:" tab.
    3. Click GAMES, then HEXEN2, then DATA1.  The path shown must be
       C:\GAMES\HEXEN2\DATA1 .
    4. Click "Copy files in..." and choose pak0.pak - or drag pak0.pak
       from your desktop onto the list.  It becomes PAK0.PAK.
    5. Switch the ARM PC on again.

    Drive C: is kept in your web browser, so PAK0.PAK stays there until
    you press "Reset C: to factory".

  WAY B - the Host Link, by modem (slow: about six hours)

    The ARM PC's modem can call the Host Link at 555-0100, which sends
    files from your computer by ZMODEM.  GETPAK.BAT does it all:

        C:\>CD \GAMES\HEXEN2
        C:\GAMES\HEXEN2>GETPAK

    TERM dials the Host Link at 14,400 bps and asks it for a file; when
    your browser opens its file window, choose pak0.pak.  The download
    goes straight into C:\GAMES\HEXEN2\DATA1 .  27 megabytes at 1,300
    characters a second take about six hours - leave the page open.
    If the call breaks off, run GETPAK again: TERM continues where it
    stopped (ZMODEM crash recovery).

    By hand: in TERM, Alt-S (setup) and set the download directory to
    C:\GAMES\HEXEN2\DATA1 and the modem init string to AT&C1&D2&B14400
    (baud rate 19200); Alt-D, dial the ARM-DOS Host Link, press D and
    pick pak0.pak.  TERM starts receiving by itself.

STEP 3 - CHECK IT AND PLAY
--------------------------

        C:\>CD \GAMES\HEXEN2
        C:\GAMES\HEXEN2>INSTALL

INSTALL checks PAK0.PAK (size, directory and a CRC-32 of the whole
file, which takes a few seconds) and tells you if anything is wrong.
Then type

        HEXEN2          (from any directory)
    or  H2              (in C:\GAMES\HEXEN2)

In the main menu choose Single Player, New Game, a class and a
difficulty.  The demo has the first three levels of the Blackmarsh hub
- Blackmarsh, Barbican and The Mill - and a deathmatch map, the Atrium
of Immolation (in the console: map ravdm1).

PLAYING
-------

  Arrow keys   walk and turn           Ctrl       attack (mouse button 1)
  Alt          strafe (with arrows)    Shift      run
  /            jump                    Space      crouch
  , .          step left / right       A / Z      look up / down
  1 - 4        weapons                 [ ] Enter  inventory: choose, use
  Tab          show your status        Esc        menu
  F2 / F3      save / load             F6 / F9    quick save / load
  ~ (left of 1)  console               F10        quit

With the mouse driver loaded (MOUSE) the mouse turns you; in the
console type +mlook to look up and down with it.  Esc, Options changes
keys, mouse speed, sound volume and screen size.

Command line switches:

  -nosound      no sound           -sndspeed 22050   better sound, slower
  -nomouse      ignore the mouse   -mem N            use N megabytes

H2.EXE needs HIMEM.SYS (DEVICE=C:\DOS\HIMEM.SYS in CONFIG.SYS) and
about 15 megabytes of extended memory: all of the ARM PC's 16 MB.  It
draws 320x200 in 256 colours (VGA mode 13h) and plays sound on the
Sound Blaster 16 (SET BLASTER=A220 I7 D1 H5 T6).  No music: the demo's
music is for CD-audio and MIDI synthesizers, neither of which H2.EXE
plays.

Saved games and CONFIG.CFG are written to C:\GAMES\HEXEN2\DATA1 .

LICENCE AND CREDITS
-------------------

Hexen II (c) 1997 Raven Software Corp.  Hexen II is a trademark of
Raven Software; Hexen II was published by id Software and distributed
by Activision.  Quake engine (c) 1996-1997 id Software.

H2.EXE is Hammer of Thyrion 1.5.10 (uHexen2, uhexen2.sourceforge.net),
O. Sezer and the uHexen2 contributors, from Raven's and id's source,
with an ARM-DOS system driver.  PROGS.DAT is the Hexen II game code
compiled from Raven's HexenC source as uHexen2 keeps it; HEXEN.RC is
uHexen2's.  All of them are free software under the GNU General Public
License version 2; the source code is part of the ARM-DOS project
(apps/hexen2).  This port is not made or supported by Raven Software,
id Software or Activision.

PAK0.PAK is not part of ARM-DOS and is not covered by the GPL: it is
yours under Activision's licence for the demo.
