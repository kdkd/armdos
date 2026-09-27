# ELBOW — Emulated Legacy Binaries On Workstation

*"Your old programs, now with a little ELBOW grease."*

`ELBOW.EXE` (C:\DOS, also as `X86.BAT`) is ARM-DOS's answer to Apple's Rosetta:
it runs **genuine x86 real-mode DOS programs** on the ARM PC.

    ELBOW [/JIT | /NOJIT] [/STATS] [/MEM:n] [/NOEMS] [drive:][path]program [parameters]

Typing the name of an x86 program at the prompt starts ELBOW by itself, the way
NTVDM starts for a DOS program under Windows NT: the kernel's EXEC hands an x86
image (an MZ file without the `AR1` header, or a .COM whose first words are not ARM
code) to `\DOS\ELBOW.EXE` on the boot drive with the program's full path in front
of its command tail (kernel/dos/proc.c `exec_x86`; ELBOW takes the first word as the
program and passes the rest on, and the program's exit code comes back as ELBOW's).
AL=03h (overlays) is not handed off; without ELBOW.EXE the error stays 11 (bad
format).

`FILE.EXE` (C:\DOS) tells what a file is (ARM or x86 executable, device driver,
disk image, archive, game data, picture, sound, text).  `C:\ELBOW` holds the
demo: `DEMO.BAT`, `README.TXT`, x86 samples, a benchmark built from one C
source for both processors, and genuine MS-DOS 2.0 tools.

## What an x86 program gets

* **A CPU**: 8086/80186/80286 real mode plus the 80386 real-mode extensions
  (operand/address-size prefixes, 32-bit registers, FS/GS, `0F xx`: Jcc near,
  SETcc, MOVZX/MOVSX, BT*, BSF/BSR, SHLD/SHRD, IMUL r,rm, LSS/LFS/LGS) and the
  486 BSWAP/XADD/CMPXCHG.  It identifies as a 386.  No FPU (ESC opcodes are
  ignored, so the usual FNINIT/FNSTSW probe says "none").  Protected mode (LMSW
  or MOV CR0 with PE) ends the program with a message.
* **Two engines** (cpu.c, jit.c): an interpreter, and a dynamic binary
  translator (default) that turns hot x86 code into ARM machine code in a code
  cache (below).  Rare instructions fall back to the interpreter.
* **1 MB + 64 KB of x86 memory** from extended memory (XMS), 640 KB of it
  conventional with its own DOS MCB chain, 128 KB aligned (so the 8237 DMA
  controller can reach it: a program's writes to the DMA *page* registers are
  moved by where the x86 memory lies, address and count registers pass through
  unchanged - Sound Blaster music works).  Page tables with 256-byte pages
  map x86 A0000-BFFFF (video) and 400-4FF (BIOS data area) onto the machine's
  real ones: a program that writes B800:0000 or A000:0000 writes the screen,
  and one that pokes 40:17 sees the real keyboard flags.  When the VGA is in a
  planar or unchained mode, A0000h is the card's MMIO window onto its planes
  (ARCH.md §6); REP MOVS/STOS touching A0000h-AFFFFh then go element by element,
  in order, so latch copies in write mode 1 work.
