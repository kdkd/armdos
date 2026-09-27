# ARM-DOS 4.00 kernel

The boot sector, IO.SYS and ARMDOS.SYS of ARM-DOS 4.00, plus HIMEM.SYS: a
from-scratch C (and a little ARM assembly) re-creation of MS-DOS 4.00's kernel
for the ARM/AT (ARCH.md). Behaviour and messages follow the MS-DOS 4.0 source
and documentation; the ARM-specific contract is ARCH.md (§5, §8-§11, §14-§16).

```
kernel/
  inc/kabi.h        structures shared by all parts (frame, device driver header and
                    request packets, BPB, DPB, SFT, CDS, PSP, MCB, LoL, DTA, FCB, init API)
  lib/klib.c        memcpy & co, ksnprintf, kint() (call INT n's vector with a frame)
  lib/karm.S        CPSR/WFI/CP15/SVC helpers (ARM code, so the C can be Thumb)
  boot/boot.S       FAT boot sector (ARM, code at 0x40)            -> build/bootsect.bin
  io/               IO.SYS: start.S, con.c (CON, INT 29h, INT 1Bh), aux.c (AUX/COMn,
                    PRN/LPTn, CLOCK$), disk.c (A: B: C:), init_sysinit.c + init_config.c
                    (SYSINIT: loads ARMDOS.SYS, CONFIG.SYS, drivers, shell) -> build/IO.SYS
  dos/              ARMDOS.SYS (AR1 image, Thumb): int21.c (dispatcher, errors),
                    char.c (console, line editor, ^C), dev.c (drivers, INT 24h, media
                    check), buf.c, fat.c, dir.c, name.c, file.c, find.c, fcb.c, mem.c,
                    proc.c (EXEC/terminate/faults), ioctl.c, misc.c, init.c -> build/ARMDOS.SYS
  himem/            HIMEM.SYS (XMS 2.0 device driver)                -> build/HIMEM.SYS
  tests/            test programs (SDK), TSHELL, scenarios.mjs + run.mjs (make kernel-test)
```

`make kernel` builds the four binaries; `make kernel-test` (part of `make test`)
builds the test programs and runs the suite (about 30 s).

## Boot

1. **Boot sector** (ROM or MBR -> 0x7C00, SYS mode, r3 = drive): moves itself to
   0x9F000, reads the first root directory sector to 0x500, requires `IO.SYS`
   and `ARMDOS.SYS` as the first two entries, loads the whole of IO.SYS
   (contiguous) to 0x700 with INT 13h AH=42h and jumps there with r3 = drive,
   r4 = BPB. Failure: `Non-System disk or disk error` / `Replace and press any key
   when ready`, then INT 19h.
2. **IO.SYS** start.S copies SYSINIT (linked at 0x90100) up high, zeroes both
   bss areas and calls `sysinit_main`. SYSINIT starts the resident drivers, reads
   ARMDOS.SYS through the block driver with a small FAT reader of its own,
   places it at the end of IO.SYS's resident part, applies its AR1 relocations
   and calls `dos_entry()`, which sets up the LoL, DPBs, the first 5 SFT entries,
   2 buffers, the CDS, the INT 20h-2Fh vectors and the fault handlers and hands
   back a small API (`struct dosapi`).
3. SYSINIT opens CON/AUX/PRN as handles 0-4 and processes `CONFIG.SYS` line by line
   (DOS 4 keywords and error messages, verified against the real 4.00 transcript in
   KERNEL.md 2.3), loading `DEVICE=` drivers as it meets them into the system area
   (each behind a DEVMARK header). Then it builds FILES/FCBS/BUFFERS/LASTDRIVE, makes
   the arena (a system block owned by 8 holding the drivers and tables, the rest free
   up to 0x90000), re-opens handles 0-4 (a new CON wins), runs `INSTALL=` programs,
   gives 0x90000-0x9FFFF to the arena and EXECs the shell. Nothing is printed on a
   clean boot, as in DOS 4.

## Memory map (default configuration, no CONFIG.SYS)

