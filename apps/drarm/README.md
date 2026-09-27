# Dr. ARMitso - the talking psychologist (DOCTOR.EXE, SAY.EXE)

**Dr. ARMitso** ("ARM Intelligent Text-to-Speech Operator") is ARM-DOS's answer to
the talking-psychologist demo that came with the Sound Blaster in 1991: white
upper-case text on a blue screen, a flat, buzzy robot voice that speaks every
line as it appears, an ELIZA-style doctor, and a few easter eggs. Everything
here was written from scratch: the speech engine, the conversation rules and
every line the doctor says. Nothing is taken from Creative Labs' or First
Byte's programs, data or recordings.

```
C:\>DOCTOR [/PC] [/Q]          /PC  speak through the PC speaker   /Q  silent
C:\>SAY [/P:n] [/S:n] [/T:n] [/V:n] [/PC] [/E] [text]
C:\>SAY < LETTER.TXT           (no text: reads standard input, a sentence at a time)
```

`C:\DOS\DOCTOR.EXE`, `C:\DOS\SAY.EXE`, and `C:\DOS\ADVICE.TXT` (something for `.READ`).

## The session

1. The banner, and the doctor introduces himself: "DR. ARMITSO, BY EUROPA MICRO SYSTEMS."
   and "HELLO, WHAT IS YOUR NAME?"
2. **As you type your name, each letter is spoken** by name, the moment you type it
   (letters and spaces only, 25 characters). Only here - later on your typing is silent.
3. "HELLO *name*, MY NAME IS DR. ARMITSO." - the name goes through the plain
   letter-to-sound rules with no dictionary, so, as in the original, many names
   come out charmingly mangled (CHEKHOV becomes "CHEK-HUV").
4. His introduction, then the conversation. Each reply is revealed word by word in
   step with the voice (Esc cuts him short).

Commands (type them at the prompt): `HELP`, `SAY <text>` (he repeats it), `.READ <file>`
(reads a text file aloud), `.PHON <text>` (shows the phonemes, then says it),
`PITCH n`, `SPEED n`, `TONE n`, `VOLUME n` (0-9; without a number he tells you the
current value), `PARAM` (all four), `RESET`, `CLS`, `NEW` (a new patient), `QUIT`/`BYE`.

**Ctrl-C / Ctrl-Break** leave DOCTOR (and SAY) at once, mid-sentence or at the prompt:
the speech stops (the SB stream / PC speaker is shut down), the screen is reset as on QUIT,
INT 23h/1Bh are restored, and you are back at the DOS prompt (SAY prints `^C`).

## The doctor's mind (eliza.c)