* **EMS** (ems.c): LIM EMS 4.0 with 2.5 MB (160 pages, allocated from ELBOW's
  heap on first use), page frame E000h, functions 40h-4Eh, 50h, 51h, 53h, 54h,
  58h, 59h, 5Ah; detectable by the `EMMXXXX0` device name at the INT 67h vector's
  segment:000Ah and by opening `EMMXXXX0` (opens NUL).  Mapping a page rewrites
  64 entries of the page tables, cheap enough for players that map in their
  interrupt handlers (Second Reality's STMIK).  `/NOEMS`: none.
* **DOS** (dos.c, proc.c): INT 21h is translated into ARM-DOS INT 21h calls —
  x86 registers to r0-r6 (ARCH.md 5), every `seg:off` pointer to the flat
  address of that x86 location, CF/ZF back.  Handles are ARM-DOS handles, so
  files, devices, redirection and pipes are the real ones.  Handled inside the
  x86 world: memory (48h-4Ah, 58h, the MCB chain, LoL 52h), vectors (25h/35h),
  PSPs in x86 layout (50h/51h/62h/26h/55h), DTA, terminate/TSR (00h, 4Ch, 31h,
  INT 20h/27h), and EXEC (4Bh AL=00/01/03): an **x86 child** runs nested in the
  same box, with a shadow ARM PSP so it has its own handle table; an **ARM
  child** goes to the real EXEC (environment copied to ARM memory).  Tables
  DOS returns pointers to (country info and its case-map routine, 65h tables,
  DPB, media byte, InDOS, DBCS) are copied into x86 memory.  INT 23h (Ctrl-C)
  and INT 24h go to the x86 program's handlers if it has hooked them.
* **BIOS** (hle.c): INT 10h (incl. palette/font calls with ES:DX/ES:BP buffers,
  fonts copied into an x86 "ROM" at F000), 11h, 12h, 13h, 14h, 15h (C0h table,
  86h wait), 16h (without blocking inside the BIOS, so the program's hooks keep
  running), 17h, 1Ah, 2Fh, 33h (mouse, with x86 event handlers), 25h/26h
  (with the flags left on the stack), 67h (EMS).  EGA info (INT 10h AH=12h)
  comes from the BIOS: an EGA/VGA with 256 KB (with the planar modes
  0Dh-12h).  A program that has asked neither AH=12h nor AX=1A00h
  believes it is on a CGA, and its AH=10h palette calls are ignored as a CGA
  BIOS would (GW-BASIC passes a non-palette buffer there).  In CGA modes 4-6, text drawn through INT 10h (AH=09h/0Ah/0Eh) uses
  the program's own font when it has installed one at INT 1Fh (80h-FFh) or
  INT 43h (00h-7Fh), as a PC BIOS does; ELBOW draws those glyphs itself.
* **Ports**: IN/OUT go to the ISA window (VGA DAC and registers, PIT, speaker,
  keyboard controller, CMOS...).  Guarded: reading 20h/A0h (would acknowledge
  an IRQ here), the ARM-PC floppy controller (300h-307h) and system board
  (F0h-FFh).  PIC masks, the PIT and the speaker are put back when ELBOW ends.
* **Hardware interrupts into the program's own hooks** (irq.c) — the hard
  part.  ELBOW hooks the ARM INT 08h/09h/1Bh/1Ch, and the vectors of IRQ 3, 4,
  5 and 7 (INT 0Bh/0Ch/0Dh/0Fh: serial ports - Kermit on the COM2 modem - and
  LPT/sound), which work like IRQ0.  If the x86 IVT has a hook
  for the vector and the x86 CPU is running, the IRQ is made pending *without an
  EOI* (as an 8259 holds it in service) and raised in the x86 world at the next
  instruction boundary with IF=1; the program's handler sends its own EOI or
  chains to the original vector, which is ELBOW's stub.  The BIOS tick calls
  INT 1Ch, which becomes an x86 INT 1Ch when hooked.  Keyboard bytes are taken
  from the 8042 at IRQ time into a queue and presented one per x86 IRQ1 (IN
  60h/64h see that byte, as often as the handler reads it); a handler that
  chains to the BIOS gets the byte translated into the shared BDA buffer by a
  port of the BIOS's own INT 09h (kbd86.c).  This is what makes TSR-style
  hooks (read 60h, chain) and game-style ones (read 60h, EOI, never chain) both
  work.
* **Loader**: EXEPACKed programs (Microsoft's LINK 3.x, MASM's tools) say
  "Packed file is corrupt" when loaded in the first 64 KB with the A20 line
  on; ELBOW recognises them and loads them above a spacer, as LOADFIX did.
* **Printer**: INT 17h AH=00h reports ACK (D0h) after a byte, as a PC BIOS
  reading the port right after the strobe does (GW-BASIC insists on it); the
  bytes go to the ARM PC's LPT1 and the page's Epson printer.
* **Graphics text**: INT 10h AH=08h in modes 4-6 reads the character back from
  the screen by matching the cell against the INT 43h/1Fh fonts (GW-BASIC's
  editor reads every line typed in SCREEN 1 that way).
* Whatever way ELBOW ends — exit, Ctrl-C, a critical-error Abort, a crash — its
  PSP's terminate address is a trampoline (t22.S) that puts the ARM vectors,
  PIC masks, PIT and speaker back.

## The translator ("Rosetta" part, jit.c)

Hot x86 code (a block start seen twice) is translated to ARM code:

* **Superblocks**: unconditional JMPs are followed, conditional jumps become
  side exits, so a loop body is one block whose back edge branches to its own
  entry.  Exits to static targets are `LDR pc, [pc, #lit]` through a literal
  that first points at a stub and is patched to the target block once that is
  translated (chaining).  RET and indirect jumps look the next block up
  without returning to C.
* **Flags for free**: 16-bit (8-bit) arithmetic is done on values shifted into
  the top half (byte) of an ARM register, so the ARM N/Z/C/V flags *are* x86
  SF/ZF/CF (or !CF)/OF; `CMP` + `Jcc` becomes one ARM compare and one
  conditional branch.  Each setter also records the interpreter's lazy-flag
  state with one STM — unless a liveness pre-pass proves nobody can see it
  (the next setter comes before any exit, helper or store); ARM flags are
  saved around memory stores only when a later instruction reads them.  INC/DEC
  take CF from the ARM flags or recover it inline from the lazy state.
* **Memory**: inline page-table lookups (`rpt`/`wpt`); a store checks the
  write table's tag bit — pages holding translated bytes are tagged, and a
  store that really hits translated code invalidates those blocks (a
  byte-granular code map) and ends the current block: **self-modifying code**
  works (tests/asm/smc.asm, also when the next instruction is patched).  Data
  loaded by DOS into x86 memory (EXEC, file reads, INT 13h/25h) invalidates
  translations in that range.
* Shifts by constants are inline; shifts by CL, rotates, MUL/DIV, flag ops and
  string instructions call the interpreter's routines; REP MOVS/STOS copy in
  page-sized runs.  386 code: 32-bit operands (66h) for MOV, the ALU, INC/DEC,
  TEST/NOT/NEG/MUL/IMUL/DIV, shifts, XCHG, LEA, PUSH/POP, CWDE/CDQ, string ops;
  FS/GS overrides; MOVZX/MOVSX, IMUL r,r/m, SHLD/SHRD by an immediate, Jcc near
  (0Fh xx); IN/OUT; far CALL/JMP (direct and through memory) and RETF (the return
  address pushed as one 32-bit store, so a store that hits translated code never
  leaves half of it); LES/LDS, PUSHA/POPA, PUSH/POP FS/GS.  Anything else (INT,
  IRET, STI/POPF, the 67h address-size prefix, ...) ends the block for the
  interpreter.  On Second Reality this took the share of the ARM's time spent in
  translated code from 11% to 66% (apps/secondreality/tests/prof.mjs, which also
  lists why blocks end: `jit_why[]`).  `/JITOFF:n` leaves groups to the
  interpreter for bisecting (1 far flow, 2 66h, 4 0Fh, 8 IN/OUT, 16 LES/LDS/PUSHA/
  POPA/FS/GS); `/FARLOG` dumps the last 64 far transfers when one lands at CS <
  100h in low memory.
* **Cache**: 2 MB of ARM code (1.25 MB blocks, 0.75 MB exit stubs), blocks
  found through a chained hash.  (A direct-mapped table and a 768 KB cache
  made MASM 5.10 retranslate its code 30 times over: 24.6 s -> 5.1 s for one
  module.)  When full, the whole cache is dropped and refilled.
* The ARM926 emulator underneath compiles this ARM code to JavaScript with its
  own JIT: a **double JIT**.
* **Watching it**: the web page's inspector has an ELBOW view (web/js/elbow-panel.js)
  that shows the block the ARM CPU is in - its x86 instructions next to the ARM code
  made of them - and where the time goes (translated code, helpers, interpreter,
  DOS/BIOS).  ELBOW announces a descriptor of its tables on the system board's ports
  FCh-FFh for it (ARCH.md 4.7); nothing runs per block.  Tested by tests/elbowview.mjs
  (also web/js/x86disasm.js against ndisasm) and web/tests/test_elbow.py.

### Speed (emulated ARM926 at 100 MHz, the machine's nominal clock)

| workload | interpreter | translator | native ARM |
|---|---|---|---|
| tests/asm/bench.asm (sieve + mixed, 9.1 M x86 instructions) | 1.25 x86 MIPS (79 ARM insns per x86 insn) | 4.9 MIPS (20) | — |
| tests/LOOP (`add ax,cx` / `loop`) | 1.13 MIPS | 9.6 MIPS (10) | — |
| FIRE.COM (mode 13h fire, own INT 8/9) | 3 frames/s | 10.7 frames/s | — |
| BENCH (demo/bench.c, loops/s): sieve | 7 | 73 | 731 |
| memcpy (REP MOVSW) | 2,606 | 2,828 | 4,422 |
| CRC-16 | 4 | 60 | 452 |
| mode 13h fill | 1 | 21 | 311 |
| **average vs native** | **132x slower** | **8.4x slower** | 1x |

(BENCH86.EXE is the same bench.c compiled with OpenWatcom for 16-bit x86, run
under ELBOW; BENCHARM.EXE is it compiled with gcc for ARM.)  The translator is
the default; `/NOJIT` for the interpreter.  In the browser the machine runs at
up to 100 MHz real time, so these are real-world rates there.

## What runs (tested)

* **MS-DOS 2.0** (Microsoft, MIT licence, in C:\ELBOW\MSDOS20): MASM 1.10,
  LINK 2.00, EXE2BIN (together they assemble, link and build a working .COM on
  the ARM), DEBUG (assemble, `g`, "Program terminated normally", unassemble),
  EDLIN, SORT, FIND, MORE — with redirection and pipes.
* **The gallery** (C:\ELBOW\APPS, `MENU`): unmodified binaries from their
  rights holders' free releases (demo/apps/README.TXT has the provenance,
  licence evidence and SHA-256 of each original archive; SHA256.TXT of every
  file):
  * Apogee's March 2009 freeware releases — the Kroz series (Kingdom, Caverns
    II, Dungeons, Return, Temple, Final Crusade, Kingdom II, Lost Adventures),
    Beyond the Titanic, Supernova, Word Whiz (text mode, Turbo Pascal 3-5),
    Arctic Adventure, Monuments of Mars, Pharaoh's Tomb (CGA 320x200, own
    INT 9/1Ch handlers, INT 1Fh fonts, PC speaker; all four volumes each);
  * ZZT 3.2 (the MIT-licensed Reconstruction, byte-identical to Epic's
    release) with its demonstration world and editor;
  * Sopwith, the Author's Edition (David L. Clark, GPL, source included; CGA,
    its own keyboard handler and timer).
  Speed (/STATS): Kroz 5.8 MIPS at 99% translated, Sopwith 5.8 MIPS at 99%;
  ZZT, the adventures and the Apogee platformers are paced by the timer and
  run at their intended speed.  Known slow spot: Arctic Adventure's title
  picture takes about 20 s to slide in under ELBOW (3.1 MIPS average there);
  the game itself then plays normally.
