# ARM-DOS SDK

C toolchain glue for ARM-DOS programs: start-up code, newlib system calls,
Microsoft-C style DOS headers, the linker script and the ELF-to-EXE converter.
The contract is `ARCH.md` (sections 5, 8, 9, 10).

## Building a program

Put an `app.mk` next to your sources in `apps/<name>/`:

```make
# apps/hello/app.mk
$(call armdos_exe,HELLO,hello.c)
```

`make` (from the project root) builds `build/HELLO.EXE`. The full form is

```make
$(call armdos_exe,NAME,SOURCES,CFLAGS,LDFLAGS,ELF2EXE-OPTIONS)
$(call armdos_com,NAME,SOURCES,...)       # build/NAME.COM (see ".COM" below)
```

* `SOURCES`: `.c`, `.cpp`, `.S`, `.s`, relative to the app.mk's directory.
  C++ is compiled with `$(ARMDOS_CXXFLAGS)` (the C flags plus `-fno-exceptions
  -fno-rtti -fno-threadsafe-statics`); link it with `$(ARMDOS_CXXLIBS)` in
  LDFLAGS (apps/edit is the example; see "C++ programs" below).
* `CFLAGS`: extra compiler flags (`-DDEBUG -Iinclude`).
* `LDFLAGS`: extra link flags/libraries. `$(ARMDOS_PRINTF_FLOAT)` turns on
  `%f/%e/%g` in printf/scanf (newlib-nano leaves them out; +~20 KB).
* `ELF2EXE-OPTIONS`: `--stack N` (default 8192), `--min-extra N`,
  `--max-extra N`, `--flags N`.

Each program gets a phony target of its own name (`make HELLO`), its objects,
ELF and link map in `build/obj/NAME/`. `ARMDOS_PROGRAMS` lists every output
(the disk images pick them up).

Compiler: `arm-none-eabi-gcc -marm -march=armv5te -mfloat-abi=soft -Os`,
`-ffunction-sections -fdata-sections`, `--gc-sections`, `-Wall -Wextra`,
newlib-nano (`--specs=nano.specs`). newlib's default multilib (ARM state,
v4T) is what gets linked: checked, the result contains no Thumb code and no
interworking veneers.

**Hardware floating point** (opt-in): the ARM926 has a VFP9-S coprocessor
(VFPv2; the ROM BIOS enables it). Add `$(ARMDOS_VFP)` (`-mfpu=vfp
-mfloat-abi=softfp`) to a program's CFLAGS:

```make
$(call armdos_exe,QUAKE,$(SRCS),$(ARMDOS_VFP) -O2,-lm)
```

The code uses VFP instructions but the soft-float calling convention
("softfp"), so it links with the SDK's soft-float runtime, and the link then
selects newlib's `arm/v5te/softfp` multilib (VFP libm). ARM state only (Thumb-1
has no VFP instructions).

By hand:

```sh
arm-none-eabi-gcc -marm -march=armv5te -mfloat-abi=soft -Os --specs=nano.specs \
    -isystem sdk/include -c prog.c
arm-none-eabi-gcc -marm -march=armv5te -mfloat-abi=soft --specs=nano.specs \
    -nostartfiles -nostdlib -T sdk/link.ld -Wl,-q -Wl,--gc-sections -Wl,--target2=rel \
    -o prog.elf build/sdk/crt0.o prog.o \
    -Wl,--start-group build/sdk/libdos.a -lc -lgcc -Wl,--end-group
node sdk/elf2exe.mjs prog.elf -o PROG.EXE
node sdk/exeinfo.mjs PROG.EXE
```

`-Wl,-q` (keep relocations) is required: elf2exe builds the relocation table
from them.

### Sizes

| program | .EXE |
|---|---|
| `hello.c` (`printf("Hello...\n")`) | **11,348 bytes** (10,744 image + 103 relocations + 192 header/stub) |
| same as a self-relocating `.COM` | 11,316 bytes |
| `files.c` (fopen/fgets/fprintf/malloc/getenv/argv) | 26,484 |
| `ints.c` (dos.h, conio.h, bios.h, int86, INT 1Ch hook) | 23,132 |
| `float.c` (`sqrt`, `atan`, `%f` with `$(ARMDOS_PRINTF_FLOAT)`) | 52,256 |

