# DISKCOPY.COM — ARM-DOS 4.00 diskette copier

A re-creation in C of MS-DOS 4.00 `DISKCOPY.COM`, written from the MS-DOS 4.0 assembly source
(`CMD/DISKCOPY`, MIT License, © Microsoft Corp.).

```
DISKCOPY [d: [d:]] [/1]
```

## What it does

* **Copying.** It copies track by track through IOCTL 440Dh: 61h read track, 41h write track, and
  42h to format a blank target ("Formatting while copying").
* **Memory.** It uses as much conventional memory as DOS will give. That is about 30 cylinders on
  ARM-DOS, so a 1.44 MB diskette takes **three passes**, with these prompts on the one drive,
  exactly as in 1989:

  ```

  Insert SOURCE diskette in drive A:

  Press any key to continue . . .

  Copying 80 tracks
  18 Sectors/Track, 2 Side(s)

  Insert TARGET diskette in drive A:

  Press any key to continue . . .
  ```

  On the web page the visitor swaps the diskette in the drive when asked, and it really copies.
* **Single-drive detection.** `A: B:` on the one-floppy machine is recognised as a single-drive
  copy through the logical-drive calls (440Eh/440Fh), so DOS never shows its own "Insert diskette
  for drive B:" prompt.
* **Serial number.** The copy gets a new volume serial number (4.00's MAYBE_ADJUST_SERIAL formula),
  which is printed at the end: `Volume Serial Number is XXXX-XXXX`. Then
  `Copy another diskette (Y/N)? `.
* **Errors.**
  * `Not ready` / `Write protect error` (+ `Make sure a diskette is inserted into / the drive and
    the door is closed`) and a key press, then a retry.
  * `Unrecoverable read/write error on drive A: / Side n, track n`, then
    `Target diskette may be unusable`.
  * `SOURCE diskette bad or incompatible` and `Copy process ended`.
  * A hard disk or an invalid drive: `Invalid drive specification / Specified drive does not exist /
    or is non-removable`.
  * A file name: the parse error, then `Do not specify filename(s) / Command Format: DISKCOPY d: d: [/1]`.

## Deviations

The real 4.00 DISKCOPY could not be run to completion in DOSBox-X: it stalled reading tracks. The
dialogue follows the source and UTILITIES.md, and its first screen matches the DOSBox-X capture up
to that point. Two-drive copies are implemented but untestable on this one-floppy machine.
Ctrl-Break is DOS's default (abort), not 4.00's exit code 2.

## Tests

`apps/diskcopy/tests/run.mjs`, part of `make diskutil-test`, swaps images when asked, as a visitor
does. It covers DISKCOPY A: A: from a data diskette onto an unformatted one:

* the prompts alternate over three passes
* the first-pass screen
* "Formatting while copying"
* the copy is identical except for the new serial, which is the one printed, and is fsck-clean

It then runs DISKCOMP on the pair (see `apps/diskcomp`), plus error cases.
