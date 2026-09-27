                                 ARM-DOS 1.0
                                 ===========

  Copyright (C) 2026 Kevin Day, with lots of help from Europa.


WHAT THIS MACHINE IS
--------------------

  It is 1988 and you are sitting at an IBM PC/AT compatible. The beige box,
  the POST count-up, the C:\> prompt, DOS 4's Shell: all where you left
  them. The processor is the odd part. Instead of an Intel 8086 family
  chip, this PC has an ARM926EJ-S, a 32-bit RISC CPU from Cambridge.

  ARM-DOS answers the question "what would IBM and Microsoft have done if
  the PC had shipped with an ARM?" Every PC interface that makes sense is
  kept: the I/O ports, the BIOS data area, the text screen at B800h, INT 21h,
  PSPs, memory control blocks, FAT disks and "MZ" EXE files. Only what the
  CPU forces to change has changed. VER calls the system ARM-DOS Version
  4.00, because it follows MS-DOS 4.00 command for command; this disk is
  release 1.0 of the whole machine.

  Every program you run is ARM machine code: the BIOS, the DOS kernel,
  COMMAND.COM, the utilities, the games. The one exception is x86 software.
  ELBOW (C:\ELBOW) runs genuine IBM PC programs by translating their Intel
  code into ARM code as they run, and the X86 lamp on the front panel
  glows while it does.

  The whole computer is emulated in your web browser. Nothing you do here
  can hurt it; if you break something, switch it off and on again.


WHERE TO START
--------------

  HELP                      the "things to try" sheet (C:\DOS\TRYME.TXT)
  DOSSHELL                  the DOS 4.00 Shell, with menus and a mouse
  DIR C:\ /W                what is on the disk
  TYPE D:\README.TXT        drive D:, the one that is yours to keep
  TYPE C:\CREDITS.TXT | MORE    who made what, and under which licence

  A quick tour of C:

    \DOS        ARM-DOS 4.00 itself: the DOS 4 commands (CHKDSK, FORMAT,
                XCOPY, MODE, KEYB, EDLIN, DEBUG ...), plus EDIT, BASIC, QB,
                TC, ARMINFO, AC (the ARM Commander), DOCTOR and more
    \GAMES      DOOM, Wolfenstein 3D, Quake, Duke Nukem 3D, Zork I-III,
                Colossal Cave, Keen Dreams (by modem) and Hexen II (bring
                your own data)
    \GEMAPPS    Digital Research's GEM desktop, compiled for the ARM
    \TC         ARM Turbo C: compiler, headers, libraries and samples
    \QB         ARM QuickBASIC samples
    \BASIC      demo programs for BASIC
    \MIDI       classical pieces for the General MIDI synthesiser
    \DEMO       a 1990s-style demo and a Hercules graphics demo
    \ONLINE     the ARM-DOS Online client
    \TERM       the terminal program
    \PAINT      ARM Paint and its sample pictures
    \ELBOW      x86 programs: MS-DOS 2.0 tools, FreeDOS tools, a gallery
                of freeware games, office programs and Second Reality

  HELP has the details, so they are not repeated here.


THE HARDWARE
------------

  CPU          ARM926EJ-S (ARMv5TE) with a VFP9-S floating-point unit.
               100 MHz with TURBO on, 12 MHz with it off.
  Memory       640 KB conventional, the rest of the 16 MB of SIMMs as
               extended memory through HIMEM.SYS (1 to 16 MB, set inside
               the case)
  Video        VGA with 256 KB (text, CGA, EGA/VGA planar, mode 13h, Mode X,
               plus its own 640x480 256-colour mode 62h), or a Hercules card
               on a green, amber or paper-white monochrome monitor
  Sound        Sound Blaster 16 (DSP 4.05, 8/16-bit DMA, CT1745 mixer) at
               220h, IRQ 7, DMA 1/5, with a YMF262 OPL3 FM chip; an MPU-401
               at 330h drives a General MIDI wavetable daughterboard. The PC
               speaker works too. An AdLib-only card can be fitted instead.
  Disks        two 128 MB hard disks: C: (ARM-DOS and its programs) and
               D: (yours; see below), a 1.44 MB floppy drive (A:) and an
               ATAPI CD-ROM drive (E:, through ARMCDEX)
  Modem        Hayes compatible on COM2: 2400, 14,400, 33,600 or 56K,
               picked with the switch on its front panel. You hear it dial,
               ring and shake hands.
  Printer      an Epson FX-80 style dot-matrix printer on LPT1, with
               green-bar paper you can tear off
  Mouse        PS/2 mouse (MOUSE.COM gives programs INT 33h)
  Joystick     a game port at 201h. Plug a gamepad into your computer and
               run JOYTEST, or use the page's PAD.
  Front panel  POWER, RESET and TURBO buttons, the red MHz display, and
               the POWER, TURBO, DISK and X86 lamps
  Monitor      the colour display has a DEGAUSS button; press it and
               watch the colours swim. The monochrome ones do without.

  TURBO ON and TURBO OFF do what the button does. TURBO MAX lifts the clock
  limit altogether: the machine then runs as fast as your own computer can
  manage, up to 999 MHz, while timers, music and the modem keep their real
  speed. TURBO RESTORE puts the clock back.

  The OPEN CASE screws on the system unit lift the lid. Inside you can swap
  cards (VGA or Hercules, Sound Blaster 16 or AdLib, the modem), add or
  take out SIMMs, the CD-ROM drive, the MIDI daughterboard and the mouse.
  Changes take effect the next time you switch on.