Of hello's 10.7 KB image, ~6 KB is newlib stdio and ~4.5 KB the ARM-DOS
runtime (start-up, argv/environ, heap + XMS, text-mode I/O).

## Files

| file | what |
|---|---|
| `crt0.S` | `_start`: zero bss, call `__armdos_start` |
| `link.ld` | everything at address 0: `.text` (`_start` first; a `.devhdr` section, if any, before it), `.rodata`, `.ARM.extab/.exidx`, init/fini arrays, `.data`; then `.bss`. Symbols `__image_end`, `__bss_start__`, `__bss_end__`, `end`/`_end` (section-relative, so they are relocated) |
| `libdos/startup.c` | `__armdos_start`, heap (`_sbrk`), XMS, `_exit`, argv/environ |
| `libdos/syscalls.c` | newlib glue: `_open _close _read _write _lseek _fstat _stat _isatty _unlink _rename _link _mkdir _getpid _kill _gettimeofday _times`, `rmdir chdir getcwd setmode`, errno mapping |
| `libdos/intr.S` | `_armdos_int10/13/16/1a/21/2f/33(regs)`, `_armdos_intr(n, regs)` (256-entry `svc #n` table), `_armdos_farcall(entry, regs)` |
| `libdos/dos.c conio.c bios.c io.c direct.c process.c` | the MS C libraries |
| `libdos/sb.c opl.c genmidi.c` | `<sb.h>`: Sound Blaster 16, OPL2/3, the MIDI helper and its built-in bank (Freedoom GENMIDI, BSD) |
| `libdos/mpu.c smf.c` | `<midi.h>`: the MPU-401 (UART mode), a 1 kHz MIDI clock, Standard MIDI Files and a player |
| `include/` | `armdos.h dos.h conio.h bios.h io.h direct.h process.h malloc.h fcntl.h sb.h midi.h` |
| `elf2exe.mjs` | ELF -> `.EXE` / `.COM` / bare AR1 |
| `exeinfo.mjs` | header + relocation dump (`--relocs`, `--disasm`, `--json`) |
| `armexe.mjs` | the formats as a JS module (build/parse/relocate) for tools and tests |
| `mzstub.asm` | the 8086 stub (nasm source of the embedded bytes) |
| `comstub.S` | the self-relocating `.COM` prologue (source of the embedded bytes) |
| `tests/` | test programs + `run-tests.mjs` (`make sdk-test`) |

## ABI summary

**Entry** (ARCH.md 8): SYS mode, `r0` = PSP, `r1` = load base (PSP+0x100),
`r2` = end of the memory block, `sp` = stack top. The load base must be at
least 8-byte aligned (it is 16: PSP is paragraph aligned) - the code uses
`LDRD/STRD`, which need doubleword alignment on ARMv5TE.

**Start-up** (`__armdos_start`), in order: record PSP/base/block end (`_psp`,
`_armdos_psp`, ...); INT 21h AH=30h -> `_osmajor/_osminor`; **shrink the
program's memory block** to PSP + image + bss + stack (INT 21h AH=4Ah, block
segment in `r8` = ES, ARCH.md 5.1), exactly as the MS C start-up does, so
`spawn`/`system` children get memory; environ; argv; constructors; `exit(main(argc, argv, environ))`.

**argv**: parsed from the command tail at PSP+0x80 with Microsoft C rules
(`"..."` groups, `\"` is a quote, 2n backslashes before a quote give n).
`argv[0]` is the full program path DOS 3+ stores after the environment
(double NUL, word count, ASCIIZ path); `""` if absent.

**environ**: pointers straight into the environment block (segment at
PSP+0x2C). `setenv`/`putenv` copy (newlib), the block itself is never modified.

