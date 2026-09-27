# COMP.COM — MS-DOS 4.00 COMP for ARM-DOS

`COMP [d:][path][file1] [d:][path][file2]` — byte compare of files, re-created
in C from the behaviour of `CMD/COMP/COMP2.ASM` / `COMPPAR.ASM` of the MS-DOS 4.0
source (MIT licence, Portions (C) Microsoft Corp.) and checked against the real
COMP.COM 4.00 in DOSBox-X. Shared helpers (parser, messages, start-up):
`apps/dosutil`.

What it does, as 4.00:
* missing names are asked for on STDERR (`Enter primary filename`,
  `Enter 2nd filename or drive id`, buffered input, parsed like the command line);
  a bad drive re-asks; an empty answer re-asks
* a directory means all its files; a path that is not a directory is split into
  directory + name; COMP changes into those directories and back (like the original)
* wildcards in the first name; `?` in the second takes the first name's character
* per pair: blank, blank, `C:\DIR\A.TXT and C:\DIR\B.TXT`, blank, then
  `Files are different sizes` / up to ten `Compare error at OFFSET x` +
  `File 1 = x` + `File 2 = x` (hex, unpadded) and `10 Mismatches - ending compare` /
  `EOF mark not found` (the last byte of the file that filled COMP's 60 KB buffer
  last is not ^Z) / `Files compare OK`; `File not found - x`, `Invalid path - x`
* `Compare more files (Y/N) ?` on STDERR, answer with INT 21h AH=0Ch AL=01h (echo),
  checked with AX=6523h; Y starts over with both prompts
* parse errors `Too many parameters - X`, `Invalid switch - /X` (exit 1);
  `Invalid drive specification` for a bad drive in either operand (exit 0)

Tests: `make comp-test` (tests/run.mjs): 12 redirected-stdout comparisons byte for
byte with the real COMP's output (tests/expected), 8 whole-screen comparisons
(prompts answered from files: bad drive at the prompt, two names at the first
prompt, wildcards, directories, errors), and a keyboard session.

Deviations:
* The original searches and reads with FCBs (AH=11h/12h/0Fh/27h); this one uses
  handle calls (4Eh/4Fh/3Dh/3Fh) with the same patterns, attributes and results.
* After `10 Mismatches - ending compare` the real COMP leaves two words on its
  stack (a bug); a following pair or the final RET then misbehaves (output stops).
  This COMP simply continues.
* COMP treats a bare `d:` as a failed CHDIR (as DOS 4's kernel answers it), so that
  `COMP \X\A.TXT C:` shows `C:A.TXT` as 4.00 does.
* No code-page / extended-attribute checks (deleted in 4.00 too, AN009).