An ELIZA-style engine: 54 ranked keyword rules ("I FEEL *", "MY MOTHER", "COMPUTER",
"SOUND BLASTER", "DREAM", ...), pronoun reflection (I/YOU, MY/YOUR, AM/ARE, ...), replies
chosen round-robin per rule so a line only comes back after the others, a memory of
what you said about "MY ..." brought back when the conversation stalls ("EARLIER YOU
MENTIONED YOUR JOB."), the doctor's deflections, and an occasional odd non-sequitur.
Special cases: an empty line, the same line twice, keyboard garbage (words without
vowels), and swearing. ARM-PC lines: "I SEE YOU ARE USING AN ARM926. HOW DOES THAT MAKE
YOU FEEL?", port 220h/IRQ 7 jokes, a cousin on the Intel side of the family...

**Swear at him three times** and his words decay into garbage as he stutters, the screen
turns into an **ARM DATA ABORT** dump (PC, FSR "parity error in patient data", FAR, r0-r15,
CPSR), he recovers ("Flushing caches ... Reloading psychology module ... OK") and
apologises for his breakdown. (The original's equivalent was a "parity error".)

If you mention wanting to die, he stops joking and tells you to talk to a real person.

## The voice (tts*.c)

A formant synthesiser in the spirit of the mid-80s SmoothTalker/Monologue voices,
built in C for the ARM (VFP floating point), 8-bit unsigned PCM at 11025 Hz:

* **Text normalisation** (`ttstext.c`): numbers (to the billions), ordinals, years
  (1991 = "nineteen ninety one"), money, percent, decimals, digit strings glued to
  letters ("ARM926" = "ARM nine two six"), abbreviations (DR., MR., ST., ETC, KB,
  MHZ...), vowel-less acronyms spelled out (IBM, CPU), symbols, and punctuation as
  phrase breaks.
* **Letter-to-sound** (`ttsnrl.c`): the ~300 context rules of the US Naval Research
  Laboratory's report NRL-7948 (Elovitz, Johnson, McHugh and Shore, 1976, "Automatic
  Translation of English Text to Phonetics by Means of Letter-to-Sound Rules" - a US
  Government work in the public domain), written out afresh from the report's method,
  plus a ~300-word exception dictionary (function words, the doctor's vocabulary, number
  words) and a simple stress rule (first full vowel of a content word).
* **Prosody** (`ttssyn.c`): Klatt-style durations (inherent/minimum per phoneme, unstressed
  and function-word shortening, shortening before voiceless consonants and in clusters,
  phrase-final lengthening) and a pitch model fitted to the reference recording's
  utterances: a gently declining baseline (86 Hz at PITCH 5; every excursion below scales with it); stressed syllables rise into
  the vowel to ~+13 Hz and fall back; statements "run out of breath" - the last three words
  step down one after another (about 0, -5 and -12 -> -19 Hz), the last word sinking to
  ~68-72 Hz; commas step down the same way but less; questions rise on the last accent to
  ~+60 Hz (140-150 Hz at the end); a spelled letter is a short fall from ~98 to ~89 Hz.
  Reference vs ours, per utterance: median 84-99 vs 84-93 Hz, accent peaks +10-20 Hz both,
  statement ends (last 200 ms) 69-74 vs 68-74 Hz, question ends +33-39 vs +40 Hz
  (build/drarm-test/pitch-compare.png).
* **Synthesis**: per 5 ms frame, targets interpolated between phonemes with locus-weighted
  transitions (stops, nasals and fricatives dominate the boundary; diphthongs glide).
  Source: an impulsive glottal pulse (the derivative of a short t^2(Te-t) flow pulse,
  open quotient 0.3, then a tilt filter set by TONE) plus aspiration noise. Cascade
  branch: nasal pole/zero pair and five formant resonators with broad bandwidths (every
  pulse rings and dies away - the clicky buzz of the old voices). Parallel branch: the
  frication noise through F2-F6 resonators with per-phoneme amplitudes and a bypass.
  Stops are closure (a voice bar for voiced ones) + burst + aspiration; affricates
  closure + frication. Output is quantised to 8 bits.
* **Playback** (`speak.c`): a continuous 8-bit auto-init DMA stream on the Sound Blaster
  (`sb_start` at 22050 Hz, each synthesised sample preceded by its linear interpolation, 1 KB halves), fed from an 8 KB ring the program fills a frame at a time
  while it waits, so speech starts at once and the words shown on screen follow the
  number of samples played. Without a Sound Blaster (or with `/PC`) it uses the PC speaker
  "RealSound"-style: PIT channel 0 is sped up to 11025 Hz for the utterance and its IRQ
  writes each sample as a one-shot pulse width on channel 2 (the BIOS 18.2 Hz tick is still
  delivered, every 607th interrupt).

**Tuning** was done against a recording of the original program (used only as a
reference, never shipped): its pitch track (~88 Hz baseline, stressed syllables jumping
to 100-110 Hz and settling, statement ends falling to ~70-75 Hz, questions rising), its
long-term spectrum of the voiced frames (steep tilt: -9 dB at 0.7-1.2 kHz, -23 dB at
2-3 kHz, -33 dB at 3-4 kHz relative to the 300-700 Hz band - ours is within ~4 dB in
every band), the waveform (a sharp pulse and a quickly damped ring per period), the
speaking rate (the doctor talks ~10% slower than the recording, on purpose:
"Our conversation will be kept in strict confidence" 3.6 s here vs 3.3 s) and the spelled-letter cadence. Settings are 0-9 like the
original's: pitch 5 = 86 Hz baseline, 0 = 60 Hz, 9 = 129 Hz (the whole contour scales with it).

**Fricatives** (v2, after listening tests): the frication noise is low-passed ((x+x1)/2),
S/Z/T/D/TH/DH use the 3.3-3.9 kHz resonators instead of the 4.75 kHz one, aspiration is 6 dB
softer and 25% shorter, and the whole output goes through a 2nd-order Butterworth low-pass at
3.8 kHz. Measured on the same sentences, the fricative frames' spectrum now peaks at
2.5-4.5 kHz like the recording's (4.5-5.5 kHz band: -22 dB of the fricative energy vs -9 dB
in the recording and -2 dB before), at -23 dB below the vowels (recording -21 dB), lasting
50-70 ms. An h between voiced sounds ("one hundred", "megahertz") is breathy voice
(voicing kept, breath at -10 dB, 50 ms) instead of a block of broadband noise
(build/drarm-test/hundred-before-after.png). Bandwidths and the nasal zero now crossfade over 10 ms at phoneme boundaries.