| address | size | what |
|---|---|---|
| 00000-003FF | 1 KB | interrupt vectors |
| 00400-004FF | | BIOS data area |
| 00500-006FF | | DOS area (0:0504 = which of A:/B: owns the floppy drive) |
| 00700-02C8F | 9,616 | IO.SYS resident: CON AUX PRN CLOCK$ block COM1-4 LPT1-3 |
| 02C90-0C80F | 39,808 | ARMDOS.SYS: the LoL first (pointer 02C9Ch, so MEM splits IO/MSDOS at 02C90h as on DOS 4), 34 KB of Thumb code, 5 SFT entries, 2 buffers, initial CDS, SYSINIT's PSP |
| 0C810 | 16 | first MCB (LoL-2): system block, owner 0008 |
| 0C820-0E71F | 7,936 | DEVMARK `F` FILES (3 more SFT entries), `X` FCBS (4), `B` BUFFERS (13 more, 15 in all), `L` LASTDRIVE (5 CDS) - plus `D` blocks for DEVICE= drivers |
| 0F060 | 16 | MCB of the shell (SYSINIT gives it no environment) |
| 0F070-9FFFF | | programs: **593,808 bytes (579.9 KB) free for the shell** |
| 90000-9FFFF | | (during boot only: SYSINIT, its stack, the moved boot sector) |
| 100000-10FFFF | 64 KB | the BIOS's data and stacks ("HMA"); the kernel runs INT 21h on the BIOS's SVC stack here |
| 110000-FFFFFF | 15,296 KB | extended memory, HIMEM.SYS's (XMS) |

So DOS itself takes 60.1 KB (0F060h bytes; the table rows above leave out the 2.3 KB that logical drives, SUBST/JOIN and the DOS 4 collating table added) of conventional memory (DOS 4.00 on a PC: about the
same). HIMEM.SYS adds 5.2 KB. With COMMAND.COM
(72,768 bytes resident + environment) and HIMEM.SYS loaded, the largest free
block is 519,600 bytes; the kernel's share of that is fixed, COMMAND.COM's is
not (DOS 4's own COMMAND kept about 4 KB resident and put its transient part
at the top of memory).

## Implemented

**INT 21h**: 00-0C (console, via handles 0/1 so redirection works; AH=0Ah line
editor with F1-F7, arrows, Ins, Del, Esc, template; ^C/^S/^P; INT 28h while
waiting), 0D 0E 0F-17 19 1A 1B 1C 1F 21-24 25 26 27 28 29 2A-2E 2F 30 31 32 33
(incl. 05 boot drive) 34 35 36 37 38 39-3F 40-43 44 (00-0F: device info, raw/
cooked, control strings, status, removable, remote, generic IOCTL on CON (display
info) and on disks: get/set device parameters, read/write/verify/format track,
get/set media ID, access flag; logical drive map) 45 46 47 48 49 4A 4B (00, 01,
03) 4C 4D 4E 4F 50 51 52 53 54 55 56 57 58 59 5A 5B 5C 5D (06, 0A) 5E/5F
(invalid function: no network) 60 61 62 63 65 (01 02 04 05 06 07 20-23 A0-A2) 66
67 68 69 6A 6B 6C; unknown functions return AL=0.

**Other interrupts**: INT 20h, 22h/23h/24h (defaults: 23h aborts, 24h fails),
25h/26h (both forms), 27h, 28h (idle), 29h (fast console output, in IO.SYS),
2Fh (1200h, 1216h, 1220h, 122Eh message hook; others "not installed"),
00h/06h/0Dh/0Eh (program faults), 1Bh (Ctrl-Break, in IO.SYS).

