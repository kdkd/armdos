# ARM-DOS disk images

`disk/mkimage.mjs` (node, no dependencies) builds the floppy and hard-disk
images from a manifest, and can list, read and check them.

`make images` builds `build/hd.img` (drive C:, from `disk/hd.json`),
`build/d.img` (drive D:, the user's own, from `disk/d.json`: formatted, only
`disk/d/README.TXT` on it; the page ships just its non-zero sectors and makes
the drive once per browser, so a new build never reaches an existing D:) and
`build/floppy-boot.img` (drive A:, from `disk/floppy.json`). Files the
manifests name that do not exist yet (IO.SYS, ARMDOS.SYS, COMMAND.COM, the
boot sector) are skipped with a note, so the images build at every stage.
Other makefile fragments can make the images depend on their outputs with
`DISK_DEPS += build/IO.SYS ...` (disk.mk is included last).

Files that go on the disks as-is live in `disk/c/` (C:) and `disk/a/` (A:)
(e.g. CONFIG.SYS, AUTOEXEC.BAT); both trees are optional.

## Commands

```sh
node disk/mkimage.mjs build MANIFEST.json [-o OUT.img] [--base DIR]
node disk/mkimage.mjs build --format fd1440 --dir TREE [--boot FILE] [--label NAME] -o OUT.img
node disk/mkimage.mjs info    IMG              # partition table, BPB
node disk/mkimage.mjs ls      IMG [PATH] [-R]  # DIR-style listing (with attributes and clusters)
node disk/mkimage.mjs cat     IMG PATH         # file to stdout
node disk/mkimage.mjs extract IMG [PATH] DEST  # copy out a file or a tree
node disk/mkimage.mjs check   IMG              # FAT consistency + DOS 4 system-file rules
node disk/mkimage.mjs selftest                 # embedded boot code == disk/*.S
```

Paths inside the image are case-insensitive, `\` or `/`. For hard disks,
`--partition N` selects a partition (default: the active one). Command-line
options override manifest fields: `--format --size-mb --heads --spt
--cylinders --dir --boot --mbr --label --serial --date --oem --mtime`.

## Manifest

```json
{
  "base": "..",                       // paths are relative to this (default: the manifest's directory)
  "format": "hd",                     // fd360 fd720 fd1200 fd1440 fd2880 hd
  "sizeMB": 32, "heads": 16, "sectorsPerTrack": 63,   // hd only; or "cylinders"
  "label": "ARM-DOS",                 // volume label (boot sector + root entry)
  "serial": "1988-0617",              // default: the date as hex digits
  "oem": "ARMDOS4",                   // 7 chars at 0x04-0x0A (byte 3 is the branch)
  "date": "1988-06-17 12:00:00",      // timestamp of every entry (default shown)
  "useMtime": false,                  // true: use the host files' mtimes instead
  "boot": { "src": "build/bootsect.bin", "optional": true },
  "mbr":  "path",                     // hd: MBR code (default: disk/mbr.S)
  "tree": [ "disk/c", { "src": "more", "dst": "UTIL", "optional": true } ],
  "files": [
    { "src": "build/IO.SYS", "attr": "HSR", "first": 1 },
    { "src": "build/ARMDOS.SYS", "attr": "HSR", "first": 2 },
    { "src": "build/*.EXE", "dst": "DOS\\", "exclude": ["SETUP.EXE"], "optional": true },
    { "src": "notes.txt", "dst": "DOC\\README.TXT", "date": "1988-07-01 09:30" }
  ],
  "attrs": { "COMMAND.COM": "R" },    // attributes for tree files (R H S A)
  "dirs": [ "TEMP" ]                  // empty directories
}
```

* `tree` entries are copied recursively (subdirectories become FAT
  subdirectories with `.` and `..`); names are upper-cased and must be valid
  8.3 names - a longer host name is an error, not silently mangled (give the
  file a `dst` instead). Dot-files, `*.md` and `*.mk` are skipped.
* `files` entries are added after the trees and replace tree entries of the same
  name. A `src` may use `*`/`?` in its last component; then `dst` is a directory.
  A `dst` ending in `\` is a directory.
* `first: N`: these files become the first root-directory entries, in order of
  N, and are allocated first - so `IO.SYS` (`first: 1`) starts at cluster 2 and
  is contiguous, and `ARMDOS.SYS` (`first: 2`) is the second entry, as DOS 4's
  boot sector requires. The volume label entry follows them. (Every file is in
  fact written contiguously.)

## What the images look like

**Floppies** (FAT12, no partition table):

| format | sectors | CHS | sect/cluster | root | FAT sectors | media |
|---|---|---|---|---|---|---|
| fd360  | 720  | 40/2/9  | 2 | 112 | 2 | FD |
| fd720  | 1440 | 80/2/9  | 2 | 112 | 3 | F9 |
| fd1200 | 2400 | 80/2/15 | 1 | 224 | 7 | F9 |
| fd1440 | 2880 | 80/2/18 | 1 | 224 | 9 | F0 |
| fd2880 | 5760 | 80/2/36 | 2 | 240 | 9 | F0 |

**Hard disk**: size = cylinders x heads x sectors; default geometry 16 heads x
63 sectors (the BIOS must report the same through INT 13h AH=08h, since the
partition table's CHS values and the BPB use it), cylinders =
ceil(sizeMB / 504 KB) - the default 32 MB gives **66 cylinders = 66,528 sectors
= 34,062,336 bytes**. One primary partition, active, from cylinder 0 head 1
sector 1 (LBA 63, the FDISK convention; hidden sectors = 63) to the end of the
disk. Type **06h** (DOS 4 "big" FAT16) when it has >= 65,536 sectors, 04h for
a smaller FAT16, 01h for FAT12 (partitions under 16 MB, 4 KB clusters as DOS
3.3). The default is type 06h, FAT16, 2 KB clusters, 16,575 clusters, 512 root
entries, media F8h. FAT16 cluster size: 2 KB up to 128 MB, then 4/8/16/32 KB.

**Boot sector** (DOS 4 layout, ARCH.md 11):

```
0x00  ARM "b 0x40" (0xEA00000E)      -- byte 3 is the branch's top byte, so
0x04  OEM name, 7 chars                 the OEM field is 0x04-0x0A
0x0B  BPB: 512, spc, reserved 1, 2 FATs, root entries, total16, media,
      FAT sectors, sectors/track, heads, hidden, total32