**Creak** (v7): the recording's pulses are not perfectly regular - successive pulses alternate
in strength, which puts subharmonics 25-27 dB below the harmonics (ours were 30-33 dB). The
source now makes alternate pulses ~0.7 dB weaker and ±0.4 % longer/shorter, a little more
below 80 Hz, plus ±3 % random shimmer: subharmonic ratio -24.5/-26.8/-27.7 dB (F0 <80, 80-95,
>95 Hz) vs the recording's -25.4/-26.6/-27.2. The recording has *less* energy below 300 Hz
relative to 300-3000 Hz than ours (-6 to -8 dB vs -1 to -2 dB), so no bass boost was added.

**Deviations**: it is a formant synthesiser, not SmoothTalker's (patented, diphone-style)
engine, so it sounds "like that kind of voice", not identical; the English rules mispronounce
plenty of words, as the originals did. The PC speaker path is written for real hardware:
the emulator's speaker model renders only square waves (PIT modes 2/3), so under emulation
the PC-speaker speech is silent (the test checks its timing and that the PIT and the BIOS
clock are restored).

## Files

| file | what |
|---|---|
| `doctor.c` | DOCTOR.EXE: screen, name entry, the session loop, commands, the crash gag |
| `eliza.c/.h` | the conversation engine |
| `say.c` | SAY.EXE |
| `tts.h`, `ttsint.h` | the speech engine's interface |
| `ttstext.c` | text normalisation, tokens |
| `ttsnrl.c` | NRL letter-to-sound rules, exception dictionary, stress |
| `ttssyn.c` | phoneme table, durations, pitch, the formant synthesiser |
| `speak.c/.h` | Sound Blaster stream / PC speaker playback |
| `tests/run.mjs` | `make drarm-test` |
| `tests/analyse.mjs` | pitch tracker, LPC formant tracks, used by the test |
| `tests/ttshost.c`, `tests/elizahost.c` | the engine and the doctor on the host (`gcc -o ttshost tests/ttshost.c tts*.c -lm`; writes a WAV) |

## Tests

`make drarm-test` boots ARM-DOS with the audio recorded and checks: SAY's speech is
there, median pitch 85-130 Hz, mostly voiced, 2.5-6 syllables/s, F1/F2 tracks present and
moving within vowel ranges, a question ends rising and a statement falling, /P moves the
pitch and /S the rate, numbers are expanded, SAY reads stdin, SAY /PC runs PIT channel 0 at
divisor 108 and restores it with the BIOS clock still at 18.2 Hz; DOCTOR's banner and
prompts, 7 spoken bursts for a 7-letter name typed a letter at a time, the greeting and the
spoken introduction, word-by-word reveal, a 13-line scripted conversation (keywords,
reflection, repeat, garbage, empty input, fallbacks, memory recall), all the commands,
the swearing -> ARM DATA ABORT -> recovery sequence, and QUIT. It leaves WAVs (SAY sentences,
pitch 2/8, the intro, the name being spelled) and screenshots in `build/drarm-test/`.

Credits: the letter-to-sound method and rule set - NRL report 7948 (public domain).
Behavioural notes on the original program (round-robin replies, repeat and garbage
handling, the swearing crash) were checked against Ralph Caraveo's MIT-licensed
"Dr. Sbaitso: Reborn" front end (github.com/deckarep/DrSbaitsoUi); none of its data or
Creative's text was used.

## The monotone voice (TERM speaks, docs/MODEM.md)

* `tts_monotone` (tts.h / ttssyn.c): when set, every vowel sits at the base pitch - no
  declination, stress or phrase contours. TERM's "wopr" voice uses it. Default 0.
* `TTS_MAXSEG`: the synthesiser's segment table size can be set at compile time. The default
  is still 900; TERM builds with 360 for its short lines (saves 47 KB of BSS).