**File system**: FAT12 and FAT16 (FAT16 iff clusters+1 >= 4086), the first DOS
partition from the MBR, 32-bit sector numbers, BUFFERS= write-back cache in MRU
order (FAT sectors written to every FAT copy; flushed on close/commit/create/
terminate and whenever the console waits for a key), whole-sector transfers go
straight between the disk and the program (merging contiguous clusters), SFT
with DOS 4's 3Bh-byte layout, JFT with 67h growth, CDS per drive, canonical
paths with `/` or `\`, `.`/`..`, 8.3 truncation, device names anywhere,
wildcards and attribute rules as DOS's Search, volume labels, subdirectories
that grow, rename across directories, disk-full partial writes, FCBs (the FCB's
reserved bytes remember the directory entry, so FCB I/O needs no kernel state).

**Drives**: A: and B: (one floppy drive), C: = the first DOS primary
partition, then D:, E:, ... = the logical drives of the extended partition;
SUBST and JOIN through the CDS flags as DOS 4.

**Floppy**: media change through the change line (BIOS AH=16h), buffers
invalidated and the BPB rebuilt; a disk without a BPB is recognised by its FAT
id; the single-drive B: phantom with DOS's `Insert diskette for drive B: and press
any key when ready`; INT 24h for not ready / write protect / etc.

**Processes**: EXEC by signature (MZ+AR1 or raw .COM), allocation from the
header's min/max (max 0xFFFFFFFF = largest block), environment copied (or the
one given) + word 1 + full path, PSP per ARCH.md 9, FCBs and tail copied,
handle inheritance (no-inherit bit honoured), MCB names, TSR (31h, 27h),
exit codes/types (4Dh, once), INT 22h/23h/24h saved/restored through the PSP,
arena check at every exit ("Memory allocation error" / "Cannot load COMMAND,
system halted"), the root-process rules of DOS, AL=01 for debuggers.

**Faults**: in a program: `Divide overflow` (INT 00h, raised by the SDK's
`__aeabi_idiv0`), or `Exception 06h: undefined instruction at <pc> in <NAME.EXE>`
(0Dh data abort with the fault address, 0Eh prefetch abort) plus a register dump,
then the program ends (type 1) and its parent continues. In the kernel or in an
SVC-mode handler: the BIOS crash screen.

**CONFIG.SYS**: BREAK BUFFERS (1-99, /X accepted) COUNTRY (from COUNTRY.SYS, DOS 4's
format; "Invalid country code or code page", "Bad or missing <file>"; see "National
language support" below) DEVICE FCBS FILES (8-255) INSTALL
LASTDRIVE REM SHELL STACKS (checked, not allocated) SWITCHES (/K selects INT 16h
AH=00h instead of 10h) MULTITRACK DRIVPARM CPSW COMMENT IFS (accepted); errors as
DOS 4 ("Unrecognized command in CONFIG.SYS", "Bad command or parameters - x",
"Invalid STACK parameters", "Bad or missing <file>", each with "Error in CONFIG.SYS
line n"); missing shell: "Bad or missing Command Interpreter".

**Device drivers**: DEVICE= AR1 images starting with their header; INIT with
the command line, first drive, memory limit; the break address is respected;
several headers in one file are chained; a char device named like a built-in
one replaces it (DEVICE=ANSI.SYS-style); block devices get drive letters, DPBs
and CDSs. `kernel/tests/drv/` has a replacement CON and a RAM disk as examples.

**HIMEM.SYS**: XMS 2.0 (functions 00h-11h: version; HMA reported present but in
use by the BIOS; A20 always on; query/allocate/free/move/lock/unlock/handle info/
reallocate; no UMBs) over 0x110000-0xFFFFFF, INT 2Fh 4300h/4310h, INT 15h AH=88h
answers 0 KB once installed, `/NUMHANDLES=n` (default 32).

## For shell and program authors

* **Starting the shell**: SYSINIT EXECs `SHELL=` (default `X:\COMMAND.COM /P`, X = the
  boot drive) with AX=4B00h; the command tail is the rest of the SHELL= line, as DOS 4.
  The shell gets **no environment** (segment 0) and builds its own `PATH=`/`COMSPEC=`;
  FCB1's drive byte is the boot drive. Handles 0-2 = CON, 3 = AUX, 4 = PRN.
* **The first shell is the root process** (its PSP's parent field points to itself; PSP
  +0Ah is 0). When it terminates, its memory and files are kept, INT 22h/23h/24h are
  restored from its PSP, and execution continues at PSP +0Ah if the shell set it (DOS 4
  COMMAND's "re-enter the command loop"; reset your stack there), else the kernel frees
  the root's memory and EXECs the shell again. If it cannot be loaded: `Bad or missing
  Command Interpreter`.
* **EXEC** (ARCH.md 14.5): the parent's registers are saved on its own stack (keep >= 128
  bytes free when you EXEC) and restored when the child ends; AH=4Dh gives the exit code
  and type once. The DTA afterwards is the parent's PSP:80h. An x86 program (an MZ image
  without the `AR1` header, or a .COM whose first words are not ARM code) is handed to
  `\DOS\ELBOW.EXE` on the boot drive with its full path in front of the command tail
  (dos/proc.c `exec_x86`, apps/x86); without ELBOW the error is 11 (bad format), as DOS.
* **Device header names are at +10h** in ARM-DOS's 24-byte header (ARCH.md 14.4), not
  +0Ah. The LoL (52h) is word aligned and after +21h everything is 8 bytes later than in
  DOS 4 (ARCH.md 16); some pointers in it are unaligned: read them bytewise.
* INT 24h handlers get a register frame (ARCH.md 16: AH/AL/DI as DOS, SI = device header).
  While one runs, INT 21h 01h-0Ch work; a second critical error inside it is failed.

## National language support

Without COUNTRY= the machine is country 001, code page 437, with built-in tables. The
tables live in one writable `struct nls_state` (inc/kabi.h, dos/misc.c): DOS 4's
COUNTRY_CDPG - the COUNTRY.SYS path, the country, the global (CHCP) and system
(COUNTRY=) code pages, the AH=38h block, the upper-case table (also used for file names),
the file-name character table and the collating sequence.

* **COUNTRY=ccc[,[cp][,path]]** (io/init_config.c) reads COUNTRY.SYS as DOS 4.00's SYSINIT
  does. ARM-DOS difference: with no path and no `\COUNTRY.SYS` in the root of the boot
  drive, `\DOS\COUNTRY.SYS` is used. The file is built from MS-DOS 4.00's MKCNTRY.ASM
  (apps/nlsfunc).
* **AH=38h** get/set, **AH=65h** (AL=01h-07h, 20h-22h, A0h-A2h) and **AH=66h** answer from
  `nls` for the current country and code page and call NLSFUNC for anything else; without
  NLSFUNC they fail as DOS 4 does. A device that could not switch code page is error 65;
  a code page the country lacks is error 2 with extended error 13.
* IO.SYS writes 0 to the system board ports F6h/F7h at the start of CONFIG.SYS processing
  (the page's keyboard layout / code page mailbox, ARCH.md 4.6).

**The NLSFUNC interface (INT 2Fh AH=14h).** The kernel passes its data instead of NLSFUNC
reaching into DOSGROUP: **DI (r5) = `struct nls_state *`**.

| AX | from | in | NLSFUNC does |
|---|---|---|---|
| 1400h | CHCP, the kernel | | AL = FFh: installed |
| 1401h | 6602h | BX = code page, DX = country | reads the tables, selects the code page on CON (IOCTL 440Ch 4Ah), fills the structure and sets `cp` |
| 1402h | 65h | BP (r6) = info type, BX = code page, DX = country, SI (r4) = buffer, CX = length | fills the buffer like 65h; CX = length returned |
| 1403h | 38h set | DX = country | the country's tables (its entry for the current code page, else its first) |
| 1404h | 38h get | DX = country, SI (r4) = 34-byte buffer | that country's block |

AL returns 0 or the error (2 no COUNTRY.SYS, 13 no such country / code page, 65 a device
failed). NLSFUNC reads COUNTRY.SYS with ordinary INT 21h calls from inside the kernel's
INT 21h, as DOS 4's NLSFUNC did.

## Deliberate gaps / differences

* No SHARE: sharing modes are parsed and checked for validity but not enforced;
  locks (5Ch) always succeed.
* No network/IFS (5Eh/5Fh return "invalid function"; IFS= is accepted and ignored), but a TSR
  redirector can own a drive: a CDS with CDS_NET|CDS_VALID sends every file-system call to INT 2Fh
  AH=11h (dos/redir.c; the interface is described in apps/cdrom/README.md, "The kernel's
  redirector interface").
* COUNTRY: from COUNTRY.SYS (apps/nlsfunc), other countries and CHCP through NLSFUNC
  (below); with no path and no \COUNTRY.SYS, \DOS\COUNTRY.SYS is used.
* STACKS= is validated but allocates nothing: ARM-DOS's interrupt handlers all
  run on the BIOS's SVC stack.
* BUFFERS /X: no EMS, the buffers stay in conventional memory (max 99).
* FCBS=: the table is allocated (MEM shows it) but FCB I/O does not need it.
* The kernel flushes dirty buffers when the console waits for input (so an
  image persisted by the web page is up to date; invisible to programs).
* INT 25h/26h leave nothing on the stack (no flags to pop).
* The CON driver answers 440Ch CX=037Fh (display information), which DOS 4's
  CON leaves to ANSI.SYS/DISPLAY.SYS; kept because programs use it.
* 6Ch honours bit 13 (fail instead of INT 24h, for the open and later I/O on
  the handle) and bit 14 (commit after every write).
* AH=0Ch on a file STDIN flushes nothing (as DOS).

## Tests

`node kernel/tests/run.mjs [name...] [--verbose] [--serial]`: each scenario
builds a hard disk (and floppy) image with mkimage, boots it in the emulator
(testkit), drives it (the test shell TSHELL runs a script; some scenarios type
keys, insert/eject diskettes), checks the COM1 log written by the test programs
(`T:PASS`/`T:FAIL`), the screen, CPU faults, and finally the disk images with
`fsck.fat -n` and `mdir` (and content checks with mkimage's FAT reader).

| scenario | covers |
|---|---|
| hello | EXEC, argv/env/COMSPEC, PSP, AH=02/09/30/62 |
| files | create/read/write/seek/truncate sizes 0..70000, errors, dup/force dup, handle limits, 67h, 57h, IOCTL info, mkdir/rmdir/chdir/getcwd, `/`, `..`, a directory of 150 files (3 clusters), rename within/across directories, find first/next (attributes, label, interleaved), truename, free space, temp files, 6Ch, stdio |
| exec | nesting (parent -> child -> grandchild, exit codes), env blocks, MCB names, INT 20h, `bx lr` .COM, PSP:50h, AL=01, AL=03, divide/undefined/abort/prefetch faults, TSR 31h and INT 27h |
| mem | 48h/49h/4Ah/58h: fragmentation, first/best/last fit, resize, error codes |
| con-line | AH=0Ah: F1 F2 F3 F4 F5 F6 F7, arrows, Ins, Del, BS, Esc, tabs, buffer full |
| con-chars / con-cooked | 01h 06h 07h 08h 0Bh 0Ch, F-keys, cooked and raw CON reads |
| con-ctrlc / con-ctrlbreak | ^C and Ctrl-Break end a program (type 1), INT 23h "continue", BREAK=ON (Ctrl-Break is typed as Ctrl+ScrollLock: the emulator's keyboard sends Pause's E1 sequence even with Ctrl held, where a real keyboard sends E0 46) |
| con-ctrls / con-exit23 | ^S stops output until a key; AH=4Ch from inside an INT 23h handler ends the program cleanly |
| crit-* | INT 24h: not ready (Fail/Retry/Abort, AH/AL/DI/SI), write protect, Retry until a diskette is inserted |
| floppy-fill / floppy-swap | FAT12 filled to the last cluster and refilled (checked on the image), media change, B: phantom prompts |
| config-* | the DOS 4 CONFIG.SYS error transcript, HIMEM + RAM disk + replacement CON + INSTALL=, missing shell, default shell tail `/P` |
| fcb / misc | FCB calls; IOCTL; INT 25h/26h; date/time and file stamps; country, 65h, 2Fh; a debugger-style AL=01 run |
| redirect | `>`, `>>`, `<` with 45h/46h; 01h and 3Fh reading a redirected STDIN to its end |
| stress-random / stress-fillhd | random create/append/truncate/delete/rename in 8 directories checked against a model; the 32 MB C: filled to the last byte |
| root-abort / arena-trashed | Abort in the root shell restarts it; a wrecked MCB stops the system |
| idle | CPU halted (WFI) > 95% of the time at a prompt (measured 100%) |
| int28-tsr | an INSTALL= TSR hooking INT 28h creates a file while the shell waits for a key |
| printer | AH=05h, handle 4, `fopen("PRN")`, ^P printer echo on/off (the emulator's LPT1 output), 6Ch with "no INT 24h" |
| tracks | generic IOCTL read/write/verify/format track on A: |
| config-misc | BREAK=ON, SWITCHES=/K, COUNTRY= without COUNTRY.SYS, FCBS=, FILES=5 error |
| nls / nls-nlsfunc | COUNTRY=049, AH=38h/65h/66h, file names in the country's upper case; NLSFUNC: other countries, 6602h, 38h set |
| memmap / command-com | the memory map above; the real COMMAND.COM on top of the kernel |
| round4 | ^C read by an INT 24h handler, 69h without INT 24h, CHDIR "SUB\\" fails |
| round3 | a disk with an extended partition (D: formatted, E: unformatted), SUBST and JOIN through the CDS, extended attributes |
| round2 / redirect-both | the fixes requested by other components (6Ch DH, 6506h table, blanks in names, CHDIR "d:", label -> boot record, close commit, IOCTL error codes, FCB device search, bare "d:" find, no-env shell and FCB1 drive), `<in >out` on one line |
