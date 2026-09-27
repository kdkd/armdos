# SBTEST.EXE - Sound Blaster test

`C:\DOS\SBTEST.EXE` finds the Sound Blaster and the OPL FM chip and plays something
on each - the first thing to run when a game is silent:

```
C:\>SBTEST
Sound Blaster test - ARM-DOS

Sound Blaster 16 found at 220h, IRQ 7, DMA 1/5, DSP 4.05
OPL3 (YMF262) FM synthesizer found at 388h
Playing a PCM chime (16-bit, 22050 Hz, DMA 5)...
  13 blocks played
Playing an FM chord (C4 E4 G4, General MIDI program 20)...
Done.
```

The chime is a two-note bell (E6, C6) synthesised at start-up and played with
`sb_play_pcm` (16-bit signed mono, auto-init DMA on the 16-bit channel, 100 ms
blocks, one IRQ each); the chord is a church organ from the SDK's built-in GENMIDI
bank through `midi_note_on`. Without BLASTER it tries the ARM-PC standard (A220 I7 D1
H5 T6) and says so; without a card it says "No Sound Blaster found". Exit code 0 when
both were found, else 1. Source: `sbtest.c` (ARM-DOS project; uses sdk `<sb.h>`).

## Tests

`make sbtest-test` (`tests/run.mjs`) builds a C: image (IO.SYS, ARMDOS.SYS,
COMMAND.COM, the real AUTOEXEC.BAT, SBMIX.EXE, SBTEST.EXE), boots it headless with the
machine's audio recorded, and checks: `SET` shows `BLASTER=A220 I7 D1 H5 T6`; SBMIX
/INIT left master/voice/FM at 0 dB and `SBMIX FM=24` draws its bar; the detection
line; the OPL3 timer detection; the chime's two notes at 1318.5 and 1046.5 Hz in the
recorded audio, silence before it; the chord's three fundamentals each > 15 dB above
the spectrum between them, on both channels, released afterwards. The recording is
`build/sbtest-test/sbtest.wav`. `tests/audio.mjs` (image builder, audio capture, WAV,
spectral helpers) is shared with apps/doom/tests/sound.mjs.
