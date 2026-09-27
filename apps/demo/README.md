# DEMO.EXE - "ARM-DOS 4.00: the demo"

A 1990-style intro in VGA mode 13h, installed in `C:\DEMO\` with `DEMO.NFO`
(`apps/demo/hd.json`). 31 KB. Original code and music.

    DEMO [/Q] [/P n] [/T s]     /Q no music, /P n start at part n (0-4), /T s quit after s seconds
    Space = next part, Esc = back to DOS

Parts: starfield + "EUROPA MICRO SYSTEMS presents ARM-DOS 4.00 the demo" (9 s), then in a
loop of 18 s each: plasma with palette cycling, rotozoomer (XOR texture with a gold
"ARM"), tunnel (400x250 angle/distance table with dithered depth bands, wandering
centre), copper bars with a wobbling "ARM/AT" logo. A sine scroller with a copper
gradient runs over everything and explains the tricks; palette fades between parts.

The inner loops are ARM assembly (`fx.S`):

* **LDM/STM** - the finished frame goes to A0000h in 2,000 LDMIA/STMIA pairs of 8
  registers (plus `fx_fill` for clears and bars).
* **Barrel shifter** - the rotozoomer packs u and v into one register (u in bits 31-16,
  v in 15-0), steps both with a single ADD and builds the texel address with
  `AND r4,r2,#0xFF00 / ORR r4,r4,r2,LSR #24`: 5 instructions a pixel. The tunnel adds its
  offset to `table << 16` and lets `LDRB r6,[r2,r5,LSR #16]` do the wrap-around.
* **Conditional execution** - the scroller: `MOVS r1,r1,LSL #1` moves each font bit into
  C, `STRBCS` stores only where it was set; 16 rows unrolled, no branches.
* **SMULBB** - 16x16 DSP multiplies scale the sines for the rotozoomer, scroller and bars.

Timing, as intros did it: the PIT is reprogrammed to 1120 Hz (divisor 1065); INT 08h
counts ticks, calls the music player every 16th (70 Hz) and chains the BIOS every
65536/1065 ticks so the clock keeps 18.2 Hz. Frames wait for the vertical retrace on
3DAh bit 3 (with WFI between polls, so the host idles), then upload the palette and copy.
Music: one PC speaker voice via PIT channel 2 - 70 Hz arpeggio chords (Am F C G), a
melody with delayed vibrato, kick (pitch sweep) and snare (noise).

On exit: speaker off, PIT back to 18.2 Hz, INT 08h restored, mode 03h, and

    2006 frames in 28.6 seconds: 70.0 frames per second.
    CPU: 21% of an ARM926 at 100 MHz.

(busy time measured with the PIT counter; with the Turbo off, 12 MHz: ~40 fps, ~73%).

Tests: `make demo-test` - screenshots of every part (`build/showcase-test/demo/`), colour
counts, 70 fps at 100 MHz, the music (tone changes and pitches seen by the speaker), the
BIOS clock still at 18.2 Hz, Esc restoring text mode, INT 08h and silence; at 12 MHz the
frame rate drops and the CPU share rises.
