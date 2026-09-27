# CRASH.EXE - a tour of ARM exceptions

An original program, installed as `C:\DOS\CRASH.EXE`. A menu (arrows, Enter, 1-7,
Esc) with an explanation panel for the highlighted item, or directly:

    CRASH n | UNDEF | DATA | PREFETCH | DIVIDE | STACK | ALIGN | VECTOR

Each choice prints its explanation through DOS (so it stays on screen), then does it:

| # | what it does | what the user sees |
|---|---|---|
| 1 | `E7F198F8h` (`UDF #1988h`, the ARM ARM's permanently undefined space; a VFP instruction would run now that the VFP9-S is fitted) | `Exception 06h: undefined instruction at ... in CRASH.EXE` + registers |
| 2 | `LDR` from 20000000h (nothing decoded there) | `Exception 0Dh: data abort ... (address 20000000)` |
| 3 | `BLX` to 30000000h | `Exception 0Eh: prefetch abort at 30000000`, LR = the caller |
| 4 | `1988 / 0` in C: libgcc -> `__aeabi_idiv0` -> INT 00h | `Divide overflow` |
| 5 | endless recursion with a Microsoft C-style stack probe (no MMU, no guard page) | `run-time error R6000` / `- stack overflow`, exit code 255 |
| 6 | `LDR` from addresses +0..+3 | no exception: the ARMv5 rotated values next to what an 8086 would read |
| 7 | writes DEADBEE0h into the INT 21h vector, then calls DOS (asks Y/N first) | the BIOS crash screen (prefetch abort in SVC mode); Ctrl+Alt+Del restarts |

Items 1-5 are handled by ARM-DOS itself (kernel `proc.c` fault handler, ARCH.md 16):
the program ends (exit type 1) and COMMAND.COM carries on.

Tests: `make crash-test` - the menu and its explanation panel, each exception from the
command line with the kernel's exact message and the prompt coming back, the rotation
values of item 6, a bad argument, Y/N on item 7, the BIOS crash screen and a
Ctrl+Alt+Del reboot. Screenshots in `build/showcase-test/crash/`.