**Heap**: starts at the stack top and grows by enlarging the DOS block (AH=4Ah,
in 4 KB steps, `_armdos_heap_grow`). When DOS cannot grow the block, the
runtime allocates **one XMS block** (INT 2Fh AX=4300h/4310h; XMS AH=08h query,
09h allocate, 0Ch lock -> DX:BX linear address) and continues the heap there;
newlib's malloc copes with the discontiguous break. The size is the largest
free XMS block, or `_armdos_xms_kb` KB if you define that variable
(`unsigned _armdos_xms_kb = 1024;`); `~0u` disables XMS. If there is no XMS
driver, malloc simply fails when conventional memory runs out - unless the
program defines `unsigned _armdos_raw_extmem = 1;`: then the heap continues in
the raw extended memory INT 15h AH=88h reports (from 1 MB + 64 KB up, skipping the
HMA where the ARM-PC BIOS keeps its data and stacks - as pre-XMS DOS extenders
did; HIMEM.SYS makes AH=88h report 0, so this cannot collide with it). The XMS block is
unlocked and freed in `_exit`, and on Ctrl-C (the runtime hooks INT 23h while
it owns XMS memory, then chains to the previous handler).
A failed AH=4Ah is followed by resizing the block back to what we had (DOS
leaves a block that failed to grow at the maximum size).

**Text and binary mode** (Microsoft C semantics):

* Every handle has a mode. `open()` uses `O_TEXT`/`O_BINARY` if given, else
  `_fmode` (default `O_TEXT`). `fopen("...b")` passes `O_BINARY` (newlib's
  `__sflags` sets `_FBINARY` = 0x10000 = our `O_BINARY`), plain `fopen` is text.
  Handles 0-4 start in text mode. `setmode(fd, O_BINARY)` changes it.
* Text write: `\n` -> `\r\n`. Text read: `\r\n` -> `\n` (a lone CR is kept,
  including across read boundaries); **Ctrl-Z ends the data**: for a file the
  file pointer is left at the Ctrl-Z so later reads return EOF too; for a
  device (console) the read ends there and the next read returns 0.
* A 0-byte `write()` returns 0 without calling DOS (a 0-byte AH=40h truncates).

**errno** is set from DOS error codes (2/3 -> ENOENT, 4 -> EMFILE, 5 -> EACCES,
6 -> EBADF, 8 -> ENOMEM, 0x11 -> EXDEV, 0x13 -> EROFS, 0x50 -> EEXIST, ...).

**Time**: `time()`/`gettimeofday()` from INT 21h AH=2Ah/2Ch (local time taken
as UTC; read twice around midnight). `clock()`/`times()` from the BIOS tick
count at 0x46C since program start, in `CLOCKS_PER_SEC` (100) units.

**Process**: `getpid()` = PSP segment. `abort()` prints "abnormal program
termination" and exits with 3, as MS C does. `spawn*`/`system` use INT 21h
AX=4B00h with the EXEC block of ARCH.md 5.1 (env segment, flat pointers to
the tail and two FCBs parsed with AX=2901h), then AH=4Dh for the exit code.
`exec*` = spawn, then exit with the child's code (DOS cannot overlay).
`P_NOWAIT` is refused. Programs without an extension are tried as given,
then `.COM`, `.EXE`, `.BAT` (a batch file runs as `%COMSPEC% /C file`); the `p`
variants search `PATH`. `system(cmd)` runs `%COMSPEC% /C cmd`
(`\COMMAND.COM` if COMSPEC is unset); `system(NULL)` says whether it exists.

## Headers

**`<armdos.h>`**: `struct armregs` (the frame, ABI), `struct psp`, `struct mcb`,
`struct armexe`, the interrupt primitives, `armdos_getvect/setvect`,
`armdos_callold`, port I/O (`armdos_inb/outb/inw/outw`, ISA port p at
`0x10000000+p`), `armdos_enable/disable/halt` (halt = CP15 wait-for-interrupt),
`armdos_debug()` (port E9h), `armdos_emu_exit()` (port F4h), BDA/VRAM pointers,
`armdos_xms_entry()`.

**Raising an interrupt**: `_armdos_int21(&r)` etc. load r0-r8 from the struct,
`svc #n`, store r0-r8 and the CPSR (`r.cpsr`, so ZF is visible) back and return
CF. `_armdos_intr(n, &r)` does any `n` via a 256-entry table of `svc #n; b`
pairs (2 KB, linked only if used; reentrant, so usable from handlers).

