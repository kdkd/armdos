# Modem, phone line and BBS — design notes

* Every ARM-PC gets a **16550A UART on COM2 (0x2F8-0x2FF, IRQ 3 -> INT 0Bh)** with an
  internal **Hayes-compatible 2400 bps modem** behind it (the COM1 port at 0x3F8 stays the
  serial console). Full 16550 behaviour: DLAB/divisor latch, IER/IIR/FCR/LCR/MCR/LSR/MSR/SCR,
  FIFO (16 bytes, trigger levels), interrupts (RX data, THR empty, line status, modem status),
  MSR bits CTS/DSR/RI/DCD and their delta bits, MCR DTR/RTS/OUT2 (OUT2 gates the IRQ as on a PC).
  BIOS: INT 14h works for COM2 too (DX=1), and 40:02 = 2F8h.
* Modem: Hayes AT command set (AT, ATZ, AT&F, ATE0/1, ATV0/1, ATQ, ATX4, ATH0/1, ATA, ATO,
  ATD[T|P]number, ATS0=n auto-answer, ATSn?, S-registers 0-12 incl. S2 escape char, +++ with
  guard time, ATM0/1/2 speaker control, ATL volume, AT&C1/&D2), result codes OK, CONNECT 2400,
  RING, NO CARRIER, ERROR, NO DIALTONE, BUSY, NO ANSWER (numeric forms with ATV0). Data rate
  paced to 2400 bps (240 chars/s) in emulated time by default — the slowness is part of the charm;
  `AT&B`, `ATS37` and the page's speed switch choose a faster line (below).
* **Phone line**: a PhoneExchange object shared by every machine on the page (and in node
  tests). A modem dialling a number rings the modem registered under it (RI toggles, "RING"
  result codes); the callee answers (ATA or S0 auto-answer); both get CONNECT and bytes flow
  between the two UARTs at the line rate; hang-up (ATH, DTR drop per &D2, or carrier loss) gives
  NO CARRIER on the other side. Numbers:
  * **555-1989** — "The ARM Pit BBS": a second ARM-PC running ARM-DOS and a WildCat!-style BBS
    program (apps/bbs), booted in a Web Worker (browser) or as a second Machine (node).
  * **555-0100** — the **Host Link**: an endpoint implemented in JS by the page, answering like a
    modem and offering a tiny menu plus ZMODEM send/receive to/from the visitor's real computer
    (browser file picker / download).
  * anything else — NO ANSWER after ringing (or BUSY for a couple of joke numbers, e.g. 867-5309).
