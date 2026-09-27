# FDISK - MS-DOS 4.0 fixed disk setup, compiled for ARM

`FDISK.EXE` in C:\DOS is Microsoft's MS-DOS 4.0 FDISK (`CMD/FDISK`, 10,000
lines of C) built for the ARM-PC: the full-screen menus, creating, deleting
and activating primary and extended partitions and logical drives, reading
and writing the master and extended boot records through INT 13h, the
restart, and the command-line mode.

    FDISK [1 [/PRI:n] [/EXT:n] [/LOG:n] [/Q]]

It really partitions the hard disk. A disk without a master boot record gets
the ARM-DOS MBR (disk/mbr.S) - the x86 boot code of the original replaced by
ARM code with the same job and messages - so a disk FDISK partitions boots.
The restart is REBOOT.ASM's: BIOS data 0472h = 1234h (warm boot) and a jump to
the reset vector, as the BIOS's own Ctrl-Alt-Del.

## Source

`src/*.c`, `*.h`, `FDISK.MSG`, `FDISK.SKL` are Microsoft's (LF line endings,
a trailing Ctrl-Z removed; changes in `#ifdef ARMDOS`); unused files and the
assembly in `orig/`. Built at build time:

* `fdiskm.c` - the screens: `tools/menubld.py` replaces Microsoft's
  binary-only MENUBLD.EXE (it merges FDISK.MSG's screen templates - `^rrcc^`
  positions, `<H><R><B><S><I>...` codes, `Φnnnn` message references - with the
  FDISK section of USA-MS.MSG);
* `bootrec.c` - the MBR image from disk/mbr.S (`tools/mbr2c.py`).

`src/armdos.c` stands in for REBOOT.ASM and MS C's `signal(SIGINT, SIG_IGN)`;
the MAPPER calls (VIO/KBD/DOSBEEP/DOSEXIT) are in apps/mslib. Changes to the
C: FDISK's copy of MS C's DOS.H switched to the SDK's (INT 13h buffers as flat
pointers), the six FP_SEG/FP_OFF assignments, packed sublist/value list, two
hex constants gcc tokenizes differently (`0x1BE+index`).

Branding: the banner says `ARM-DOS Version 4.00` and `(C)Copyright Europa
Micro Systems 1988` instead of Microsoft's lines.

## Tests

`make fdisk-test` (`tests/run.mjs`) types the same keys into ARM-DOS as were
typed into the real MS-DOS 4.00 FDISK under DOSBox-X (`tests/real/keys-*.txt`,
captured screens in `tests/expected/*.txt`) and compares **every screen**,
text and colours, and the partition tables written, byte for byte:

* display: the menus on a partitioned disk (nothing written);
* create: blank disk, maximum primary partition, restart (warm boot seen),
  then the new disk boots through the MBR FDISK wrote ("Missing operating
  system": the partition is not formatted yet);
* partitions: 10 MB primary, extended partition, set active, display;
* logical: a logical drive in it (extended boot record compared too);
* delete: the logical drive, the extended and the primary partition;
* cmdline: `/PRI /EXT /LOG /Q`, parse errors, errorlevels.

120 checks, no known differences (the logical drive D: FDISK creates is
mounted by the kernel since round 3; its "FAT12" on the delete screen matches).

## Licence

Portions (C) Microsoft Corp., MIT License (`src/LICENSE`).
