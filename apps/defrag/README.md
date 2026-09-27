# DEFRAG.EXE - ARM Disk Optimizer 1.1

An original full-screen disk defragmenter in the style of the era's classics
(Norton Speed Disk, PC Tools Compress, the later DOS 6 DEFRAG). It is **not** part
of ARM-DOS 4.00 and **is not installed on C:** (`hd.json` keeps it out of the
catch-all `build/*.EXE -> C:\DOS` rule). It is a shareware download on The ARM Pit
BBS: the build makes `build/DEFRAG11.ZIP` (DEFRAG.EXE, DEFRAG.DOC, FILE_ID.DIZ;
DEFLATE, CR LF text, a fixed 1990-11-12 01:10 timestamp, made by
`tools/mkzip.py`). ARM-DOS's UNZIP unpacks it (tested).

```
DEFRAG [d:] [/F | /U] [/S[:]order[-]] [/H] [/FAST] [/AUTO] [/BW]
```

| switch | |
|---|---|
| `d:` | drive (a "Select Drive" dialog if left out) |
| `/F` `/U` | Full Optimization / Unfragment Files Only (skips the recommendation) |
| `/S:N` `E` `D` `S` | sort each directory by name, extension, date & time, size; `-` = descending |
| `/H` | move hidden (not system) files too |
| `/FAST` | no pacing |
| `/AUTO` | no dialogs: analyze, optimize (the switch's method, else the recommended one), exit 0 done / 1 error / 2 stopped, two report lines on stdout |
| `/BW` | monochrome attributes |

## The screen

Menu bar (`Optimize`), the cluster map (the whole disk, 78 x 16 blocks, one block
= N clusters: `■` used, `░` unused, `X` unmovable, `B` bad, `r` being read, `W`
being written; vertically centred when the disk needs fewer rows), a **Status**
box (Cluster n / percent / progress bar / Elapsed Time / method) and a
**Legend** box ("Drive A: 1 block = 3 clusters"), and a status line.

Flow: "Reading drive X: information..." (the map is revealed row by row), then
the **Recommendation** dialog ("49% of drive A: is not fragmented. Recommended
optimization method: Full Optimization." / "No optimization necessary." and
"Close all other programs before optimizing.") with Optimize / Configure; the
run; then "Finished Condensing" with Optimize another drive / Configure / Exit
DEFRAG. The Optimize menu (Alt, F10, Alt+O or a click): Begin Optimization
(Alt+B), Drive, Optimization Method, File Sort, Map Legend, Fast Speed (check
mark), About Defrag, Exit (Alt+X). F1 = keys help. Esc during a run asks "Do you
want to stop DEFRAG?" (Resume / Stop). Dialogs: Tab / arrows / Enter / Esc /
hot letters, and the INT 33h mouse (menu, items, radio buttons, buttons).

**Pacing**: every disk operation is charged a 1990 cost (diskette: 10 ms per
operation + 5 ms per sector, ~100 KB/s with seeks; hard disk: 4 ms + 0.35 ms per
sector) and the program waits (CPU halted) once a timer tick's worth has
accumulated. Measured in emulated time: the test diskette's Full Optimization
takes ~31 s, a fragmented 32 MB C: ~19 s, a 128 MB C: with 60 MB of fragmented
files 164 s. `/FAST` or Fast Speed turns it off (the same diskette: ~2 s).

## How it works (engine.c)

* The first FAT is read with INT 25h (packet form; after AH=0Dh so DOS's
  buffers are on the disk), the directory tree is walked sector by sector, every
  cluster gets an owner. Lost or cross-linked clusters, a bad chain or a
  directory without clusters stop it: "Disk errors found; run CHKDSK /F first".
* **Unmovable**: IO.SYS, ARMDOS.SYS (and MSDOS.SYS/IBMBIO.COM/IBMDOS.COM) in the
  root, every system file, hidden files (unless `/H`), bad clusters, and files
  the kernel has **open** (the SFT chain from the list of lists: the file's first
  cluster; the directory holding its entry is pinned and not sorted, because the
  SFT caches the entry's sector).
* **A move** (`move_run`) copies a run of consecutive clusters (up to 32 KB)
  to free clusters, reads the copy back and compares, then: (1) chains the new
  clusters in the FAT (a crash leaves only a lost chain), (2) switches the single
  pointer into the run - the previous cluster's FAT entry, or the directory entry
  (checked against the expected name and cluster first) - (3) frees the old
  clusters. Every FAT change goes to every FAT copy (only the dirty sectors). A
  moved directory gets its "." patched in the copy and its subdirectories' ".."
  entries updated after the switch. Esc stops between moves: the disk is always
  consistent.
* **Full Optimization**: directories first (breadth first), then the files
  directory by directory (in directory order, i.e. the sorted order if File Sort
  is on), packed from cluster 2. For each position the file's next run moves in;
  whatever is in the way is evicted to the highest free clusters (or, on a
  nearly full disk, parked further up the target area). A gap before an
  unmovable file is filled with later files that fit.
* **Unfragment Files Only**: each fragmented file moves whole to the first free
  gap that holds it; files that fit nowhere stay.
* **File Sort** rewrites each directory (up to 32 KB): ".", "..", the label
  and hidden/system entries first in their order (IO.SYS/ARMDOS.SYS stay the first
  root entries), then subdirectories, then files in the chosen order; deleted
  entries are dropped. Only changed sectors are written.
* Before the run the drive's current directory is set to the root (the CDS
  holds a cluster) and restored by path afterwards; the DPB free count is
  invalidated.

## Tests

`make defrag-test` (part of `make test`): `tests/run.mjs` boots headless from a
32 MB FAT16 C: that was itself fragmented with mtools (`tests/disks.mjs`:
interleaved writes over three directories, deletions, a hidden+system
LOCKED.SYS in the middle, holes refilled with bigger files, files replaced by
longer versions, a big file, a directory grown late, more deletions), plus
several such 1.44 MB FAT12 diskettes. After each run, with an independent FAT
reader (`tests/fat.mjs`) and the Linux tools: `fsck.fat -n` clean, every file
byte-identical (mcopy), the same directory entries, zero fragmented files and no
free space between files (Full), the unmovable files' chains and data unchanged,
IO.SYS still at cluster 2 in one piece, "." / ".." right. Covered: Full on A:
and on C: (the running system's disk; a program still loads from it afterwards),
a second Full run moves nothing, Unfragment + sort by name, the interactive
paced run (recommendation, mid-run status, 15-60 s), the menu by mouse, Map
Legend, F10 menu keys, About, Alt+X, Esc -> Stop (consistent and complete
afterwards), UNZIP of DEFRAG11.ZIP. `FULL=1` adds a 128 MB FAT16 C:, paced.
mtools and dosfstools are optional: without mtools there are no fragmented
volumes, and only the menus (on a diskette with nothing to optimize), `/?` and
UNZIP are checked; without dosfstools, no `fsck.fat -n` (each says so).
Screenshots land in `build/defrag-test/*.png`. A seed sweep of 24 more diskettes
(Full/Unfragment, every sort order, nearly full disks) was run during
development: all consistent.

## Deviations / limits

* An original program, not a clone: DOS 6 DEFRAG's layout and wording inspired
  the screen; no DOS 6 code or text.
* Directories larger than 32 KB are not sorted (still defragmented).
* A crash between steps (2) and (3) of a directory move can leave a
  subdirectory's ".." pointing at the old cluster (CHKDSK /F repairs it); a crash
  during File Sort can duplicate or drop entries of that directory (as with
  DOS 6's DEFRAG).
* The "used" block does not distinguish directories or already-optimized files.