* **Sounds** (page, WebAudio, through the modem's speaker per ATM/ATL): off-hook click, US dial
  tone (350+440 Hz), DTMF digits (standard row/column pairs, ~70 ms per digit), ringback (440+480 Hz,
  2 s on / 4 s off), answer tone (2100 Hz), and the 2400 bps V.22bis handshake noise (scrambled
  data bursts ~1200/2400 Hz carrier), then silence once connected (ATM1). Pulse dialling
  (ATDP) clicks.

## How TERM.EXE and BBS.EXE drive the modem

* TERM.EXE and BBS.EXE program the 16550 directly: divisor 0x0030 (2400) via DLAB, LCR=03h (8N1), FCR=C7h (FIFO on,
  clear, trigger 14) — then FCR=87h, trigger 8 —, hook INT 0Bh, unmask IRQ3 (port 21h bit 3),
  MCR=0Bh (DTR+RTS+OUT2), IER=0Fh (RX data, THRE, line status, modem status). The ISR loops on IIR
  until bit 0 set, drains RBR while LSR bit 0, fills THR up to 16 bytes from a TX ring when THRE,
  reads MSR on a modem-status interrupt (DCD 0x80 = carrier, RI 0x40, TERI 0x04, DDCD 0x08), sends
  EOI (20h to port 20h). Carrier detection = MSR bit 7. Hang-up = drop DTR (MCR bit 0) for ~500 ms
  (relying on &D2), then "+++" / ATH0 as a fallback if DCD stays up.
* TERM sends `ATZ\r`, then its init string `AT&C1&D2\r` (it waits for OK after each, 3 s
  timeout), then `ATDT5551989\r`. Then it waits up to 45 s for CONNECT 2400 /
  CONNECT / BUSY / NO CARRIER / NO ANSWER / NO DIALTONE (verbal result codes, ATV1, ATQ0).
* BBS sends `ATZ\r`, `ATE0V1Q0S0=0&C1&D2\r`, waits for `RING`, then sends `ATA\r` and waits for
  `CONNECT`: it relies on `\r\nRING\r\n` at every ring even with S0=0, and on ATA answering. On NO CARRIER / DTR drop
  it re-sends ATZ and the init string.
* BBS goodbye: it drops DTR; the caller's TERM should then see DCD fall and "\r\nNO CARRIER\r\n".
* Doors: the BBS keeps DTR up while it EXECs a door (the door opens COM2 itself, same programming,
  and must not drop DTR on exit). If the modem sees MCR reprogrammed with DTR still set, the line
  must stay up.

## The modem and the phone exchange (emu/dev/uart16550.mjs, emu/dev/modem.mjs, emu/phone.mjs)

* Built for the programming sequence above; it also works from ARM-DOS itself
  (`ECHO ATDT5551989>COM2` / `COPY COM2 CON`, through the kernel's COM2 device and BIOS INT 14h DX=1).
* **Node tests**: every `Machine` has `machine.com2` (the 16550A) and `machine.modem`.
  ```js
  import { boot } from '../../../emu/testkit.mjs';
  import { PhoneExchange } from '../../../emu/phone.mjs';
  const ex = new PhoneExchange();                     // or new PhoneExchange({ lineRate: 115200 })
  const a = await boot({ rom, hd: hdA, phone: ex, phoneNumber: '555-2000' });
  const b = await boot({ rom, hd: hdB, phone: ex, phoneNumber: '555-1989' });
  // (or: a.machine.modem.attach(ex, '555-2000'))
  for (...) { a.run(10); b.run(10); }                 // run both in lock step, small slices
  ```
  Each machine keeps its own emulated clock; run them alternately in slices of ~5-20 ms.
* **Faster for tests**: `new PhoneExchange({ lineRate: 115200 })` forces every call to that line
  rate (result code `CONNECT 115200`), and `machine.com2.baudOverride = 115200` makes the UART's
  wire run at that rate whatever divisor the program set (the 16550 otherwise paces characters at
  the programmed DTE rate — divisor 0x30 = 2400 bps = 240 chars/s). Both are needed to go
  fast. For the visitor, `AT&B9600` (or `ATS37=9`) asks for a faster line; the far end follows.
* **Behaviour**
  * Dial strings: `T P , W ! ;` honoured, everything else that is not a digit (`- ( ) space`) is
    ignored; `ATDL` redials. `ATDT555-1989` works.
  * `\r\nRING\r\n` at every ring (2 s ring, 4 s pause; the first ~0.3 s after the call arrives),
    RI (MSR bit 6) high during the 2 s ring, TERI on its trailing edge, S1 counts rings. With
    S0=0 it rings until the caller gives up (caller's S7, default 30 s). `ATA` answers.
  * From answer to `CONNECT 2400`: ~4.7 s (billing delay 0.4 s, 2100 Hz answer tone 2.4 s,
    V.22bis training 1.9 s) on both ends. DCD (MSR bit 7) rises exactly when CONNECT is sent.
  * Hang-up: DTR falling edge with &D2 (factory default here is **&C1 &D2**) hangs up at once and
    sends `\r\nNO CARRIER\r\n`; the other side sees DCD drop and `NO CARRIER` after S10 (1.4 s).
    Only a DTR *falling edge* matters, so re-writing MCR with DTR still set keeps the line up (doors
    are safe). While DTR is low with &D2 the modem does not auto-answer (S0>0), but `RING` is still
    reported and commands are still accepted.
  * `+++` needs 1 s of silence before and after (S12=50); the `+` characters are also transmitted.
    `ATO` returns online; line data received in between is held and delivered after `CONNECT`.
  * A character sent while dialling/training aborts the call (`NO CARRIER`) — except within
    150 ms of the command's CR, so a CR LF line ending is fine.
  * The internal modem never overruns the receive FIFO: if it is full, the next character waits
    (so an ISR that is late loses nothing). CTS and DSR are always on (unless AT&K3, then CTS
    follows the far end's buffer and RTS low holds received data).
  * Result codes: `OK CONNECT RING NO CARRIER ERROR NO DIALTONE BUSY NO ANSWER`, `CONNECT <rate>`
    at X1-X4 (X4 default), numeric with V0 (0 1 2 3 4 6 7 8, 10 = CONNECT 2400, 12 = 9600).
    Unknown numbers ring until S7 -> `NO ANSWER`; 867-5309 is `BUSY`; a Machine with no
    `phone` option has no line: `NO DIALTONE`.
* The **page**: the visitor's machine is 555-2000 (not that anyone can call it); the BBS runs in
  a Web Worker from `build/bbs.img` (+ build/rom.bin) as 555-1989, booted on the first call to
  that number: the caller hears it ring while it boots, and BBS.EXE comes up quickly after boot
  (AUTOEXEC.BAT) and answers with ATA. It keeps running. The page can show
  its screen in a small "sysop view". If build/bbs.img does not exist, 555-1989 rings unanswered.

## Tests and the BBS image

* `apps/term/tests/run.mjs` and `apps/bbs/tests/run.mjs` boot with
  `{ phone: ex, phoneNumber }` on one `PhoneExchange` (tests/lib.mjs taps
  `machine.com2.receive` to see what the caller's UART gets). Everything passes at the real
  2400 bps pacing: dial + RING + ATA + CONNECT, the full BBS session, ZMODEM/YMODEM both ways,
  the door (EXEC with DTR up), goodbye/DTR drop -> NO CARRIER, Alt-H, BUSY/redial on 867-5309,
  and TERM against the **Host Link** (emu/hostlink.mjs): ZMODEM download by auto-start, Esc
  abort, resume of the partial file (our receiver sends ZRPOS at the partial length; the Host
  Link's ZMODEM sender resumes), ZMODEM upload into `saveFile`.
* `make bbs-image` (also part of `make all`) builds **build/bbs.img**. AUTOEXEC.BAT runs
  `C:\BBS\BBS`; it reaches "Waiting for call" (ATZ + `ATE0V1Q0S0=0&C1&D2` done) ~2.7 s of
  emulated time after power-on, and a call that was already ringing is answered at the next
  RING (tested by handing a ringing leg to a freshly booted machine's `modem.incoming`).
* After a carrier loss the BBS purges its transmit ring before ATZ, so no stale session text
  reaches the modem's command parser.
* TERM's dialing directory: 555-1989 BBS, 555-0100 Host Link, 867-5309 Jenny (BUSY),
  399-2364 WOPR and 555-0142 (both ring out -> NO ANSWER after S7).

## Speeds up to 56K, and the sound of it

* **The speed switch.** The modem's front panel on the page has a four-position slide switch,
  **2400 / 14.4 / 33.6 / 56K** (localStorage `armdos.prefs.modemSpeed`, default 2400). It is the
  card's `modem.switchRate` (`Machine({ modemRate })`, `modem.setSwitch(r)`): the rate `S37=0`
  (the factory setting) trains at. Per call, `AT&B<bps>`, `ATS37=n` and `AT+MS=` override it,
  up or down. Both ends ask for their rate and **the slower one wins**. A bare `Machine` still
  defaults to 2400, so every existing test behaves as before.
* **Modulation per rate.** 300: Bell 103 FSK. 1200: V.22. 2400: V.22bis. 4800-14400: V.32bis.
  16800-33600: V.34. 56000: V.90. V.90 is asymmetric, with the answering side as the "digital"
  server: **53,333 bps down to the caller, 31,200 up**. Each direction is paced separately
  (`modem.rxRate` / `modem.txRate`, `leg.rxRate` / `leg.txRate`, `lineRates()` in phone.mjs).
* **Answer to CONNECT** (both ends, emulated time):

  | rate | sequence | answer -> CONNECT |
  |---|---|---|
  | 2400 | V.25 2100 Hz answer tone, then V.22bis | ~4.7 s |
  | 14400 | V.25 answer tone, then V.32bis | ~6.5 s |
  | 33600 | V.8 (ANSam, CM/JM), then V.34 phases 2-4 | ~8.6 s |
  | 56K | V.8bis, then V.8, then V.34 phases 2-3, V.90 DIL, phase 4 | ~18.3 s |

  After the far end answers, the caller waits for carrier as long as its training needs (+5 s),
  whatever S7 says.
* **Result codes.** `CONNECT 2400`, `CONNECT 14400`, `CONNECT 33600`, `CONNECT 56000` (ATW0,
  the default). With **ATW1/ATW2** the modem reports the rate it receives and the modulation:
  `CONNECT 2400/V22BIS`, `CONNECT 14400/V32BIS`, `CONNECT 33600/V34`, `CONNECT 53333/V90`.
  Code that does `atol(line + 7)` gets the right number either way. Numeric codes (V0) above
  14400 are `1` (CONNECT).
* **Commands.**
  * `AT&B<bps>` takes 300 1200 2400 4800 7200 9600 12000 14400 16800 19200 21600 24000 26400
    28800 31200 33600 56000.
  * S37 codes: 3=300 5=1200 6=2400 7=4800 8=7200 9=9600 10=12000 11=14400, extended with
    12=16800 13=19200 14=21600 15=24000 16=26400 17=28800 18=31200 19=33600 20=56000; 0 = the switch.
  * `AT+MS=<mod>[,<auto>[,<min>[,<max>]]]` takes names (V21 V22 V22B V32 V32B V34 V90) or V.250
    codes (0 1 2 9 10 11 12). End it with `;` if more commands follow: `AT+MS=V34;DT5551989`.
  * `AT+MS?` answers e.g. `+MS: 12,1,300,56000`.
  * `AT&V` shows the switch and the modulation.
* **The port must not be the bottleneck.** The 16550 still sends characters at its divisor's rate,
  so a program that sets 2400 bps gets 240 chars/s whatever the line does, exactly like the real
  thing. Real fast-modem setups lock the port at 57600/115200, and so do TERM and the BBS:
  * `apps/term/term.c`: the default port speed is 115200 (the setup menu cycles 300 … 115200).
    A dialling-directory entry's baud never *lowers* the port (old TERM.DIR files say 2400).
    The status line shows the port speed, "115200 N81".
  * `apps/bbs/data/BBS/BBS.CFG`: `BAUD=115200` and `INIT=…&B56000`, so the BBS answers at
    whatever the caller trains at, up to 56K.
  * `apps/bbs/misc.c`: "Who's online" prints the caller's rate instead of the port's
    ("You are on at 53333 bps").
  * `GETKEEN.SCR` is untouched and still connects at 14400 (`&B14400`, port 19200); the Keen
    test passes.
* **The sounds** (`emu/modemsound.mjs`, played by web/js/modem-audio.js): everything is generated
  procedurally at 8 kHz from the actual line signals, with no samples.
  * **Mixing:** both directions are mixed as the *calling* modem's speaker hears them, with the
    near end about 3 dB louder, a faint 18 ms hybrid echo and line hiss, then band-limited to the
    telephone band (high-pass 280 Hz, low-pass 3.5 kHz).
  * **Tuning:** the V.90 sequence was compared with a recording of a real 56K call (used as a
    reference only, not shipped), spectrogram side by side, phase by phase, and each phase's
    level set to the measured one (within ~2 dB relative to the TRN hiss). The annotated
    spectrogram is
    `docs/modem-v90-spectrogram.png`; re-render it with
    `node emu/tests/modemsound/render.mjs <dir>` and
    `emu/tests/modemsound/spectrogram.py <dir>/V90.wav <dir>/V90.phases.json out.png`.

  | phase (V.90) | reference (s into the call) | ours (s after the answerer goes off hook) | what it is |
  |---|---|---|---|
  | V.8bis CRe | ends 0.14 | 0.20-0.70 | 1375+2002 Hz dual tone, then 400 Hz |
  | V.8bis MRd | 0.17-0.66 | 0.73-1.22 | 1529+2225 Hz, then 1900 Hz |
  | V.8bis messages | 0.72-2.63 | 1.28-3.19 | V.21 low (980/1180 Hz, 300 bit/s), high (1650/1850), low: CL/MS/ACK |
  | silence | 2.65-3.53 | 3.19-4.09 | |
  | ANSam | 3.53-5.68 | 4.09-6.24 | 2100 Hz, 15 Hz AM ±20 %, 180° phase reversal every 450 ms (the "snaps") |
  | V.8 CM | 4.71-6.75 | 5.27-7.31 | V.21 low under the ANSam, starting ~1.2 s into it |
  | V.8 JM | 5.68-6.75 | 6.24-7.31 | V.21 high once the ANSam stops |
  | INFO0c/a, tones A/B | 6.82-7.1 | 7.38-7.66 | DPSK 600 bit/s on 1200 / 2400 Hz, then tones 1200 + 2400 (+1800) Hz |
  | L1/L2 line probing, x2 | 7.1-7.45, 7.56-7.9 | 7.66-8.01, 8.12-8.46 | comb of tones every 150 Hz from 150 to 3750 Hz (900, 1200, 1800, 2400 left out); L2 6 dB below L1: the "ding-ding" chord bursts |
  | INFO1c / INFO1a | 8.0-8.28 | 8.56-8.84 | DPSK bursts |
  | S / S-bar | 8.37-8.5 | 8.93-9.06 | line spectra (250, 1800, 3450 Hz …) |
  | phase 3 TRN | 8.5-10.9 | 9.06-10.76 | scrambled QAM, 3429 baud: the first "shhhh" (band ~150-3750 Hz) |
  | V.90 PCM training | 10.9-15.15 | 10.76-13.66 | full-band hiss (random PCM codes + the upstream QAM) |
  | V.90 DIL | 15.15-17.1 | 13.66-15.63 | periodic in **144 samples (18 ms)**: comb lines every 55.6 Hz with a 56 Hz buzz, getting louder (measured on the reference: lines 55.6 Hz apart, envelope ripple 56 Hz) |
  | Jd/Ja + 1335 Hz tone | 17.1-17.55 | 15.63-16.05 | |
  | phase 4, data | 17.55-20.05 | 16.05-17.9 | the final hiss; the speaker goes off at CONNECT (ATM1) |

  The only deliberate difference is the length: the two long hiss phases are 2.3 s shorter
  (17.9 s instead of 20.1 s). V.34 is the same V.8 + phase 2/3 without V.8bis, PCM or DIL (8.2 s).
  V.32bis has AA 1800 Hz, the AC/CA 600 + 3000 Hz alternation, S/TRN, R and TRN (3.7 s). V.22bis
  has USB1 (a steady 2250 Hz), S1, scrambled DPSK and 16-QAM (1.9 s).
  `emu/tests/modemsound/check.mjs` checks the V.90 structure (phase order, the tones of each
  phase, the comb, the DIL period, the band limit).

## One release per browser, and the BBS welcome

* **Module versions:** a page and its BBS worker must never mix modules of two releases (a
  worker running an older `emu/dev/modem.js` out of the HTTP cache would answer every call at
  the old rate). So every relative module import in `js/` and `emu/`, and the BBS worker's URL,
  carries `?v=<hash of the emulator + page sources>` (web/tools/build-site.mjs). The service
  worker's shell list includes those exact URLs, so offline start still works.
* **BBS welcome screens:** "Now with a digital line! 56K V.90 callers welcome (2400 still works,
  too)". The header reads "V.90 56K" (`tools/mkans.py`, WELCOME.ANS, WELCOME.TXT). A real BBS on
  an analog line could not be a V.90 server; here it is.
* **TERM / BBS counters:**
  * `TICKS()` (`apps/term/lib/comm.h`, shared with the BBS) is monotonic across midnight
    (the BIOS count at 40:6C restarts at 0 after 1800B0h ticks).
  * The CPS math is 64-bit: bytes × 182 overflowed 32 bits past 11.8 MB, giving "CPS: -828".
  * The online and elapsed timers are 64-bit too.
  * TERM's test checks that the online timer keeps counting across midnight. Its directory-date
    assertion now also accepts the next day, because that check lets the clock roll over.

## The other numbers, WOPR, and a terminal that talks

* **Numbers that answer** (`emu/phonelines.mjs`; the page registers them, and so do the node
  tests). Each is a small scripted endpoint with its own clock (`tick(ms)`):

  | number | who | what happens |
  |---|---|---|
  | 399-2364 | **WOPR** | the WarGames homage (below); trains at the caller's rate, types slowly, speaks |
  | 555-0142 | Europa Micro Support | a PBX answers by voice: Greensleeves on hold in the modem speaker, then a modem picks up with "Your call is important to us" (spoken), 47 minutes' wait, goodbye |
  | 767-2676 | Time and Temperature (POPCORN) | "AT THE TONE, THE TIME WILL BE ..." from the browser clock, spoken, BEL, a fake temperature, three times, click |
  | 555-3299 | Europa's fax machine | answers by voice with CED (2100 Hz) and the V.21 HDLC flags/DIS screech; the calling modem never gets a carrier: NO CARRIER |
  | 555-0386 | The 386 Fortress | one snooty ANSI screen: "ARM users are not welcome here" |
  | 555-7734 | The Floating Point | Doc Mantissa's pun |
  | 555-0123 | a wrong number | "Hello? ...Hello?" (spoken) and a click |
  | 011-7-095-231-1984 | **KREMVAX** | international: a longer silence while the trunk routes (hiss, clicks), then the Soviet/European ring (one ~425 Hz tone, 0.8 s on / 3.2 s off), CONNECT 1200 "over the cable" with line noise between words, a VAX/VMS-style banner, any login is GUEST ("Privet!"), NEWS winks at the 1984 April Fool and the 1990 hostname, `%SYSTEM-F-TIMEOUT` |
  | 867-5309 | Jenny | BUSY |

  They are listed in the BBS bulletin "Other boards worth a call" (BULLET4.TXT) and in
  TERM's dialling directory (TERM.DIR + its built-in default).
* **The phone line gained** `leg.voice()` (the far end picks up without a modem: the caller's
  ringback stops and its speaker keeps playing what the line sends) and `leg.lineSound({ sound:
  'hold' | 'fax' | 'routing' | 'pickup', ms })`. The page plays these through the modem speaker
  (`lineSignal()` in emu/modemsound.mjs, procedural). `normalizeNumber` keeps `011…`
  international numbers whole, and the exchange tells the caller's modem the call is
  international (`onRinging({ international })`).
* **WOPR (399-2364).** After CONNECT, a long silence, then `LOGON:`.
  * **Logon:**
    * `HELP GAMES` gives the definition and the game list; `LIST GAMES` gives the list; any other
      `HELP` gives "HELP NOT AVAILABLE".
    * A wrong logon gives "IDENTIFICATION NOT RECOGNIZED BY SYSTEM / --CONNECTION TERMINATED--"
      and a hang-up.
    * `7KQ201 MCKITTRICK` gets a priority greeting, then "not authorized for game control".
  * **JOSHUA** opens the backdoor with a burst of port-status noise, including "WARNING: IMSAI
    8080 NOT DETECTED. ARM926EJ-S ACCEPTED UNDER PROTEST.", then "GREETINGS, PROFESSOR FALKEN."
  * **Conversation** (keyword matching, upper case):
    * the opening: HOW ARE YOU FEELING TODAY?, then SHALL WE PLAY A GAME?
    * MISTAKE → YES, THEY DO.
    * GAME OR IS IT REAL → WHAT'S THE DIFFERENCE?
    * PRIMARY GOAL → YOU PROGRAMMED ME, then (asked again) TO WIN THE GAME.
    * STILL PLAYING → DEFCON 1 IN 28 HOURS / KILL RATIOS
    * GLOBAL THERMONUCLEAR WAR → WOULDN'T YOU PREFER A GOOD GAME OF CHESS?, then FINE.
    * CHESS → I'm still thinking about my opening
    * other games → not available, IMSAI 8080 required
    * TIC-TAC-TOE → NUMBER OF PLAYERS:
  * **Global Thermonuclear War:**
    * The war room: the screen framed with GST / TEP / SIM / TTG in the corners and GAME TIME
      ELAPSED / GAME TIME REMAINING clocks (redrawn with ESC 7/8); the dialogue scrolls inside
      a scroll region.
    * WHICH SIDE DO YOU WANT?, AWAITING FIRST STRIKE COMMAND, primary targets.
    * The launch order: the film's coordinates, with DLG2209TVX rejected as a test code.
    * DEFCON 4-3-2, while it still answers "is this a game or is it real".
    * SEARCHING FOR LAUNCH CODE: CPE1703TKS found one character at a time.
    * DEFCON 1, RUNNING FINAL SIMULATIONS, then the climax. Typing TIC-TAC-TOE during the code
      search jumps straight to the climax.
  * **Tic-tac-toe:**
    * 1 player: you are X against perfect play (minimax); you can only draw.
    * 0 / ZERO / PLAY YOURSELF: the climax. It plays itself, faster and faster, then flashes
      every scenario in the film's list (its misspellings included) with WINNER: NONE. The first
      few get a small world map with missile arcs, as many as the line rate allows (one at
      1200 bps, about 40 at 56K). Then: GREETINGS, PROFESSOR FALKEN. / A STRANGE GAME. / THE
      ONLY WINNING MOVE IS NOT TO PLAY. / HOW ABOUT A NICE GAME OF CHESS? --CONNECTION
      TERMINATED--.
* **The speech escape** (the private terminal extension):

      ESC P speak[:<voice>] ; <text> ESC \          (a DCS string; BEL also ends it)

  A DCS string is swallowed by a VT100 and by TERM's VT emulator (`lib/vt.c` now parses DCS and
  hands it to a `dcs` callback), so terminals that don't know it show nothing. **TERM** speaks
  `<text>` through the Sound Blaster with DRARM's TTS engine (apps/drarm, linked in) while the
  other end prints the same text at speaking pace.
  * Voices: default, and `wopr`: monotone (the new `tts_monotone` flag; f0 flat at ~103 Hz,
    measured), a little high, slow.
  * Utterances queue (1.5 KB); the Sound Blaster is claimed on the first utterance and released
    after 15 s of silence. It is never the PC speaker, which would take the timer.
  * **Alt-V** turns speech off/on.
* **TERM's menus run commands:** in the Alt-Z menu, pressing an Alt-command (Alt-D, Alt-S, Alt-X,
  PgUp, …) runs it directly (Procomm style). The dialling directory does the same for any
  command except Alt-D.
* **TERM's size:** TERM.EXE takes 320 KB of conventional memory when running
  (image + BSS + stack), about 100 KB of it the synthesiser, sized for TERM's short lines with `-DTTS_MAXSEG=360`
  (DRARM keeps 900), plus the speech ring and the VFP math library.
* **Reboot mid-call** (the BIOS drops DTR on Ctrl-Alt-Del): tested in `apps/term/tests/wopr.mjs`.
  In the middle of Global Thermonuclear War the modem hangs up at once and WOPR sees the line
  drop. After the reboot TERM starts "Offline" and the next call (the talking clock) works.
* **WOPR HELP**: `HELP`, `HINT`, `?` or `WHAT DO I DO` works wherever WOPR
  waits for you, with a nudge for that moment:
  * at LOGON: the J----- hint (and "(TYPE HELP AT ANY TIME)" the first time)
  * after GREETINGS: say hello or how you feel
  * at SHALL WE PLAY A GAME?: "SUGGESTION: LOVE TO. HOW ABOUT GLOBAL THERMONUCLEAR WAR? -- OR LIST
    GAMES, OR TIC-TAC-TOE", plus the famous questions
  * after the chess offer: "LATER. LET'S PLAY…"
  * at the side and target prompts: what to type
  * during the DEFCON steps and the launch-code search: a nudge towards tic-tac-toe
  * at NUMBER OF PLAYERS and YOUR MOVE: how to move, and the ZERO PLAYERS secret
  * at the final chess question: say yes or no

  `HELP GAMES` and `LIST GAMES` still show the list. `HELP` followed by anything else at LOGON:
  answers "HELP NOT AVAILABLE", as in the film.