**Interrupt handlers** are plain C functions `void handler(struct armregs *f)`
(ARCH.md 5). Install with `_dos_setvect(n, handler)`; chain with
`armdos_callold(old, f)` / `_chain_intr(old, f)`. A handler returns results by
modifying `*f` (e.g. `f->cpsr |= ARM_CPSR_C`). IRQ handlers (INT 08h-0Fh,
70h-77h) run with IRQs off and must send EOI (`outp(0x20, 0x20)`) unless they
chain to a handler that does.

**`<dos.h>`** (Microsoft C): `union REGS` - **fields are 32 bits**; `x.ax` is all
of r0, and the byte registers overlay it little-endian so `h.al`/`h.ah` are
bytes 0/1 of `x.ax`, `h.bl`/`h.bh` of `x.bx`, etc.; `x.cflag` = CF, `x.flags`
= the CPSR after the call. **Pointers go in the offset register as flat
addresses** (`r.x.dx = (unsigned)buf` for DS:DX). `struct SREGS {es, cs, ss, ds}`:
only `es` (-> r8) and `ds` (-> r7) are used, for segment *values* (ARCH.md 5.1).
`int86 int86x intdos intdosx bdos segread`, `MK_FP FP_SEG FP_OFF`
(`FP_SEG(p) = p>>4`, `FP_OFF(p) = p&15`, so `MK_FP(FP_SEG(p), FP_OFF(p)) == p`),
`peek peekb poke pokeb`, `inp outp inpw outpw` (+ Turbo C `inportb` etc.),
`_enable _disable`, `_dos_getvect _dos_setvect _chain_intr`, `_dos_findfirst/next`
(`struct find_t` is the DTA, packed), `_dos_get/setdate`, `_dos_get/settime`,
`_dos_get/setdrive`, `_dos_getdiskfree`, `_dos_open/creat/creatnew/close/read/write`,
`_dos_get/setfileattr`, `_dos_get/setftime`, `_dos_keep`, `_dos_allocmem/freemem/setblock`,
`dosexterr`, `_psp _osmajor _osminor _osversion`. `far near huge _far interrupt
cdecl pascal` are defined empty (note: `interrupt` as a macro clashes with
identifiers of that name - don't include dos.h in code that has a struct field
called `interrupt`, like a device header).

**`<conio.h>`**: `getch getche kbhit ungetch` (DOS 07h/01h/0Bh, so they read
redirected stdin like MS C's), `putch cputs cprintf vcprintf` (BIOS INT 10h
AH=0Eh: straight to the screen, not redirectable), `cgets` (DOS 0Ah).

**`<bios.h>`**: `_bios_keybrd` (READY uses ZF), `_bios_disk` (`struct
diskinfo_t`, flat buffer), `_bios_equiplist _bios_memsize _bios_timeofday
_bios_printer _bios_serialcom`.

