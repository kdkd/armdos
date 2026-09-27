# General MIDI on the ARM-PC: the MPU-401, its synthesizer, PLAYMIDI

The ARM-PC has a **Roland MPU-401 at 330h, IRQ 9** with a **General MIDI / GS
synthesizer** behind it - the Sound Canvas daughterboard experience of 1992 - next to
the Sound Blaster 16 (ARCH.md 4.3.1). `SET BLASTER=A220 I7 D1 H5 P330 T6` announces it.

| part | where |
|---|---|
| the MPU-401 (ports, UART mode, reset/ACK, IRQ 9) | `emu/dev/mpu401.mjs` |
| the synthesizer (SoundFont 2, GM/GS) | `emu/dev/gmsynth.mjs`, `emu/dev/sf2.mjs` |
| the sound set | `3rdparty/midi/ARMGS.SFA` (made by `tools/mksf.mjs` when tools/fetch-3rdparty.sh runs, below) |
| the page: lazy load, AudioWorklet, the MIDI switch | `web/js/audio-gm.js`, `web/js/audio-gm-worklet.js` |
| SDK | `sdk/include/midi.h` (`libdos/mpu.c`, `libdos/smf.c`) |
| PLAYMIDI.EXE | `playmidi.c` -> `C:\DOS\PLAYMIDI.EXE` |
| songs | `data/*.MID` -> `C:\MIDI\` (`hd.json`), `data/README.TXT` |
| DOOM, Duke Nukem 3D | `apps/doom/src/i_mpumus_armdos.c`; Duke's own `MPU401.C` (`apps/duke3d/src/audiolib/`) |

## PLAYMIDI.EXE

```
C:\>PLAYMIDI C:\MIDI\*.MID              the player screen, all eight songs
C:\>PLAYMIDI /L C:\MIDI\BACH846         loop until Esc
C:\>PLAYMIDI /Q C:\MIDI\TOCCATA > LOG   no screen: one line per file through DOS
```

A full-screen player in the style of the early-90s MIDI players (blue, double-line
frame): file, title (the first track name), format/tracks/PPQN, the device line
("MPU-401 at 330h, UART mode, IRQ 9"), tempo in BPM, time signature, bar:beat, time /
length and a position bar, and one row per MIDI channel with its instrument (GM names;
the kit name on drum channels, GS rhythm parts followed), volume, pan, the last note and
an activity meter (green/yellow/red, falls in 0.5 s, held notes keep half). Esc stops,
Space pauses (All Notes Off, time frozen), N or Enter skips to the next file. The DOS
screen and cursor come back at exit. Timing: the SDK's 1 kHz MIDI clock (PIT channel 0,
BIOS tick chained), WFI between events - the CPU is idle while a song plays. It sends GM
System On first. Without an MPU: `MPU-401 not found at 330h.` (exit code 2).
Wildcards, missing extension = .MID, `File not found` / `Not a MIDI file` per file.
Deviation: there was no single "PLAYMIDI" everybody had; this one is ours (the name
follows Creative's SB16 utilities).

## The synthesizer

`emu/dev/gmsynth.mjs` is a compact SoundFont 2 player written for this machine (no
vendored code): the SF2.04 generator model with FluidSynth's conventions (which is what
GeneralUser GS is voiced for): sample loops (modes 0/1/3), volume and modulation
envelopes with key scaling, both LFOs, the resonant low-pass filter, pan, reverb and
chorus sends, exclusive classes, the default modulators and the bank's own ones (override
and addition rules, all curve types, amount sources), EMU's 0.4 initial-attenuation
factor. MIDI: running status, notes, program change, bank select (GS variations, falling
back to the capital tone), channel pressure, pitch bend with RPN 0 range, RPN 1/2
tuning, CC 1/6/7/10/11/38/64/91/93/96-101/120/121/123-127, SysEx GM System On/Off,
GS Reset, GS "use for rhythm part", XG System On, master volume. 64 voices (released and
quiet voices are stolen first), linear interpolation, a Freeverb-style reverb and a
stereo chorus. Channel 10 is the drum kit. ~50 ms of host CPU per second at 64 voices.
Deviations from FluidSynth: CC 7 and CC 11 follow the GM/Sound Canvas volume curve
(40 log10) instead of the SF2 default's 80 log10 (DOOM sends CC 7 = 50 at its default
music volume: -16 dB as on an SC-55, not -32 dB).

In node (tests, `emu/headless.mjs --wav`) the synth mixes into the machine's audio
chunks directly; on the page it runs inside the Sound Blaster's AudioWorklet, fed the
MIDI bytes of each chunk (web/README.md "General MIDI"), and the ~6 MB sound set is
downloaded only when a program first uses the MPU-401. The page's **MIDI** switch turns
the MPU-401 off (programs then choose FM music).

## The sound set: ARMGS.SFA (from GeneralUser GS 2.0.3)

`3rdparty/midi/ARMGS.SFA` (6.0 MB, SHA-256 `983529c7...12abcc6`) is **GeneralUser GS v2.0.3** by
S. Christian Collins, reduced for the web by `tools/mksf.mjs`:

* every preset is kept: 128 GM programs, the GS variation banks, 13 drum kits;
* samples above 22,050 Hz resampled to ~22,050 Hz (Blackman-windowed sinc; a looped
  sample's new rate is chosen so its loop is a whole number of points and plays at the
  original pitch); continuous-loop tails removed;
* sample points stored as a 4.5-bit block ADPCM (chunk `smpB`: blocks of 16 points,
  8 second-order predictors x 25 step sizes; 32 dB SNR on this bank, IMA ADPCM was 24 dB);
  `emu/dev/sf2.mjs` decodes it at load (~0.1 s).

Source: `https://github.com/mrbumpy409/GeneralUser-GS`, `GeneralUser-GS.sf2` (32,319,396
bytes, SHA-256 `9575028c7a1f589f5770fccc8cff2734566af40cd26ed836944e9a5152688cfe`,
repository commit `97049183`, 2026-02-23), downloaded 2026-09-24. Rebuild:
`node apps/midi/tools/mksf.mjs GeneralUser-GS.sf2 3rdparty/midi/ARMGS.SFA` (~25 s).