YOUR FILES: DRIVE D:
--------------------

  C: and D: are both saved in your web browser, but not in the same way.

  C: holds ARM-DOS and everything that comes with it. Your changes to C:
  are kept only until a new version of the C: disk comes out (new
  programs, fixes); C: then starts over, fresh from the factory, and what
  you changed on it is gone. The web page says so when that happens.

  D: is yours. Nothing on the page, and no new version of ARM-DOS, ever
  touches it: whatever you save there stays until you delete it. Keep your
  own programs, documents and saved games on D:. D:\README.TXT says more,
  including how to keep a copy of the whole drive on your own computer
  ("Back up D:" below the disk box).


PHONE NUMBERS
-------------

  Dial with TERM (its directory has them all), with ONLINE, or with Kermit
  in the ELBOW gallery (ATDT5551989).

    555-1989            The ARM Pit BBS, a second ARM PC running a
                        bulletin board. Log on as GUEST, password GUEST.
                        Its file areas hold things that are not on C:.
    555-0199            ARM-DOS Online: encyclopedia, dictionary, weather,
                        news and pictures, live (run ONLINE)
    555-0100            the Host Link: send files to and from your real
                        computer by ZMODEM
    399-2364            WOPR. You know what to type.
    555-0142            Europa Micro Support. Please hold.
    767-2676            the talking clock (time and temperature)
    555-3299            Europa Micro Sales. It is a fax machine.
    555-0386            The 386 Fortress, a rival board
    555-7734            The Floating Point
    555-0123            a wrong number
    011-7-095-231-1984  KREMVAX, Moscow, at 1200 bps over the cable
    867-5309            Jenny. Always busy.

  Everything else rings and rings.


KEYS AND BUTTONS ON THE PAGE
----------------------------

  Click the screen and every key goes to the PC, Esc included. Click
  anywhere else on the page to take the keyboard back.

    Ctrl+Alt+M          capture or release the mouse
    Shift+click         capture the mouse (so does the Mouse button)
    Ctrl+Alt+Del        the button of that name sends it for you
    Paste text          types text into DOS for you
    PC keys             an on-screen PC keyboard that follows KEYB
    Follow my layout    type with your own keyboard layout instead of by
                        key position (handy if yours is not US)
    Full screen         the monitor fills the display

  And inside ARM-DOS:

    Ctrl+Alt+P          POPUP's desk accessories (run POPUP first)
    Ctrl+Alt+F1, F2     US keyboard, and back to the KEYB layout
    Ctrl+C, Ctrl+Break  stop the current program, as on any PC
    Pause               pause the screen

  The disk box holds diskettes to drop into drive A:, and the CD-ROM box
  holds the Multimedia Sampler '93 disc (or any .iso of your own). The
  Files panel copies files between your computer and the disks.


CREDITS
-------

  ARM-DOS stands on a great deal of other people's work: MS-DOS 4.00 and
  GW-BASIC as released by Microsoft, GEM, id Software's and 3D Realms'
  games, Turbo Vision, TinyCC, newlib, the Nuked OPL3 core, VileR's PC
  fonts, GeneralUser GS, Future Crew's Second Reality and many more.

  C:\CREDITS.TXT names every one of them, with its author and licence.
  Many programs also keep their own licence file next to them, such as
  LICENSE.TXT, COPYING.TXT or VENDOR.DOC.

  Thank you to all of them.
