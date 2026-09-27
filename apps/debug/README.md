# DEBUG.COM — DOS 4.00's DEBUG for the ARM PC

`C:\DOS\DEBUG.COM` is MS-DOS 4.00 DEBUG as Microsoft might have shipped it for
a PC whose CPU is an ARM926: the same `-` prompt, the same one-letter commands
and parameter syntax, the same output layouts and the same `^ Error` pointer,
with an ARM register set, an ARM/Thumb disassembler, a line-by-line ARM
assembler and ARM breakpoints underneath.

```
C:\>DEBUG
-R
R0(AX)=00027390  R1(BX)=00027490  R2(CX)=000A0000  R3(DX)=00000000
R4(SI)=00000000  R5(DI)=00000000  R6(BP)=00000000  R7(DS)=00000000
R8(ES)=00000000  R9=00000000  R10=00000000  R11=00000000  R12=00000000
SP=000A0000  LR=00027391  PC=00027490   NV EI PL NZ NC AR SYS
00027490 00000000       andeq   r0, r0, r0
```

## HELLO

INT 21h AH=02h writes the character in DL; on ARM-DOS `svc #n` is `INT n`,
AH is bits 15:8 of r0 and DL is r3 (ARCH.md §5):

```
-A 100
00027490 mov r0,#0x200
00027494 mov r3,#'A'
00027498 svc #0x21
0002749C svc 20
000274A0
-G
A
Program terminated normally
-Q
```

(`svc 20` without a `#` is hex, like DEBUG's `INT 20`; `#` numbers follow GNU
as: `#0x41`, `#65`, `#'A'` and `#41H` are the same.)  A string with AH=09h:

```
-A 100
00027490 mov r0,#0x900
00027494 adr r3,120
00027498 svc 21
0002749C svc 20
000274A0
-E 120 "Hello, world!$"
-G
Hello, world!
Program terminated normally
```

Then `T` traces it one instruction at a time, `U 100` shows the code, and
`DEBUG C:\COMMAND.COM` followed by `U` shows the shell's own ARM entry code.

## Commands

All of 4.00's: **A**ssemble, **C**ompare, **D**ump, **E**nter, **F**ill,
**G**o, **H**ex, **I**nput, **L**oad, **M**ove, **N**ame, **O**utput,
**P**roceed, **Q**uit, **R**egister, **S**earch, **T**race, **U**nassemble,
**W**rite, and XA/XD/XM/XS (which answer `EMS not installed`: the machine has
XMS, not EMS). Numbers are hex, `,` or blanks separate parameters, a range is
`address address` or `address L length`, lists mix hex bytes and `'strings'`.

**Addresses** are 32-bit and printed with 8 digits. On input, 5 to 8 hex
digits are a flat address (`D B8000`), **1 to 4 digits are an offset from the
program's PSP** — DEBUG's CS/DS — so `A 100`, `G =100` and `D 80` mean what
they always meant; `SEG:OFF` is `SEG*16+OFF` (`D 0:400` is the BIOS data
area, `D B800:0` the text screen). An odd address to U, G or T means Thumb.
Nonexistent memory reads as FFh.

**R** shows r0-r12 (with the x86 names the INT ABI gives them: AX BX CX DX SI
DI BP, DS = r7 and ES = r8), SP, LR, PC, the CPSR flags in DEBUG's two-letter
style and the processor mode, then the next instruction with the memory
operand of a load/store (`ADDRESS=VALUE`, as DEBUG shows `DS:0015=7510`).

| flag | set / clear |
|---|---|
| V overflow | `OV` / `NV` |
| I (IRQ mask) | `DI` / `EI` |
| N sign | `NG` / `PL` |
| Z zero | `ZR` / `NZ` |
| C carry | `CY` / `NC` |
| T Thumb state | `TH` / `AR` |

`R reg` changes one register (`R0`-`R15`, `SP` `LR` `PC` `IP`, `CPSR`, and
`AX` `BX` `CX` `DX` `SI` `DI` `BP` `DS` `ES`); an odd value for PC selects
Thumb; CPSR must keep USR or SYS mode. `R F` changes flags by name, with 4.00's
`bf Error` / `df Error`; a bad register name is `br Error`.

