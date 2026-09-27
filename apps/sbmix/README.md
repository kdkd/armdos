# SBMIX.EXE - Sound Blaster 16 mixer levels

```
SBMIX [/INIT] [/Q] [MASTER=n] [VOICE=n] [FM=n] [CD=n] [LINE=n]
```

The CT1745 mixer of a Sound Blaster 16 powers up with master, voice and FM at 24
(-14 dB). On a real PC the card's own utilities (DIAGNOSE, MIXERSET) ran from
AUTOEXEC.BAT and set the owner's levels at every boot; ARM-DOS's AUTOEXEC.BAT runs
`SBMIX /INIT /Q` right after `SET BLASTER=...`, which sets master, voice and FM to 31
(0 dB) and CD/line to 24, so games that only touch voice or FM (or nothing) are
audible. `SBMIX` alone shows the levels as bars with their dB values; `n` is 0 (off)
to 31 (0 dB) in 2 dB steps; `/Q` suppresses messages. Exit code 1 without an SB16.
ARM-DOS's own utility (not a Creative program); uses sdk `<sb.h>` (`sb_mixer`).
Tested by apps/sbtest/tests/run.mjs.
