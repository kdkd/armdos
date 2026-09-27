# TREE.COM — the MS-DOS 4.00 TREE command for ARM-DOS

    TREE [d:][path] [/F] [/A]

A C port of `CMD/TREE/TREE.ASM` + `TREEPAR.ASM` from Microsoft's MS-DOS 4.0
source (MIT licence), checked byte for byte against the real TREE.COM in
DOSBox-X. Goes to `C:\DOS\TREE.COM`.

```
Directory PATH listing for Volume ARMDOS
Volume Serial Number is 4069-12FF
C:\TT
├───A1
│   ├───B1
│   │   └───D1
│   └───B2
└───A2
```

The port keeps the original's structure — the line buffer with a 4-column
slot per level, the per-level work area holding the Find First/Next state,
the look-ahead for "another subdirectory follows" (Find Next in the shared
DTA), F_FIRSTIME — so the output is the same in every detail:
* `Directory PATH listing for Volume LABEL` (the label's period removed) or
  `Directory PATH listing`; `Volume Serial Number is XXXX-XXXX` when Get
  Media ID (INT 21h AX=6900h) works; then the start drive and path as given
  (`C:.` with no path, `C:TT\A1`, `C:\TT` — a trailing `\` removed) with
  the original's NUL byte before the CR LF;
* graphics `└─├│` (CP437 C0 C4 C3 B3), or `\-+|` with `/A`;
* `/F`: the files first (as the directory lists them; hidden and system
  files not shown), then a "blank" line that also shows what deeper levels
  left in the buffer (lines of 17, 21, 25... characters with trailing
  blanks, exactly as 4.0), then the subdirectories;
* a directory counts only if its attribute is exactly 10h, as in 4.0;
* `No sub-directories exist` + CR LF LF; `Invalid path - \NODIR` (after the
  header, as the original); `Invalid drive specification`; parse errors
  with the offending text (`Invalid switch -  /X` — with the blank before
  the switch — `Invalid switch - /F` for a second /F, `Too many parameters -
  \WORK`, `Parameter format not correct - /F:1`);
* the current directory and drive are restored at the end; exit code 0,
  1 on errors, 2 for a wrong DOS version.

Tests: `make tree-test` (apps/tree/tests/run.mjs: 23 checks — 15 listings
byte-compared with the real TREE's, error messages, directory restore).

Deviations: TREE does not hook INT 23h/24h to restore the current
directory when stopped with Ctrl-Break or Abort (4.0 does); no stack-depth
check ("Insufficient memory" for absurdly deep trees).