**Licence** (`sf/GeneralUser-GS-LICENSE.txt`, "License v2.0", also in the file's ICMT
chunk and staged next to it on the site): "You may use GeneralUser GS without
restriction for your own music creation, private or commercial ... Please feel free to
use it in your software projects, and to modify the SoundFont bank or its packaging to
suit your needs." The samples "allow full use in music production"; the author notes he
cannot be 100% sure of every sample's origin (none from commercial sets). His request
"please do not link directly to my download files ... provide your own local copy" is
followed: the site serves its own modified copy. The modified bank says so in its INAM
and ICMT.

## The songs (C:\MIDI\)

All from the **Mutopia Project** (LilyPond engravings by volunteers), each marked
**Public Domain** by its typesetter ("placed in the public domain by the typesetter -
free to distribute, modify, and perform"). Downloaded 2026-09-24 from
`https://www.mutopiaproject.org/ftp/...`; originals in `data/src/`.

| file | piece | Mutopia id | typesetter | source .mid (SHA-256) |
|---|---|---|---|---|
| BACH846.MID | J.S. Bach, Prelude in C, BWV 846 | Mutopia-2011/09/12-5 | Tobias Erbsland | BachJS/BWV846/wtk1-prelude1 (`874e07d0...`) |
| TOCCATA.MID | J.S. Bach, Toccata and Fugue in D minor, BWV 565 | Mutopia-2011/09/11-1780 | Anonymous | BachJS/BWV565/ToccataFugue (`1aabd009...`) |
| BRANDBG2.MID | J.S. Bach, Brandenburg Concerto No. 2, I. Allegro | Mutopia-2009/06/13-1680 | Andy Vaught | BachJS/BWV1047/brandenburg_2, score.mid (`d02f5467...`) |
| NACHTMUS.MID | W.A. Mozart, Eine kleine Nachtmusik, I. Allegro | Mutopia-2007/01/01-900 | Anonymous | MozartWA/KV525/eine-kleine-nachtmusik-mvt1 (`f8b6dc99...`) |
| FURELISE.MID | L. van Beethoven, Fur Elise, WoO 59 | Mutopia-2015/08/18-931 | Stelios Samelis | BeethovenLv/WoO59/fur_Elise_WoO59 (`1c12c21c...`) |
| MTNKING.MID | E. Grieg, In the Hall of the Mountain King | Mutopia-2013/12/07-1888 | Coyau | GriegE/O46/Dans_l_antre_du_roi_de_la_montagne (`0c256a80...`) |
| ENTERTNR.MID | S. Joplin, The Entertainer | Mutopia-2016/11/25-263 | Chris Sawer | JoplinS/entertainer (`33e4e81e...`) |
| MAPLELF.MID | S. Joplin, Maple Leaf Rag | Mutopia-2011/11/13-23 | Chris Sawer | JoplinS/maple (`3dd712a8...`) |

`tools/prepmid.mjs` makes the disk copies: the track-0 name ("control track", as LilyPond
writes it) becomes the title, a copyright meta event records the provenance, and the two
files whose score set no tempo (LilyPond then writes quarter = 60) get one (Toccata 72,
Brandenburg 96). Notes, channels and programs are untouched (the tests check the note
counts).

## Tests

* `make midi-test`: `tests/synth.mjs` (MPU-401 ports; the synth through the machine: onset
  time, pitch, envelopes, levels, bend, sustain, drums, GS parts, bank fallback, voice
  stealing and speed, mixer MIDI volume, remote mode) and `tests/playmidi.mjs` (PLAYMIDI
  on ARM-DOS: the screen; every note-on at the MPU matches the file in order, key,
  velocity and time within 1 ms; the opening arpeggio in the audio's spectrum; pause,
  next, Esc restoring the screen; /Q redirected; no MPU).
* `make doom-gm-test` (`apps/doom/tests/gm.mjs`), `make duke3d-gm-test`
  (`apps/duke3d/tests/gm.mjs`): the games' General MIDI music.
* `web/tests/test_gm_audio.py` (Playwright): lazy load, the music at the worklet's output,
  the switch.
WAVs and screenshots go to `build/midi-test/`, `build/doom-test/gm/`, `build/duke3d-test/gm/`.
