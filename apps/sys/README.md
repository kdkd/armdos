# SYS.COM — ARM-DOS 4.00 system transfer

A re-creation in C of MS-DOS 4.00 `SYS.COM`, written from the MS-DOS 4.0 assembly source
(`CMD/SYS`, MIT License, © Microsoft Corp.).

```
SYS [d:][path] d:
```

## What it does

* **What it copies.** It copies `IO.SYS` and `ARMDOS.SYS` (4.00: `IO.SYS`, `MSDOS.SYS`) and
  writes the ARM boot record. The source is the current drive, or `[d:][path]` when one is given.
  `COMMAND.COM` is not copied, as in 4.00; copy it yourself.
* **Making room.** It makes room as 4.00 does:
  * Foreign entries in root slots 0 and 1 (the volume label, data files) are moved to the first
    free slot further down.
  * Old system files in those slots are replaced.
  * Clusters that `IO.SYS` needs are moved out of the way: data is copied, the FAT is relinked,
    and the directory entry is updated. For a directory, its `.` and its subdirectories' `..` are
    updated too.
* **IO.SYS contiguity.** ARM-DOS's boot sector loads **all** of IO.SYS contiguously from the first
  data cluster, where 4.00's needed only the first three sectors there. So the whole IO.SYS range is
  cleared, and the result is checked before the boot record is written.
* **The boot record.** It keeps the target's BPB, serial number, label and file-system type. A disk
  without an extended boot record gets a new serial (4.00 SYS's formula) and its volume label.
* **Messages.** All go to STDERR and the exit code is always 0, as in 4.00:
  * `System transferred`
  * `No room for system on destination disk`
  * `Invalid path or System files not found`
  * `No system on default drive`
  * `Cannot specify default drive` (`SYS C:` when C: is current)
  * `Insert system disk in drive A:` + `Press any key to continue . . .` (removable source without
    the files)
  * `Not able to SYS to xxx file system`
  * `Write failure, diskette unusable`
  * The parser's `Required parameter missing`, `Too many parameters - x` and
    `Invalid drive specification`.

## Deviations

* The version check of the source IO.SYS (4.00 compared a version word at offset 3) is not done:
  ARM-DOS's IO.SYS has no such word.
* The new files are created through DOS, so their attribute includes Archive (27h), as a DOS
  create gives.

## Tests

`apps/sys/tests/run.mjs`, part of `make diskutil-test`:

* `SYS A:` onto a used data diskette. Its label sits in slot 0, and its files and a two-level
  subdirectory occupy the first clusters.
* After the transfer: `fsck.fat -n`, every file byte-identical, IO.SYS contiguous from cluster 2,
  label and serial kept, ARM boot code in place, and the diskette **boots**.
* SYS with an explicit source onto a system diskette.
* A full diskette gives "No room", and it is left intact.
* Parameter errors.

The sources share `apps/format/dosutil.c`, `bootrec.c` and `bootsect.S`, through the
`*_inc.c`/`*_inc.S` wrappers.
