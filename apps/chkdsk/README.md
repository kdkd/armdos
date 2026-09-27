# CHKDSK.COM — ARM-DOS 4.00 disk checker

A re-creation in C of MS-DOS 4.00 `CHKDSK.COM`, written from the MS-DOS 4.0 assembly source
(`CMD/CHKDSK`, MIT License, © Microsoft Corp.). It was checked against the real CHKDSK.COM in
DOSBox-X on the same deliberately damaged diskettes: for clean disks, lost chains (`N`, and `/F`
with `Y`), an invalid cluster in a chain, and size errors, the redirected output is byte-for-byte
identical before, during and after `/F`.

```
CHKDSK [d:][path][filename] [/F] [/V]
```

## What it does

* **Header.** `Volume LABEL______ created MM-DD-YYYY H:MMa`. The label is the raw 11 characters;
  the date and time come from the label's directory entry. It is followed by
  `Volume Serial Number is XXXX-XXXX` (IOCTL 440Dh/66h).
* **Reading the disk.** The FAT is read with INT 25h: copy 1, or copy 2 if copy 1 is unreadable.
  4.00's DOS 4 packet form is always used, so the >32 MB C: works. A FAT ID below F8h (and not F0h)
  asks `Probable non-DOS disk / Continue (Y/N)?`.
* **The walk.** The directory tree is walked depth first, entering each subdirectory as soon as it
  is found, and every cluster reached is marked.
  * A chain that runs into an invalid value (outside 2..max, or `F7h`):
    `   Has invalid allocation unit, file truncated`. An end-of-chain mark is set.
  * A bad first cluster: `   First allocation unit is invalid, entry truncated`.
  * A size that does not match the chain: `   Allocation error, size adjusted`. The size is set to
    the chain length, in both directions, as 4.00 does.
  * Bad `.` / `..` entries: `   Invalid sub-directory entry`, or the detailed messages with `/V`.
  * The file's path goes to STDERR and the message to STDOUT, as 4.00's retriever does.
    `Errors found, F parameter not specified` / `Corrections will not be written to disk` is
    printed once, before the first error, when `/F` was not given.
* **Lost chains.**
  * Clusters that are allocated but unreachable are collected into chains with 4.00's FINDCHAIN
    rules, and CHKDSK prints `   n lost allocation units found in n chains.` (on STDERR) and asks
    `Convert lost chains to files (Y/N)?`. It asks even without `/F`.
  * **Y**: `FILE0000.CHK`, `FILE0001.CHK`, ... are created in free root slots, with sizes of whole
    clusters (only with `/F`).
  * **N**: the space is freed, reported as `bytes disk space freed` (or `would be freed` without `/F`).
* **Cross-links.** A second pass reports every file on a shared chain:
  `   Is cross linked on allocation unit n`. Cross-links alone do not print "Errors found", as in 4.00.
* **The report.** Total, hidden files (the label counts, with 0 bytes), directories, user files,
  recovered files, bad sectors, available, allocation unit size, total and available units, then
  `655360 total bytes memory` / `bytes free`. These last two are 4.00's formula: PSP:[2] and the
  PSP segment. All numbers are 10 columns wide, with no commas.
* **`/V`.** Lists `Directory X:\...` and every file (8 blanks + path).
* **`CHKDSK file`.** Reports `... Contains n non-contiguous blocks` or
  `All specified file(s) are contiguous`.
* **`/F`.** Directory-entry fixes are written immediately with INT 26h. The FAT is written once to
  every copy at the end. Then comes AH=0Dh and a DPB free count of "unknown", so DIR sees the new
  free space. The free count is ARM-DOS's addition, harmless.

## Deviations

* Directories are read sector by sector with INT 25h instead of 4.00's FCB searches and CHDIR.
  Results, paths and messages are the same.
* "Convert directory to file" appears only for a subdirectory entry with no cluster. The
  `.`/`..` repairs set attribute, link and size but do not rebuild missing entries.
* The FAT12/FAT16 decision follows the ARM-DOS kernel (max cluster >= 4086) rather than CHKDSK's
  own `>= 4088`. They differ only for disks of 4085-4087 clusters.
* In DOSBox-X the real 4.00 CHKDSK hung on cross-linked test diskettes, so the cross-link output
  follows the source (CHKPROC.ASM pass 2) and was not compared with a capture.

## Tests

`apps/chkdsk/tests/run.mjs`, part of `make diskutil-test`, rebuilds the reference diskettes with
mkimage: label TESTDISK, serial 1234-5678, A-D.TXT and SUB\E.TXT. It then corrupts them and
compares CHKDSK's redirected output with `tests/ref/*.TXT`, which are captures from the real
MS-DOS 4.00. It also covers cross-links, `/F` with `N`, bad sectors, C: (FAT16), errors, and
`fsck.fat -n` plus mtools on what `/F` left behind.