**U** disassembles 32 bytes by default (ARM, or Thumb after a Thumb address or
in Thumb state) in GNU objdump syntax: `mov r0, #512  @ 0x200`, branch targets
as addresses. Coprocessors 10/11 are the VFP9-S: VFPv2 instructions print in
UAL (`vadd.f32`, `vldmia`, `vmrs`), anything VFPv2 does not define there
(VFPv3 and later, NEON, d16-d31) is UNDEFINED on this machine and prints as
`.word`. The mnemonic sits in DEBUG's column 24 followed by a TAB, as
4.00 prints its lines.

**A** assembles ARM (ARMv5TE) in GNU syntax — everything U prints assembles
back to the same instruction: data processing with shifts and any rotated
immediate (or its complement: `mov r0,#-1` becomes `mvn r0,#0`), `lsl`/`asr`/…
aliases, multiplies including the v5TE DSP ones, LDR/STR/LDRB/LDRH/LDRSB/
LDRSH/LDRD/STRD in all addressing modes, pc-relative `ldr r0, 180`, `adr`,
LDM/STM with every mode and the stack aliases, push/pop, B/BL/BX/BLX/BXJ,
SVC, BKPT, MRS/MSR, SWP, CLZ, QADD…, coprocessor instructions (`mcr p15,0,r0,
c7,c0,4` is the ARM926's wait-for-interrupt), the VFP9-S's VFPv2 instructions
in UAL (`vadd.f32 s0,s1,s2`, `vldr d0,[r1,#8]`, `vpush {d8-d15}`,
`vmov r0,r1,d0`, `vcvtr.s32.f64 s0,d1`, `vmrs APSR_nzcv,fpscr`), both `ldreqb` and `ldrbeq`
suffix orders, and `DB`/`DW`/`DD` (hex, like DEBUG's DB/DW), `.byte`/`.short`/
`.word`, `ORG`. Branch targets are DEBUG addresses (`bne 104`) or `0x…`
absolute ones. An error puts the caret under the bad operand and asks again.

**G** `[=address] [breakpoints…]` (up to 10, else `bp Error`), **T**
`[=address] [count]`, **P** `[=address] [count]`: see "How it works".

**N/L/W**: `N name [parameters]` names the file and sets the program's command
tail and FCBs. `L` of a `.EXE` or `.COM` loads it with INT 21h AX=4B01h ("load
but do not execute"): relocations applied, registers in the program's entry
state (R0 = PSP, R1 = load base, R2 = end of its block, SP, LR = PSP|1, PC and
the T bit at the entry point), the rest of the N line as its command tail —
exactly what `DEBUG FOO.EXE args` does. Any other file is read to PSP:100
with its size in CX (and BX = 0). `W` writes CX bytes from PSP:100 (or the
address given). `L`/`W address drive sector count` read and write absolute
sectors (INT 25h/26h, 32-bit sector numbers; drive 0 = A:).

**I port** / **O port byte** use the ISA window (port p at 10000000h+p):
`I F0` answers 41 (`'A'`, the ARM-PC board ID).

## How it works

Everything runs in SYS mode, as every ARM-DOS program does, so DEBUG cannot
use a trace flag or a privileged monitor mode. Instead:

* **Breakpoints** are `bkpt #0xDB` instructions (ARM `E1200D7B`, Thumb
  `BEDB`) planted in the program. BKPT raises a prefetch abort, which the
  BIOS turns into INT 0Eh (`bios/start.S`); DEBUG hooks INT 0Eh (and chains
  every other prefetch abort to the kernel's fault handler).
* **Switching** between DEBUG and the program is a frame swap in that handler
  (`trap.S`): DEBUG itself executes a BKPT at `dbg_trap`, the handler puts the
  program's r0-r15 and CPSR into the exception frame and returns; when the
  program hits a breakpoint the handler saves the frame as the program's
  registers and returns into DEBUG after `dbg_trap`. When the program ends,
  DOS continues at its PSP's INT 22h address, which DEBUG sets to
  `dbg_term22`: "Program terminated normally".
* **T** (trace) works out where the next instruction will go: the condition
  codes are evaluated, and branches, BL/BLX (immediate and register, with the
  ARM/Thumb switch), BX, LDR to pc, LDM with pc, data processing into pc,
  Thumb conditional and long branches, `pop {pc}`, `add/mov pc, rm` are
  computed from the registers and memory. One temporary breakpoint goes
  there, the program runs one instruction, the breakpoint comes out. An SVC
  is traced over (its handler runs in SVC mode, where no breakpoint can stop
  it).
* **P** steps over BL, BLX and SVC by stopping at the instruction after them.
* **^C** at DEBUG's prompt abandons the command (`^C`, new prompt). ^C while
  the program is in a DOS call stops it right after that call and shows its
  registers, as 4.00's DEBUG does.
* An INT 24h Abort of DEBUG's own disk access returns to the prompt instead of
  ending DEBUG (4.00 does the same).

## Deviations from 4.00 (all forced by the CPU or deliberate)

* Registers, flags, addresses and the disassembler/assembler are ARM's (see
  above); addresses print with 8 digits, so D/E/U/C/S lines are one column
  narrower than 4.00's `SSSS:OOOO`; register values are 8 digits, as is the
  count in `Writing 0000000C bytes`.
* The register display takes four lines plus the instruction (4.00: two plus
  one); 40-column mode is not specially handled.
* CX alone is W's count (a 32-bit register; L sets BX to 0). After `L` of a
  .COM or .EXE the registers are the program's entry state, so set CX before
  writing a loaded .COM back.
* HEX files are not supported (`.HEX` loads as binary; W refuses it as 4.00
  does).
* T does not trace into an SVC (INT) handler; A assembles ARM, not Thumb.
* Faithful 4.00 quirks kept: `L` of a missing file prints nothing (4.00's
  "File not found" message never reaches the screen), a missing .COM/.EXE an
  empty line; `N` with no name makes W fail with `File creation error`; G
  after "Program terminated normally" runs the program again.

## Files and tests

| file | what |
|---|---|
| `debug.c` | command loop, parser, R D E F M C S H I O U A N L W Q |
| `run.c` | G/T/P: breakpoints, the INT 0Eh handler, next-instruction logic |
| `trap.S` | the DEBUG ⇄ program switch, setjmp/longjmp, INT 25h/26h |
| `disasm.c` | ARM + Thumb + VFPv2 disassembler (a C port of `emu/disasm.mjs`) |
| `asm.c` | ARM + VFPv2 assembler |

`make debug-test` (part of `make test`):

* `tests/asmtest.mjs` builds the disassembler and assembler for the host
  (`tests/hostasm.c`) and checks: the disassembler equals `emu/disasm.mjs`
  (itself checked against `arm-none-eabi-objdump`) on ~84,000 random and
  per-class ARM encodings (VFP ones included) and 20,000 Thumb halfwords;
  every one of ~68,000 disassembled instructions assembles back to an
  instruction that disassembles identically; the assembler equals
  `arm-none-eabi-as -mfpu=vfpv2` on the ~48,000 of them GNU as accepts, plus
  118 hand-written DEBUG-style lines, error columns, branch/pc-relative
  targets and data directives.
* `tests/run.mjs` boots ARM-DOS headless (the kernel's test shell) and types
  DEBUG sessions: register display, A/U/G of the HELLO programs, D E F S C M H
  I, every error form, R reg / R F, tracing through a conditional loop, BL (P),
  BLX into Thumb and back through `pop {pc}`, LDR pc and LDM pc, breakpoints,
  `DEBUG K_HELLO.EXE args` (entry state, U of real crt0 code, T/P in C code,
  G, L again, Q with a program loaded, no memory leaked), U on COMMAND.COM's
  entry point, N/W/L files and absolute sectors, E's interactive mode, ^C at
  the prompt and in a running program.
* The output formats were compared with the genuine 4.00 DEBUG run in
  DOSBox-X: `tests/mkref.mjs` (developer tool, needs dosbox-x and mtools)
  records `tests/ref/*.OUT`.

## Source and licence

Written for ARM-DOS from the behaviour of MS-DOS 4.00 DEBUG, whose source
Microsoft released under the MIT licence (`CMD/DEBUG` in the MS-DOS 4.0
release); the message texts and command semantics come from it. Portions (C)
Microsoft Corp., MIT License.