* **Business and productivity** (C:\ELBOW\APPS, `OFFICE`, or `X86APPS` from
  anywhere; licence evidence in demo/apps/README.TXT):
  * **GW-BASIC built on the ARM**: Microsoft's MIT-licensed 1983 source plus
    the OEM modules by Spinellis, Gros and Chia, assembled by Microsoft MASM
    5.10 and linked by LINK 3.65 (from the MIT MS-DOS 4.00 release) running
    under ELBOW - 38 modules in about 80 s of emulated time; BASIC\SOURCE\
    BUILD.BAT repeats it and gives the same GWBASIC.EXE byte for byte.
    LPRINT goes to the printer; SCREEN 1 graphics; LOAN.BAS, SPIRAL.BAS.
  * **VDE 1.97** (Eric Meyer, copyrighted freeware): WordStar-style word
    processor; prints to LPT1.
  * **SC 6.21** (public domain spreadsheet, 1994 DOS port; GNU regex, source
    included): BUDGET.SC; `W` then `PRN` prints the sheet.
  * **DataPerfect 2.6** (Lew Bastian / WordPerfect / Novell, copyrighted
    freeware): needs FILES=30 or so in CONFIG.SYS, else it asks for 40.
  * **MS-DOS Kermit 3.14** (Columbia, BSD since 2011): terminal on COM2 - the
    ARM PC's modem - through its own IRQ 3 handler; ATDT5551989 is the BBS.
