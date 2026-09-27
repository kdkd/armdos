# ARMINFO.EXE - System Information for the ARM/AT

An original program (no Microsoft or Symantec code) in the style of the Norton
Utilities' SI: a full-screen report in blue boxes, and a teletype report (`/T`)
in the SI 4.x style that can be redirected. Installed as `C:\DOS\ARMINFO.EXE`.

    ARMINFO            full screen, five pages: PgDn/PgUp (or Enter/arrows), 1-5, R re-test, Esc
    ARMINFO /B         start at the benchmarks      ARMINFO /A   start at the ARM tests
    ARMINFO /T [/N]    teletype report on stdout (/N: skip the benchmarks)

Pages and where each figure comes from (nothing is hard-wired):

1. **System Summary** - computer name, BIOS maker/version/date (strings scanned in
   the ROM at FFF00000h, date turned into "Saturday, September 24, 1988"), CPU and
   clock (ports F1h/F2h), INT 21h AH=30h, INT 13h AH=08h geometry, logical drives
   (IOCTL 4408h), INT 12h, CMOS 30h/31h, XMS (INT 2Fh 4300h/4310h), INT 10h AH=1Ah/0Fh,
   BDA COM/LPT tables, keyboard flag 0496h, INT 33h, INT 15h AH=C0h.
2. **Processor** - CP15 main ID (`MRC p15,0,r0,c0,c0,0` = 41069265h decoded), cache
   type and control registers, the CPSR bit by bit (mode SYS), and eight instruction
   probes that are *executed*: Thumb via BLX, QADD saturation + Q flag, SMULBB, CLZ,
   UMULL, LDRD, SWP; Jazelle/VFP shown as not fitted.
3. **Memory** - the ARM/AT memory map, largest free block, and the MCB chain with the
   interrupt vectors each program has hooked (it shows POPUP's 08 09 10 13 15 28 2F).
4. **Benchmarks** - *Computing Index* (a Dhrystone-flavoured integer mix, `bench.c`
   -O2), *Disk Index* (32 scattered + 504 sequential INT 13h sector reads), and the
   *Performance Index* ((3 CI + DI) / 4), as bars against period machines. Timing: PIT
   channel 0 reprogrammed to 1 kHz with our INT 08h (BIOS tick chained at 18.2 Hz) plus
   the latched counter - 0.84 us resolution. Everything is measured in emulated time,
   so the Turbo button (100 vs 12 MHz) changes the result: CI 500.2 vs 59.7.
5. **ARM Architecture Tests** - hand-written asm (`armasm.S`) against the 8086 way and
   compiled C: barrel shifter (`ADD r12,r3,r3,LSL #2`, 16.6 vs 12.5 M/s), conditional
   execution (Euclid's GCD with SUBGT/SUBLT, 81,772 vs 65,626 GCD/s), LDM/STM block move
   (759 MB/s vs 24 MB/s for LDRB/STRB) at 100 MHz.

Calibration: CI 1.0 = an IBM PC/XT. The XT figure is set from the Dhrystone 1.1 ratio
of an ARM926 at 100 MHz (~110 DMIPS) to a 4.77 MHz 8088 (~0.22), i.e. CI 500 at
100 MHz. The reference bars (XT 1.0, AT 8 MHz 7.7, PS/2 80 17.8, COMPAQ 386/25 27.4,
COMPAQ 486/25 55.2) are approximate period SI figures for those machines. The Disk
Index models the XT's ST-412 as 93.3 ms per random access and 85 KB/s.

Deviations from Norton SI: the layout is an homage, not a copy; no printing; the
memory scan is replaced by the ARM/AT memory map; the BIOS has no fixed date at
F000:FFF5, so the date is found by pattern in the ROM.

Tests: `make arminfo-test` (`tests/run.mjs`; `tests/harness.mjs` is shared by the four
showcase programs) - teletype report redirected to a file and checked, all five pages at
100 MHz and 12 MHz with screenshots in `build/showcase-test/arminfo/`, CI scaling with
the clock, the asm beating the "8086 way", Esc restoring the DOS screen.