**`<io.h>`**: `setmode filelength tell eof chmod dup dup2 creat umask` plus
newlib's `open read write close lseek access unlink`.
**`<fcntl.h>`** (wraps newlib's): adds `O_TEXT O_BINARY O_RAW _O_*` and `_fmode`.

**`<direct.h>`**: `mkdir(path)` (one argument, MS C; the POSIX two-argument
call also compiles), `getcwd` (mallocs if buf is NULL), `chdir rmdir _getdrive
_chdrive _getdcwd`.

**`<process.h>`**: `spawnl spawnle spawnlp spawnlpe spawnv spawnve spawnvp
spawnvpe`, `execl... execvpe`, `P_WAIT P_OVERLAY`, `_armdos_exec(path, tail, envseg)`.

**`<malloc.h>`** (wraps newlib's): `_fmalloc _ffree halloc hfree farmalloc ...`
as the plain functions, `_memavl()` (bytes the heap can still grow in
conventional memory, or left in the XMS block), `_memmax()` (largest free DOS block).

**`<sb.h>`** - sound (ARCH.md 4.2-4.3; the ARM-PC's card is a Sound Blaster 16 at
220h, IRQ 7, DMA 1/5, and AUTOEXEC.BAT sets `BLASTER=A220 I7 D1 H5 T6`):

```c
sb_card sb;
if (sb_detect(&sb))                                   /* BLASTER (or its defaults), DSP reset, E1h version */
    printf("%s at %Xh, IRQ %u, DMA %u/%u, DSP %u.%02u\n", sb_name(), sb.port, sb.irq, sb.dma8, sb.dma16, sb.dsp_major, sb.dsp_minor);
sb_play_pcm(pcm, bytes, 22050, SB_16BIT | SB_SIGNED);  /* in the background; sb_busy() */
sb_start(11025, SB_16BIT | SB_STEREO | SB_SIGNED, 1024, mix, user);   /* stream: mix(buf, 1024, user) from the IRQ */
sb_set_rate(22050); sb_stop(); sb_set_volume(31, 31, 31); sb_mixer(0x82, -1);
if (opl_detect()) {                                   /* 3 = OPL3, 2 = OPL2, 0 = none (timer test at 388h) */
    opl_write(0x105, 1);                              /* raw registers, 000h-1FFh */
    midi_init(NULL);                                  /* GENMIDI bank: NULL = built-in, or a DOOM-style lump */
    midi_program(0, 19); midi_note_on(0, 60, 100); ... midi_note_off(0, 60);
    midi_control(0, 7, 90); midi_pitch_bend(0, 8192 + 2048); midi_shutdown();
}
```

* **Streams**: `sb_start` allocates 2 x `block` bytes with `malloc` inside one DMA page
  (64 KB for 8-bit, 128 KB for 16-bit channels; any RAM address works - the heap may be in
  XMS), fills both halves through the callback, programs the 8237 for auto-init
  (+ the high page register if an address is above 16 MB) and the DSP (SB16: 41h rate,
  B6h/C6h; older DSPs: 40h/48h/1Ch, 8-bit only), and installs the IRQ handler with INT
  21h AH=25h on INT 08h+irq (70h+irq-8). The handler reads mixer 82h, acknowledges
  (22Eh/22Fh), calls the fill callback for the half that just finished, sends the EOI,
  and chains interrupts that are not the card's. It runs in SVC mode with IRQs off:
  keep the callback short, no DOS calls. `sb_stop` (also run at exit) pauses, masks the
  channel, resets the DSP, restores the vector and the PIC mask.
* `sb_play_pcm` streams from your buffer in 100 ms blocks and leaves auto-init mode (D9h/
  DAh) after two blocks of silence; the data must stay valid until `sb_busy()` is 0.
* Everything is safe without a card: `sb_detect`/`opl_detect`/`midi_init` return 0 and
  the rest does nothing.
* **Waits** use port 61h's 15 us refresh toggle (DSP reset, the OPL timer test), not
  loops of port reads (an ISA access is not 1 us on this machine).
* **MIDI helper** (`opl.c`): GENMIDI (DMX) instruments incl. double-voice instruments
  (second voice with the fine-tuning offset), fixed-note percussion on channel 9 (notes
  35-81), note offsets, pitch bend (+-2 semitones), controllers 7/10/11/120/121/123,
  OPL3: 18 voices and stereo pan (C0h bits 4/5), OPL2: 9 voices. Voices are taken
  free-longest-first; when all are busy, a second voice or the oldest note of the
  highest channel is stolen. Frequencies are equal-tempered F-numbers (1/32 semitone
  steps), loudness a square-root curve of velocity x volume x expression. Not
  reentrant: drive it from one place (main loop or one timer ISR). No sequencer is
  included; DOOM's DMX-exact MUS player lives in apps/doom (GPL).
* The built-in bank is **Freedoom's GENMIDI** (freedoom commit d14dbbee, built with its
  `lumps/genmidi/mkgenmidi`; BSD 3-clause, the notice is in `libdos/genmidi.c` - keep it
  when you distribute a program that links it). 6.3 KB, linked only with `midi_init`.

**`<midi.h>`** - General MIDI on the MPU-401 (ARCH.md 4.3.1: 330h, IRQ 9, the P field of
`BLASTER=A220 I7 D1 H5 P330 T6`):

```c
if (mpu_detect(NULL) && mpu_uart()) {          /* port from BLASTER P (else 330h), FFh -> ACK FEh, 3Fh UART mode */
    mpu_msg(0xC0, 19, 0);                      /* program change, channel 1: church organ */
    mpu_msg(0x90, 60, 100); ... mpu_msg(0x80, 60, 0);
    mpu_send(sysex, n); mpu_all_notes_off();   /* raw bytes; CC 64/123/120/121 on all channels */
}
smf_file song;                                 /* Standard MIDI Files, format 0 and 1, SMPTE division too */
if (smf_load(&song, "C:\\MIDI\\BACH846.MID") == 0) {
    printf("%s, %lu s\n", song.title, song.length_us / 1000000);
    mpu_play_smf(&song, NULL, NULL);            /* blocking; Esc (or your poll callback) stops */
    smf_free(&song);
}
```

* `smf_next()` merges the tracks in time order and applies the tempo map (`ev.time_us`);
  `mpu_send_event()` sends channel messages and SysEx. PLAYMIDI.EXE (apps/midi) is the
  worked example: its own loop with a text UI on top of these.
* `midi_timer_start()` puts PIT channel 0 at 1000 Hz with its own INT 08h handler that
  chains the BIOS every 65536 counts (the 18.2 Hz tick and the DOS clock keep running);
  `midi_timer_us()` reads it; `armdos_halt()` then sleeps at most 1 ms. Restored by
  `midi_timer_stop()` and at exit. Don't combine it with another owner of the PIT.
* `mpu_uart()` registers an exit handler (`mpu_shutdown`: all notes off, reset to
  intelligent mode). Everything is safe without an MPU (`mpu_detect` returns 0 after
  ~30 ms). The names don't clash with `<sb.h>`'s OPL `midi_*` helper.

## .COM programs

* `elf2exe --com` writes the bare image, entered at offset 0 = PSP+0x100. It
  fails if the image has absolute relocations - i.e. it is for hand-written
  position-independent code (`sdk/tests/pic.S`, 56 bytes).
* `elf2exe --com --selfreloc` (what `armdos_com` uses) puts a 160-byte ARM
  prologue (`comstub.S`) in front of an ordinary C image: it applies the
  relocation table appended to the file, sets `sp` to image+bss+stack
  ("Not enough memory" and exit 8 if that exceeds the block), and enters the
  image with the EXE entry registers. So any C program can be a `.COM`.

## EXE layout (what elf2exe writes)

```
0x00  MZ header (64 bytes). e_cp/e_cblp cover only header + stub, so a real
      PC loads just the stub; SS:SP 0000:0100, minalloc 16 paras,
      e_lfarlc 0x40, e_lfanew -> the AR1 header
0x40  8086 stub (56 bytes): push cs; pop ds; mov dx,000Eh; mov ah,09h;
      int 21h; mov ax,4C01h; int 21h; "This program requires an ARM processor.",CR,LF,"$"
0x80  AR1 header (64 bytes)
0xC0  image (16-aligned), then the relocation table (u32 image offsets, sorted)
```

`--ar1` writes the same without the MZ part (AR1 header at offset 0): for
images DOS loads itself (ARMDOS.SYS, `DEVICE=` drivers, ARCH.md 14).

### Relocations

Absolute: `R_ARM_ABS32`, `R_ARM_TARGET1` (init arrays), `R_ARM_ABS32_NOI` ->
one table entry each. No fixup: PC-relative types (`CALL`, `JUMP24`, `PC24`,
`REL32`, `PREL31` in `.ARM.exidx`, Thumb branches, `*_PC_G*`, `MOVW/MOVT_PREL`,
`TARGET2` with `--target2=rel`, which sdk.mk links with), `V4BX`, `NONE`.
References to weak undefined symbols stay 0 (NULL); references to absolute
(`SHN_ABS`) symbols stay as they are (warning). Rejected, naming the
relocation, offset, section and symbol: `MOVW/MOVT_ABS`, `ABS16/12/8`,
`THM_ABS5`, SB-relative, GOT/PLT/TLS types, an `ABS32` that is not 4-byte
aligned (a pointer in a packed struct), a PC-relative reference to an absolute
address, and linker veneers (they carry absolute addresses with no relocation).

## Tests

`make sdk-test` builds `sdk/tests/*` into `build/sdk-tests/` and runs
`tests/run-tests.mjs` (the programs cannot run until the emulator, BIOS and
kernel exist, so the checks are structural):

* the embedded 8086 stub == `nasm sdk/mzstub.asm`, and `ndisasm` shows the
  expected instructions; the `.COM` prologue == assembled `comstub.S`
* each program's relocation table == the absolute relocations `readelf -r`
  lists independently (weak-undefined ones excluded); its image == `objcopy -O binary`
* loader simulation: relocated at 4 random bases, every relocated word points
  inside image+bss; entry is crt0
* elf2exe rejects MOVW/MOVT, unaligned ABS32, and `--com` with relocations;
  `.bss` size; `--ar1`
* the loader check fires on a planted bogus relocation

## Known gaps

* Nothing has run yet: the emulator/BIOS/kernel don't exist. All the INT 21h
  usage follows DOS 4 semantics plus ARCH.md 5.1; expect a round of fixes once
  it runs.
* newlib-nano printf: no `%lld`/`%llu`, no positional arguments; floats only
  with `$(ARMDOS_PRINTF_FLOAT)`.
* `.fini_array` (C++ static destructors, `__attribute__((destructor))`) is not
  run at exit (it would pull in atexit, +800 bytes). Constructors run.
* `signal(SIGINT)` is newlib's software emulation; Ctrl-C is DOS's INT 23h
  (terminates), not a signal. INT 24h "Abort" also bypasses `_exit`, so an
  XMS heap block would leak in that case.
* `P_NOWAIT`, `cscanf`, `sopen`/locking, `_heapwalk`, `graph.h` are not provided.
* `stat()` on a root directory reports a directory without a timestamp; `stat()`
  uses find-first, so wildcards are refused.
* Time zones: none; local time is treated as UTC.
* Handles >= 64 have no text-mode state (always binary). DOS 4 has 20 per
  process by default.

## C++ programs

`.cpp` sources are compiled with `ARMDOS_CXX` (`arm-none-eabi-g++`) and
`ARMDOS_CXXFLAGS` = `ARMDOS_CFLAGS` + `-fno-exceptions -fno-rtti
-fno-threadsafe-statics -fno-use-cxa-atexit` (defined with `=`, so a per-target
`ARMDOS_CFLAGS += -O2` applies to C++ too). `ARMDOS_CXXLIBS` (`-lstdc++
-lsupc++`, the nano variants) goes in the program's LDFLAGS. crt0 runs
`__libc_init_array` and elf2exe relocates `.init_array`, so static constructors
work.

* Link the program's own code as objects and a big library as an archive
  (`apps/edit/app.mk` does this for Turbo Vision): the linker then pulls in only
  the members that are needed, so an unused `<iostream>` user does not drag
  libstdc++'s locale machinery (~200 KB) in through its static initialiser.
* Without exceptions, provide `operator new`/`delete` on malloc/free and the
  `std::__throw_*` helpers your containers use (`apps/edit/armdos/cxxrt.cpp`) so
  that libstdc++'s `functexcept.o` (and the unwinder) are not linked.
* `thread_local` needs TLS (`__aeabi_read_tp`), which ARM-DOS does not have.
* DOS reads and writes at most 32 KB per call: `read()` may return less than
  asked (POSIX short reads), so loop.

## Other library notes

* `ARMDOS_TEXT_VRAM` (armdos.h) is `0xB0000` when the BIOS video mode (BDA 449h)
  is 7 (the Hercules/MDA option) and `0xB8000` otherwise; it is an expression, so
  read it after any mode change. `armdos_vga_present()` (INT 10h AX=1A00h) tells
  a program whether there is a VGA at all.
* `rename()` works (libdos defines `_rename_r` on INT 21h AH=56h; newlib's
  fallback via link/unlink cannot work on FAT).
* The heap continues in extended memory through XMS; a first request bigger
  than the largest XMS block does not give up XMS for later, smaller requests.