* **Second Reality** (Future Crew, 1993; public domain since 2013) in
  C:\ELBOW\DEMOS\SECOND - a 386 demo with its own EXE loader, Mode X/Y, EGA
  mode 0Dh, a PIT-timed "copper", Scream Tracker music from EMS through the Sound
  Blaster - start to end (apps/secondreality/README.md).
* **MS-DOS 4.00** (tested locally only, not on the disk): MEM, CHKDSK (reads
  the FAT with INT 25h), TREE, ATTRIB, GW-BASIC 3.23 (text, arithmetic, FOR
  loops, SCREEN 1 graphics with the right palettes).
* **FreeDOS** (GPL, in C:\ELBOW\FREEDOS with their source packages): TREE, FIND,
  MORE, CHOICE (errorlevels come back through ELBOW).
* **Stretch, partial**: MS-DOS 2.0 COMMAND.COM (`ELBOW COMMAND.COM C:\dir`, the
  directory to reload its transient part from) starts, runs x86 programs by
  name from its current directory as nested x86 children, and VER/TYPE/EXIT
  work; its DIR and redirection do not (it edits its PSP's job file table
  directly, which ELBOW only mirrors).  Not on the disk.
* OpenWatcom v2 16-bit C programs; nasm-built test programs (tests/asm).
* **Conformance**: 7,800 cases over ~330 instruction templates (ALU, shifts,
  rotates, multiply/divide, BCD, bit ops, SETcc after CMP/TEST/SUB/DEC/ADD for
  all 16 conditions, 386 ops) give results *and flags identical to a real x86*:
  tests/cpu/gen.py emits each case twice, as a DOS .COM run under ELBOW and as a
  32-bit Linux program run on the host's own x86 CPU.  Both engines.
