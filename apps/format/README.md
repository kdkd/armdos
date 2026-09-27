# FORMAT.COM — ARM-DOS 4.00 disk formatter

A re-creation in C of MS-DOS 4.00 `FORMAT.COM`, written from the MS-DOS 4.0
assembly source (`CMD/FORMAT/*.ASM`, MIT License, © Microsoft Corp.) and checked
against the real `FORMAT.COM` running in DOSBox-X: for the same keystrokes the
redirected output is byte-for-byte identical, including the percent counter and all
the CRs and padding blanks. Only the serial number differs, because it comes from the clock.

```
FORMAT d: [/S] [/V[:label]] [/B] [/F:size] [/T:tracks /N:sectors] [/1] [/4] [/8]
```

## What it does

* **Diskette.**
  1. Prints `Insert new diskette for drive A:` / `and press ENTER when ready...` and waits for a
     line of input (AH=0Ah, with the flushes 4.00 does).
  2. Formats the disk one track at a time with IOCTL 440Dh/42h. The block driver behind that call uses INT 13h.
  3. After each track it waits three timer ticks (~165 ms, about 30 s for a 1.44 MB disk). The
     emulated controller finishes a track instantly, so without this pause the
     `nn percent of disk formatted` counter would not visibly count. The counter is 4.00's:
     `\r%3u percent...` plus 40 blanks and `\r`, printed only when the percentage changes.
* **After the tracks:** `Format complete`, then:
  1. The boot record: the ARM boot sector `build/bootsect.bin` embedded at build time
     (`bootsect.S`), with the BPB, drive number, `29h`, serial, `NO NAME    ` and `FAT12   `/`FAT16   `.
  2. Both FATs, with media byte and bad clusters.
  3. The zeroed root directory.

  These are written with IOCTL write-track requests (440Dh/41h), so an unformatted diskette
  never goes through DOS. Then IOCTL 40h (set device parameters) makes DOS rebuild the DPB, and
  AH=0Dh plus a DPB free count of "unknown" (as 4.00 does) complete the update.
* **`/S`.** `IO.SYS`, `ARMDOS.SYS` and `COMMAND.COM` are read into memory before the insert
  prompt, from the boot drive (`COMMAND.COM` is taken from `COMSPEC` when that is on the boot
  drive, as 4.00 does). They are written as the first root entries, IO.SYS contiguous from
  cluster 2, followed by `System transferred`. A diskette formatted with `/S` boots (tested).
* **Volume label.** The prompt (or `/V:label`), with 4.00's validation: `Invalid Volume ID` and a
  re-prompt, silent truncation to 11 characters. The label is created as an extended-FCB entry and
  also written into the boot record.
* **The report.** Total, used by system, bad sectors, available, allocation unit size and count, all
  from INT 21h AH=36h, in 10-column numbers with no commas. Then
  `Volume Serial Number is XXXX-XXXX`, generated from the date and time with FORMAT's
  `Create_Serial_ID` formula (KERNEL.md §1.4), then `Format another (Y/N)?`.
* **Fixed disk (`FORMAT C:`).**
  1. If the disk has a label, it first asks `Enter current volume label for drive C:`. A wrong
     answer gives `Invalid Volume ID` and exit code 4.
  2. It then asks `WARNING, ALL DATA ON NON-REMOVABLE DISK / DRIVE C: WILL BE LOST! / Proceed with Format (Y/N)?`.
     Anything but Y ends with exit code 5.
  3. It verifies the tracks (IOCTL 62h, as 4.00's IO.SYS "formats" a fixed disk, so data outside
     the system areas is not wiped), keeps the partition's BPB (or computes one from the partition
     table with IO.SYS's DiskTable2), and never asks "Format another".
* **Switches.**
  * `/F:` accepts 160, 180, 320, 360, 720, 1200 and 1440, with K/KB/M/MB spellings.
  * `/N:` and `/T:` work as in 4.00.
  * `/1`, `/4` and `/8` on the 3.5" drive give `Parameters not supported by drive`, exactly as
    4.00 did on a 3.5" drive.
  * The other messages follow 4.00: `Same parameter entered twice`, `Parameters not compatible`,
    `Must enter both /T and /N parameters`, `Required parameter missing -`,
    `Invalid switch -  /X` (with two blanks, as 4.00 prints it).
* **Errors.**
  * Write-protected disk: `Write protect error` (STDERR) and `Format terminated`, exit code 4.
  * Empty drive: `Not ready` and `Format terminated`, exit code 4.
  * A bad track 0: `Invalid media or Track 0 bad - disk unusable`.
  * Other bad tracks are marked `F7h` in the FAT and reported as `bytes in bad sectors`.

The real IOCTL error comes from INT 21h AH=59h, because DOS maps AH=44h errors to 5, as 4.00
does. Ctrl-C is ignored while the system areas and files are being written.

## Deviations

* There is no "Checking existing disk format" message. It is a DOS 5 message, and the real 4.00
  FORMAT does not print it (verified).
* `/B` is accepted but writes no placeholder IBMBIO/IBMDOS files. `/SELECT`, `/AUTOTEST` and
  `/BACKUP` are not implemented.
* On a diskette whose image is 1.44 MB, `/F:720` formats all 160 physical tracks and lays a 720 KB
  file system into the first 720 KB. The emulated drive's geometry is set by the image size.
* The Ctrl-C handler aborts through DOS rather than exiting with code 3.

## Files

`format.c` is the program. `dosutil.c`/`.h` is the small runtime shared with SYS, CHKDSK,
DISKCOPY and DISKCOMP: DOS-handle output, numbers, the parser's error texts, IOCTL/INT 25h/26h
helpers and Y/N via AX=6523h. `bootrec.c` builds boot records and FORMAT's serial numbers.
`bootsect.S` embeds `build/bootsect.bin`. The other programs compile these through one-line
`*_inc.c`/`*_inc.S` wrappers in their own directories.

## Tests

`make diskutil-test` runs `tests/run.mjs` (and the SYS, CHKDSK and DISKCOPY/DISKCOMP tests).
`tests/lib.mjs` is the shared harness: it boots a scratch C: with the kernel's test shell and
swaps floppy images. `tests/redir.c` (`DUREDIR.EXE`) runs a program with both stdin and stdout
redirected; the redirected output files are read back with `disk/mkimage.mjs`'s own FAT reader.
`fsck.fat` (dosfstools) and mtools are optional: without them their checks are skipped with a
note, and the rest of the tests still run. The FORMAT test covers:

* The outputs of `FORMAT A:`, an invalid then lower-case label, `/V:hello` and `/S`, compared with
  the real 4.00 captures in `tests/ref/` (DOSBox-X, same input files).
* `fsck.fat -n`, mtools, the boot record, BPB, serial and label bytes of the resulting diskettes.
* The `/S` diskette booting.
* Parameter errors, a write-protected diskette, an empty drive, and "Format another".
* `FORMAT C: /S /V:ARMDOS` on a scratch disk: the label check, the warning, fsck, and booting the result.
* With `build/COMMAND.COM`: `FORMAT A: /S` typed at the real `C:\>` prompt, and the diskette
  booting to COMMAND.COM.
