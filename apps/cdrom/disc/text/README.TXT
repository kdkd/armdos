╔══════════════════════════════════════════════════════════════════════════╗
║              ARM-DOS MULTIMEDIA SAMPLER '93  -  README.TXT               ║
║                  (C) 1993 Europa Micro Systems Inc.                      ║
╚══════════════════════════════════════════════════════════════════════════╝

Welcome to the ARM-DOS Multimedia Sampler '93!

This compact disc is a MIXED-MODE disc: track 1 holds the programs and
files you can see with DIR, and tracks 2 to @LASTTRACK@ are ordinary CD audio
(Red Book) tracks you can play with CDPLAY or on any audio CD player.

GETTING STARTED
───────────────

  1. Make sure the CD-ROM driver and the CD-ROM extensions are loaded.
     Your CONFIG.SYS needs a line like

        DEVICE=C:\DOS\ARMCD.SYS /D:ARMCD001

     and your AUTOEXEC.BAT a line like

        C:\DOS\ARMCDEX /D:ARMCD001

  2. Change to the CD-ROM drive (usually D:) and start the menu:

        C:\>D:
        D:\>MENU

  3. Type the number of the item you want and press Enter.

WHAT'S ON THE DISC
──────────────────

  README.TXT     this file              CREDITS.TXT   who made what
  MENU.BAT       the main menu          COPYRGHT.TXT  copyright notice
  1.BAT - 8.BAT  the menu items         ABSTRACT.TXT  about this disc

  DEMO\          "ARM-DOS 4.00: the demo" - VGA plasma, tunnel, copper bars
  ZORK\          ZORK I, II and III (Infocom, 1980-82) with the MojoZork
                 Z-machine interpreter
  ADVENT\        Colossal Cave Adventure (Crowther and Woods)
  ANSI\          ANSI art - TYPE it with ANSI.SYS loaded
  BASIC\         BASIC programs for C:\DOS\BASIC.EXE
  UTILS\         ARMINFO, the system information program
  PICTURES\      public-domain NASA pictures (GIF, 320x200, 256 colours)
  TEXTS\         public-domain books to read with MORE
  MUSIC\         TRACKS.TXT - the audio tracks on this disc

CD AUDIO
────────

@TRACKLIST@
  Type CDPLAY to play them with the ARM-DOS CD player. CDPLAY /R leaves a
  small player in memory: press Ctrl+Alt+C to pop it up over any text-mode
  program - change tracks while you play ZORK!

NOTES
─────

  * A CD-ROM is read-only: programs run from it cannot save their games or
    settings on it. Copy a program to your hard disk if you want to keep
    saved games, e.g.  MD C:\ZORK  and  COPY D:\ZORK\*.* C:\ZORK
  * The pictures are GIF files (GIF89a). Use your favourite GIF viewer.
  * If DIR D: says "Invalid drive specification", the CD-ROM extensions are
    not loaded (see step 1) or LASTDRIVE in CONFIG.SYS is too low.

Enjoy the disc!
