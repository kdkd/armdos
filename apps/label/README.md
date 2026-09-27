# LABEL.COM — MS-DOS 4.00 LABEL for ARM-DOS

`LABEL [d:][label]` — create, change or delete the volume label, re-created in C
from `CMD/LABEL/LABEL.ASM` of the MS-DOS 4.0 source (MIT licence, Portions (C)
Microsoft Corp.) and checked against the real LABEL.COM 4.00 in DOSBox-X.

As 4.00:
* `Volume in drive C is ARMDOS     ` (the label's 11 bytes, blanks included; no
  leading space unlike DIR/VOL) or `Volume in drive C has no label`,
  `Volume Serial Number is 4069-12FF` (INT 21h AX=6900h), then
  `Volume label (11 characters, ENTER for none)? ` and a read of STDIN (AH=3Fh)
* a label on the command line is used silently; more than 11 characters are
  dropped; `* ? [ ] : < | > + = ; , / \ . "` and control characters give
  `Invalid characters in volume label` and the prompt (so `LABEL /?` asks);
  blanks are allowed (`LABEL MY DISK`); `LABEL C: X` (blank after the drive) asks
* ENTER with an old label: `Delete current volume label (Y/N)? ` (no echo,
  AH=0Ch AL=08h, AX=6523h check, repeated until Y or N)
* the old label is found with an extended-FCB search (AH=11h, attribute 08h),
  deleted with an extended-FCB delete (AH=13h) and the new one created with
  AH=5Bh attribute 08h. LABEL itself only changes the root directory entry; the
  kernel's create then copies a new label into the boot sector's extended BPB
  (Set_Media_ID, as 4.00's DOS_Create does - checked on the real 4.00)
* `Invalid drive specification`, `Cannot LABEL a network drive` (IOCTL 4409h),
  `Cannot LABEL a SUBSTed or ASSIGNed drive` (TRUENAME of `d:CON`),
  `Cannot make directory entry` (the old label is put back); errorlevel 1

Tests: `make label-test` (tests/run.mjs): the screens of the real LABEL for the same
keys, the redirected stdout byte for byte, and the label in the root directory /
boot sector of the disk afterwards (17 checks).

Deviation: for a label with a blank LABEL creates the entry with an extended FCB
(AH=16h) instead of AH=5Bh. The result on disk is the same.
