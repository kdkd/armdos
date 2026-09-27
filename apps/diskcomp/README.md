# DISKCOMP.COM — ARM-DOS 4.00 diskette compare

A re-creation in C of MS-DOS 4.00 `DISKCOMP.COM`, written from the MS-DOS 4.0 assembly source
(`CMD/DISKCOMP`, MIT License, © Microsoft Corp.).

```
DISKCOMP [d: [d:]] [/1] [/8]
```

## What it does

It compares track by track through IOCTL 440Dh/61h, as many cylinders per pass as conventional
memory holds, plus a one-cylinder buffer for the other diskette.

**Prompt order.** On one drive the prompts alternate as in 4.00: each pass first reads the
diskette that is already in the drive, then asks for the other one:

```
FIRST, SECOND, FIRST, SECOND, ...
```

**Output:**

* `Comparing 80 tracks` / `18 sectors per track, 2 side(s)` (lower case, unlike DISKCOPY).
* `Compare error on` / `side n, track n` for every side that differs.
* `Compare OK` only when nothing differs.
* `Compare another diskette (Y/N) ?`

Volume serial numbers are ignored, so a DISKCOPY copy compares OK.

**Errors:**

* `FIRST/SECOND diskette bad or incompatible`
* `Drive types or diskette types / not compatible`
* `Not ready` + a key press, then a retry
* The same drive and parameter messages as DISKCOPY

## Deviations

The real program was not run to completion in DOSBox-X (see DISKCOPY). Compare errors do not
change the exit code, as in 4.00.

## Tests

The tests are in `apps/diskcopy/tests/run.mjs`, which is part of `make diskutil-test`:

* After DISKCOPY, DISKCOMP A: A: on the pair prints `Compare OK`, with the prompts in 4.00's order.
* After one byte is changed on side 1, track 40 of the copy, it prints
  `Compare error on / side 1, track 40` and no "Compare OK".
* `/X` is rejected with the usage text.