* MS-DOS 2.0 SORT output checked against DOSBox-X (the same 21 bytes).

Deviations / not supported: protected mode and DOS extenders, XMS for x86
programs, the FPU, the Gravis UltraSound; the CP/M
`CALL 5` entry is approximate; INT 15h AH=4Fh (keyboard intercept) is not
offered to x86 hooks; a TSR at the top level ends with ELBOW.  Known program
quirk handled: MASM 1.10 and LINK 2.00 break with more than 512 KB free (a
signed compare) — ELBOW recognises them and gives them 512 KB (a
compatibility shim; `/MEM:n` sets it by hand).

## Files

| file | what |
|---|---|
| main.c | command line, loading, the run loop, /STATS |
| cpu.c | the x86 CPU: interpreter, lazy flags, helpers for jit.c |
| jit.c, jitasm.S | the translator and its entry/exit |
| mem.c | page tables, slow stores (code invalidation) |
| hle.c | IVT stubs, the F000 "ROM", BIOS services, ports (DMA page moves), nested x86 calls |
| ems.c | LIM EMS 4.0 (INT 67h) |
| dos.c | INT 21h translation |
| proc.c | x86 memory arena, PSPs, .COM/.EXE loader, EXEC, terminate |
| irq.c, kbd86.c | hardware interrupts into x86 hooks; the BIOS keyboard translation |
| t22.S | the terminate-address trampoline |
| file/file.c | FILE.EXE |
| demo/ | FIRE.COM, HELLO86.COM, bench.c (+ BENCH86.EXE built by OpenWatcom), msdos20/, freedos/, apps/ (the gallery) |
| dist/ | DEMO.BAT, ASM.BAT, MENU.BAT (the gallery's), README.TXT, X86.BAT |
| tests/ | run.mjs (`make x86-test`), cputest.mjs, cpu/gen.py, asm/*.asm, profilers |

## Tests

`make x86-test` (part of `make test`, ~3 minutes): 50 checks — the CPU suite
against the host CPU with both engines, MS-DOS 2.0 tools, EXEC in both
directions with separate handle tables, INT 08h/1Ch hooks (18 ticks/s), an INT 9
hook that reads 60h and chains (keys in order, Shift), Ctrl-C into an x86 INT
23h handler, self-modifying code in both engines, FIRE (mode 13h pixels, its
own 70 Hz timer, its keyboard handler), FILE, the two-machine benchmark,
and GALLERY.COM: a CLI'd retrace poll while a hooked timer IRQ is pending
(ZZT; the translator must not spin on pending interrupts it cannot take), an
INT 1Fh font in mode 4, AH=08h reading a character back in mode 4, and a
"CGA" program's AX=1002h being ignored; CARRY.COM: multi-word ADC/SBB chains
through INC and LOOP (the translator used to lose the carry-out of ADC/SBB
when an INC followed - every double-precision GW-BASIC result was wrong); and
GW-BASIC itself computing SIN, COS and double precision.
VGAEMS.COM: INT 21h AH=55h's memory size from SI, a 1 kHz timer hook that keeps
ticking through a STI/CLI loop (the lost-IRQ race), EMS, EGA info, mode 12h write
mode 2 and read map select, a Mode X latch copy with REP MOVSB, DMA page registers.
Debugging switches: `/TRACE` (every INT to port E9h, with the file name of opens and EXECs), `/TRACE2`, `/TRACEAT:n`,
`/SEQAT:n` (the translator's block entry/exit log from the n-th INT on:
compare it with `/TRACEAT:n` to find a mistranslated block - that is how the
lost ADC/SBB carry was found), `/JITINVAL` (invalidations and cache refills),
`/RING`, `/TRACEKB`, `/WATCH:lin`, `/OPHIST`, `/JITDUMP`; tests/profile.mjs and
tests/jitprof.mjs sample the ARM PC (the latter attributes samples to
translated x86 blocks).

## Building BENCH86.EXE

The x86 half of the benchmark needs OpenWatcom v2 (Sybase Open Watcom Public
Licence, https://github.com/open-watcom/open-watcom-v2). It is optional: point
`WATCOM_DIR` at the installation (the directory holding `binl64/wcl`),
`make WATCOM_DIR=/path/to/watcom build/elbow/BENCH86.EXE`; without it the build
uses the committed copy, demo/BENCH86.EXE.

## Licences

ELBOW, FILE and the demos: part of ARM-DOS (Europa Micro Systems); the CPU core
is original (no third-party emulator code).  MS-DOS 2.0 programs: Microsoft,
MIT licence (demo/msdos20/LICENSE.TXT) — they print their own copyright lines.
FreeDOS programs: GNU GPL v2, unmodified binaries shipped with their complete
packages (sources included) in demo/freedos/source.  The gallery (demo/apps):
Apogee Software freeware (2009 release notes next to each game; not public
domain, not for sale) and the other freeware titles (VDE, DataPerfect) are not in
the repository: tools/fetch-3rdparty.sh downloads them from their original release
archives into 3rdparty/elbow-gallery/ (3rdparty/manifest.json). ZZT reconstruction under MIT, Sopwith under the GPL
with its source — see demo/apps/README.TXT.