0x24  drive (00h floppy / 80h hard disk), 0, 29h, serial, label, "FAT12   "/"FAT16   "
0x3E  (2 bytes, zero)
0x40  boot code .. 0x1FD
0x1FE 55 AA
```

The code starts at **0x40, not 0x3E**: the extended BPB ends at 0x3E, which is
only halfword aligned, and an ARM `b` can only reach word-aligned targets.

`boot` may be either
* a **512-byte sector** (e.g. the kernel's `kernel/boot` linked as a whole
  sector): used as is, except that mkimage writes the BPB (0x0B-0x3D) and
  55AA; its branch, OEM name and code are kept. Link it so its code does not
  overlap 0x0B-0x3D; or
* a **code blob** of up to 446 bytes, placed at 0x40, with `b 0x40` written at 0.

Without one, the sector gets `disk/nosys.S`: print "Non-System disk or disk
error / Replace and press any key when ready" (INT 10h AH=0Eh), wait for a key
(INT 16h), INT 19h.

**MBR** (`disk/mbr.S`, 436 bytes; `mbr` overrides with a blob <= 440 bytes or a
512-byte sector whose first 0x1BE bytes are used): entered at 0x7C00 (SYS mode,
r3 = drive, sp = 0x7C00), copies itself to 0x0600, finds the single active
partition (0x80; others must be 00h, else "Invalid partition table"), reads its
first sector to 0x7C00 with INT 13h AH=42h (LBA; flat buffer pointer in the
packet) or, failing that, AH=02h with the table's CHS, 5 tries ("Error loading
operating system"), checks 55AA ("Missing operating system"), and jumps to
0x7C00 in ARM state with **r3 = drive, r4 = flat pointer to the partition entry
(DS:SI), sp = 0x7C00**. No active partition: INT 18h.

## Tests

`make disk-test` (`node disk/test-mkimage.mjs`) builds every floppy format and
20/32/100 MB and 10 MB (FAT12) hard disks from a tree with subdirectories and
checks each with mkimage's own `check`, **mtools** (`mdir`, `mcopy` round trip of
every file, `mlabel`) and **`fsck.fat -n`** (dosfstools; on the partition
slice for hard disks), plus `sfdisk -d` for the partition table; boot-code
placement, both `boot` forms; error cases (8.3 names, root full, disk full);
and deliberate defects (a cross-linked FAT, IO.SYS not first) to prove the
checks fire. mtools/dosfstools checks are skipped if the tools are missing
(`sudo apt install mtools dosfstools`).

`make setup-floppy-test` (`disk/test-setup-floppy.mjs`) uses the Startup
diskette as a DOS setup disk: boots from it, runs the DOS Shell from A:\DOS,
`FORMAT C: /S` (current volume label, warning, Y), `INSTALL`, then boots the
fresh C: without the diskette and runs the Shell and EDIT from there.

## The Startup diskette

`build/floppy-boot.img` is a bootable system diskette with DOS in A:\DOS, as a
DOS 4 setup disk had it: C:\DOS minus what does not fit in 1.44 MB (the
programming tools, DOCTOR/SAY, the sound and joystick testers, ARM Commander,
ARMINFO, FILE, UNZIP, POPUP). `disk/a/` holds its CONFIG.SYS, AUTOEXEC.BAT (which
explains the two steps), INSTALL.BAT (copies A:\DOS to C:\DOS, adds CONFIG.SYS,
AUTOEXEC.BAT and the C: DOSSHELL.BAT from A:\SETUP when missing) and an A:
DOSSHELL.BAT. `FORMAT C: /S` then `INSTALL` leaves a fresh, empty ARM-DOS on the
hard disk; the page's "Reset C: to factory" restores the original C:.

## Per-app fragments

`make` merges `disk/<image>.json` with every `apps/*/<image>.json` (image = `hd`,
`floppy`, `floppy-games`, `floppy-utils`) into `build/manifests/<image>.json`
(`disk/merge.mjs`). A fragment holds `files`, `tree`, `dirs` (appended) and
`excludeFromDos` (names kept out of the catch-all `build/*.EXE -> C:\DOS\` rule).
Paths in fragments are relative to the project root. So an app only ever edits
its own directory, e.g. `apps/zork/hd.json`:

    { "dirs": ["GAMES", "GAMES\\ZORK"],
      "files": [ { "src": "build/ZORK.EXE", "dst": "GAMES\\ZORK\\" },
                 { "src": "apps/zork/data/ZORK1.Z3", "dst": "GAMES\\ZORK\\" } ],
      "excludeFromDos": ["ZORK.EXE"] }
