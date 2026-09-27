# web/ - the ARM-DOS 4.00 page

The play page (live demo: https://kevin.day/armdos/): a drawn 1988 PC (CRT monitor,
system unit with power/reset/turbo, 3.5" drive, LEDs, 7-segment MHz readout), a
disk box, a dot-matrix printer on LPT1, a COM1 terminal and the ARM inspector.

```
make site        # -> build/site (index.html, css, js, fonts, docs, emu/*.js, images/*.gz, *.json)
make web-test    # site + Playwright checks (web/tests/test_site.py ...)
./build.sh       # everything, staged into public_html/ (see the top-level README.md)
```

The staged site is self-contained static files with relative URLs only, so it works
from any web server and under any sub-path (e.g. https://example.org/armdos/). To try
it locally, `python3 web/tests/serve.py build/site` serves it at
http://127.0.0.1:8000/armdos/ (the web tests use the same server). The manual
(`web/docs/`, plain HTML) is copied to `docs/`; the web fonts (`web/fonts/`, SIL OFL
1.1) to `fonts/`.

## Files

```
index.html            the page (all text lives here)
css/armdos.css        the machine is CSS gradients + tiny inline SVGs; sizes in cqw
js/main.js            wiring: power, reset, turbo, LEDs, loading, serial, downloads
js/display.js         WebGL CRT shader (curvature, scanlines, bloom, warm-up, off-dot); 2D fallback (?gl=0)
js/audio.js           WebAudio synthesis: speaker, PSU/fan, degauss, floppy, HDD, printer.
                      Fan, hum, spindle and flyback whine go through a separate "Ambient" gain, off by default
js/audio-sb.js        the Sound Blaster 16 + OPL3: machine.audio -> AudioWorklet (ScriptProcessor fallback) -> +9.5 dB -> master
js/audio-sb-worklet.js  its AudioWorkletProcessor: ~60 ms queue, drift absorption (drop / +-0.5% rate / pad)
js/audio-gm.js        the MPU-401's General MIDI synth: lazy sound-set load, the "MIDI" switch (Sound Canvas / off)
js/audio-gm-worklet.js  the synth (emu/dev/gmsynth.js) inside the SB worklet's scope, fed the MPU's MIDI per audio chunk
js/debug.js           Driver (RealtimeDriver + breakpoint/crash handling), Debugger (bp, step, INT log)
js/inspector.js       the inspector panel UI
js/memmap-panel.js    the inspector's MEMORY MAP: live execute/read/write heat map of the address space (emu/memmap.mjs)
css/memmap.css        its styles
js/elbow-panel.js     the inspector's ELBOW view: the x86 block running under ELBOW next to the ARM code made of it
js/elbow-probe.js     reads ELBOW's descriptor (ports FCh-FFh, ARCH.md 4.7) and blocks out of guest RAM
js/x86disasm.js       16-bit x86 disassembler (8086-386 real mode, x87), ndisasm syntax
css/elbow.css         its styles
js/printer.js         LPT1 -> green-bar paper, 8x8 font as ink dots, tear off = PNG
js/diskbox.js         disks.json -> disk box, drive A:, drag and drop, downloads
js/input.js           keyboard capture, pointer lock mouse, paste, phone keyboard + extra keys
js/storage.js         HD sectors in IndexedDB, CMOS in localStorage
js/hdstream.js        the streamed hard disk: chunk index, demand fetch, background prefetch, write overlay
js/fat.js             FAT12/16 reader + writer (port of disk/mkimage.mjs parts): list, read, write, 8.3 names
js/pckeys.js          on-screen PC keyboard (sticky Shift/Ctrl/Alt, typematic repeat) and the game pad (its JOY switch)
                      - its caps follow KEYB's layout (port F6h, ARCH.md 4.6): the 102nd key and AltGr appear, AltGr legends in small type
js/kbdlayouts.js      GENERATED (apps/keyb/tools/mklegends.mjs, from KEYBOARD.SYS): every KEYB layout's key legends per code page
js/joystick.js        the game port's joysticks: Gamepad API controllers and the pad's JOY mode -> machine.joy, the lamps
js/applayout.js       the app layout (home-screen app, ?app, phones in landscape) and its control bar
sw.js                 service worker template (build-site fills in the build id and shell list)
manifest.webmanifest  the installable app's manifest; tools/icons.mjs draws the icons at build time
js/files.js           the Files panel: browse A:/C:, click to download, drop host files to copy in
js/sevenseg.js        the red MHz display
js/openbox.js         "Open case": the chassis from above (motherboard, cards, SIMMs, J1, drives, PSU), parts bin, configuration sheet
css/openbox.css       its drawing around the SVG: the lid and its slide, the bin, the sheet, the app-layout sheet
js/cdrom.js           the CD-ROM drive in the case (tray, tray button, busy LED, headphone jack, volume knob), the CD-ROM box, disc data/audio loading
css/cdrom.css         its drawing (the drive, the tray, the jewel case)
disks.json            the disk box manifest
tools/build-site.mjs  stages build/site (called by web.mk)
tests/                Playwright checks + local server
screenshots/          reference screenshots
```

## How the page talks to the emulator

The staged site carries the emulator as `emu/*.js` (build-site copies emu/*.mjs and
rewrites the imports, so no server needs to know the .mjs MIME type; headless.mjs
and testkit.mjs are left out). Nothing runs until the power switch is pressed; the
ROM and disk images download (gzip, ~60 KB) in the background after first paint.

* `new Machine({...})` is created on the first power-on with the callbacks:
  `onSpeaker` -> audio (timed against emulated time per slice, `Sound.frameSync`),
  `onDiskActivity` -> LEDs + floppy/HDD sounds, `onDiskWrite` -> IndexedDB (HD) /
  "modified" mark (floppy), `onCmosWrite` -> localStorage, `onLeds` -> NUM/CAPS/SCRL,
  `onSerial` -> COM1 panel, `onPrint` -> printer. Later power cycles use `powerCycle()`.
* The Sound Blaster 16 and its OPL3 are rendered by the emulator in emulated time
  (`machine.audio`, emu/dev/audio.mjs) at the AudioContext's rate; `SbAudio.attach(m)`
  (audio-sb.js, called in createMachine, i.e. after the power switch's gesture) starts
  it and posts the chunks to the `armpc-sb` AudioWorklet, which plays them through a
  gain of 3 into `Sound.master` (so mute applies). The worklet waits for ~60 ms of
  audio, drops the oldest when > 180 ms is queued, nudges its read rate by 0.5% when
  the queue drifts, and fades to silence on underrun (pause, slow host). Power off
  flushes it. The PC speaker stays on its own synthesis in audio.js. No AudioContext
  or audio node exists before a user gesture.
* The FDC's recalibrate (command 3) has no callback, so main.js wraps
  `machine.fdc.command` to hear it (the POST's grind).
* `Driver` (debug.js) subclasses `RealtimeDriver`; its `onFrame` stats feed the
  inspector. The screen is drawn in the page's own rAF loop via `renderScreen`.
* Inspector hooks, installed only while needed: breakpoints use the interpreter's
  trace callback (`cpu.trace` + an instance `cpu.traceRec`; the JIT is switched off
  while any breakpoint exists and back on afterwards); the interrupt log wraps
  `cpu.exception` (SVC n = INT n, AH from r0) and `pic.ack` (IRQs) on the instances.
  With the inspector closed and no breakpoints nothing is wrapped.
* The MEMORY MAP (js/memmap-panel.js) attaches an `emu/memmap.mjs` MemActivity
  (`cpu.act`) only while its section is open, the inspector is open and the tab is
  visible; otherwise it detaches and the machine runs its plain code paths.
* Keys: `machine.keyDown/keyUp(e.code)` while the canvas has focus (Escape too; click
  elsewhere to release): by position, so DOS's layout is KEYB's (US without it). Windows' extra
  Ctrl press with AltGr is taken back, so AltGr reaches KEYB as AltGr. **"Follow my layout"**
  (the toolbar's `followKbdBtn`, "Keyboard: follow my computer's layout", `prefs.followHost`,
  off by default; js/input.js): a key whose `event.key` is one character is typed as that
  character - the key itself when DOS's layout gives the same character for it, else as
  Alt+keypad digits of its byte in the screen's code page (port F7h) - bypassing KEYB for
  printable keys; host dead keys and AltGr stay in the browser. Paste and the phone keyboard
  push onto `machine.typeQ` (characters the US keys lack go as Alt+keypad codes too).
  `tests/test_keyb.py` checks the relabelling and the follow mode.

## General MIDI (MPU-401)

The machine's MPU-401 (emu/dev/mpu401.mjs) runs in **remote** mode on the page: the MIDI bytes a
program writes travel, with their sample offsets, alongside the Sound Blaster chunks
(`onAudio(l, r, midi)` -> audio-sb.js `push` -> the `armpc-sb` worklet), and
`audio-gm-worklet.js` - a second module added to the same AudioWorkletGlobalScope - renders them
with `GmSynth` into the chunk before it is queued: sample-aligned with the SB in emulated time,
off the main thread. Nothing is fetched at load or power-on: the first MPU command or MIDI byte
(`onFirstUse`) adds the worklet module and downloads `images.gm` (3rdparty/midi/ARMGS.SFA,
~6 MB, staged by build-site with its licence as images/ARMGS-LICENSE.txt), which is parsed on
the main thread (dev/sf2.mjs) and transferred; MIDI that arrives before the module only sets
channel state. The **MIDI** toolbar switch (after Ambient; `armdos.prefs.gmSynth`) turns the
MPU-401 off (`machine.mpu.present = false`: programs fall back to FM at their next start).
Without AudioWorklet the ScriptProcessor fallback runs the synth on the main thread.
`tests/test_gm_audio.py` checks the lazy load, the music at the node's output and the switch.

## The CD-ROM drive (js/cdrom.js)

The emulator's ATAPI drive (emu/dev/atapi.mjs, secondary IDE, IRQ 15) is a 5.25" bay above
drive A:. build-site stages `build/cdrom/sampler93` (`make cdrom-disc`, apps/cdrom) as
`images.json` `cdroms[]` (the disc.json manifest; the data track as `images/cd/<id>/data.iso.gz`,
each audio track's `audio.opus` / `audio.mp3`; no WAVs). They live under `images/`, so the
service worker caches them when first used and never precaches them.

* Click the jewel case (or drag it onto the drive; the app layout has a CD button in its bar)
  to put the disc in: the tray comes out, the disc goes on, the tray goes in (`m.cdrom.insert`).
  Before the first power-on the disc waits and goes to `new Machine({ cdrom })`
  (`cd.machineOptions()`, which also sets `onCdrom` / `onCdActivity`). The tray button is
  `m.cdrom.trayButton()` (refused while a program has locked the drive).
* The data track is fetched when the disc goes in; until it has arrived a READ keeps BSY.
  An audio track is fetched and decoded only when the drive asks (`CdDisc` `audio.request`,
  when play reaches it or 30 s before): Opus, else MP3 (Safari), decoded by an
  `OfflineAudioContext` at 44.1 kHz and kept as 16-bit PCM, at most two tracks. Meanwhile the
  drive holds the play position and its LED flickers; the status line says "Loading audio track N".
* CD audio is rendered by the emulator into `machine.audio` (the SB16's CD input, scaled by the
  mixer's CD and master volumes), so it plays through audio-sb.js's worklet like the card.
  The headphone jack (click it) bypasses the mixer; the knob (drag, scroll, arrow keys) sets
  the headphone level (`m.cdrom.headphones`, `m.cdrom.knob`, remembered in prefs).
* LED: amber blink = data read, green = playing audio, flicker = waiting for audio to arrive.
* `make cdrom-web-test` (tests/test_cdrom.py): insert, `ECHO DEVICE=...ARMCD.SYS>>CONFIG.SYS`,
  reboot, ARMCDEX, `DIR D:`, CDPLAY plays track 2 (Opus fetched only then, samples reach the
  worklet, headphones louder), tray eject -> "Not ready reading drive D", re-insert.

## The streamed hard disk

build-site cuts `build/hd.img` into 256 KB chunks, gzips each, names it by its content hash
(`images/c/<hash>.gz`) and writes the index into `images.json` (`hd.chunkSize`, `hd.chunks`:
hash per chunk, `null` = all zeros, not stored). Unchanged chunks keep their URL across
releases, so browsers and the service worker keep them.

* `StreamedDisk` (js/hdstream.js) owns a full-size image and is the ATA device's sector source
  (`machine.ata.source`, emu/README.md): a READ of a missing chunk keeps BSY until it arrives
  (3 tries, 20 s each; offline fails at once), then completes; a failure is an ATA error, so the
  BIOS returns an error and DOS asks Abort, Retry, Fail?. The disk LED pulses while waiting; the
  drive sounds play only for real reads.
* Background prefetch starts 6 s after power-on: three chunks at a time in ascending order,
  only while no demand fetch is in flight and the tab is visible. "C: n% cached" shows in the
  C: status line.
* Written sectors (by DOS, by the Files panel, restored from IndexedDB) are an overlay: a chunk
  arriving later never overwrites them. Factory reset clears the overlay and re-fetches the
  chunks that held changes.
* The Files panel fetches the FATs and root directory before listing C:, a directory's
  clusters when it is opened and a file's when it is downloaded; "Download C:" fetches all.
* Measured (web/tests/test_stream.py, 5 Mbit/s + 100 ms): about 0.4 MB and 6 s from the power
  switch to `C:\>`; DOOM's title and demo after about 2 MB in total.

## Persistence

* Drive C: only changed sectors are stored, in IndexedDB `armdos` / `hdsectors`,
  keyed `[imageHash, lba]`. `imageHash` is the sha256 prefix of build/hd.img from
  `images.json`, so a new release starts clean (old versions' sectors are deleted).
  "Reset C: to factory" clears them (and the CMOS).
* CMOS battery RAM: localStorage `armdos.cmos`. Preferences: `armdos.prefs`.
* Floppies keep their contents for the session only (eject and re-insert keeps them);
  "Download A:" saves the current image.

## Host files (the Files panel)

* Click a file in the A: or C: listing to download it (reads are always safe).
* Drop files on the drive, on the listing, or use "Copy files in": they are written
  with `FatVolume.writeFile` (lowest free clusters, every FAT copy, directory grows
  if a subdirectory is full, `DiskFullError` before anything changes). Host names
  become 8.3 the DOS way: upper case, invalid characters to `_`, base cut to 8 and
  extension to 3, clashes end in a digit (`LONGFIL1.TXT`).
* A: while the machine runs: the diskette is ejected, written and re-inserted
  (a media change DOS sees). With no diskette in the drive the blank one goes in.
  Write-protected diskettes are refused.
* C: accepts files only while the machine is off; the written sectors go to the
  IndexedDB store like any other change to C:.
* A dropped `.img`/`.ima`/... of an exact diskette size is still used as a diskette.

## Phones, tablets and the home-screen app

* **Device keyboard**: "⌨ Keyboard" (toolbar; ⌨ in the app bar; tapping the screen on a
  touch device) focuses `#softKbd` synchronously inside the tap, which is what iOS needs
  to open its keyboard. The field is a real textarea parked 1px over the monitor
  (opacity .01, 16px text so Safari doesn't zoom, autocapitalize/autocorrect/autocomplete
  off, spellcheck false, enterkeyhint enter). It always holds a sentinel so Backspace
  fires. Input: cancelable `beforeinput` (insertText, insertLineBreak,
  deleteContentBackward, ...) is typed through the machine's typist queue; composition
  and anything uncancelable is recovered by diffing the value against the sentinel.
  `visualViewport` resizes scroll the monitor above the keyboard.
* **PC keys** (`js/pckeys.js`): pointerdown = make, pointerup/cancel = break, per
  pointer (multi-touch). Held keys repeat like a keyboard (480 ms, then every 70 ms).
  Shift/Ctrl/Alt latch when tapped (lit) and release after the next key; held while
  pressing another key they behave as ordinary modifiers. The **pad** layout (D-pad,
  FIRE=Ctrl, OPEN=Space, STRAFE=Alt, RUN=Shift, Esc/Enter/Y/N/Tab/1-7) has plain
  hold keys, no latching.
* **Layouts**: phones in portrait put the keyboard right under the monitor. The app
  layout (`html.app`) is used when launched from the home screen (display-mode
  standalone/fullscreen, `navigator.standalone`), with `?app`, or on a phone held
  sideways (the × in the bar leaves it for the session): a slim bar (power, turbo, reset,
  diskette menu, keyboards, sound, inspector), the screen as large as fits, the keys
  below it (portrait) or floating translucent over it (landscape; the pad splits to the
  sides like a handheld). Safe-area insets are padded.
* **Installable**: `manifest.webmanifest` (standalone, start_url `./?app`), icons drawn
  by `tools/icons.mjs` (180 apple-touch, 192, 512, 512 maskable), iOS web-app meta tags.
* **Releases and caching** (web/tools/build-site.mjs): everything the page loads (css, js,
  the emulator, fonts, icons) is staged into `r/<release id>/`, the id a hash of all of it,
  so every URL there is immutable and any cache (browser, CDN, service worker) may keep it
  forever. Disk images carry their hash in the file name (`images/rom.bin.<hash>.gz`,
  C: chunks `images/c/<hash>.gz`), the manual's stylesheet and pictures too. No URL has a
  query string (some CDN configurations won't cache those). `images.json` and `disks.json`
  are part of the release too, so the page and its disks always come as a pair. Only
  `index.html` (which names the release), `manifest.webmanifest` and the manual's pages
  change under the same name; a CDN may cache those briefly too, since whichever
  `index.html` a visitor gets decides everything else. `sw.js` and top-level copies of
  `images.json`/`disks.json` are kept for pages from before release directories.
* **Offline** (`sw-<release>.js`, from web/sw.js): the page registers its release's worker
  (a new name each release, so no cache can hand out an old one; `sw.js` is the same file
  for pages from before release directories). The page itself is network-first (4 s, then
  the installed release's copy); `r/<release>/` is precached under `armdos-app-<release>`,
  all or nothing: every file is hash-checked, and a failed attempt (cut off, or a site half
  way through an upload) leaves the old release installed and resumes next time. Once
  installed, the new worker takes over when the page running that release asks (no reload
  needed), and not while another ARM-DOS window is open. A worker leaves other releases'
  files to the browser: relaying them would keep it busy, and browsers switch workers only
  once the old one is idle. Disk images are cache-first in `armdos-data`, pruned to what
  the installed release's manifests name. `?nosw` skips it all. `web/tests/test_sw_update.py` covers updates, failures and the switch-over.

## Full screen, the POST seek, your own ISO

* **Full screen** (the ⛶ button, the monitor fills the display): the mouse is captured on the
  way in (still inside the click's user activation) and released on the way out. Inside,
  **Ctrl+Alt+M** toggles capture (the M never reaches DOS; Ctrl and Alt are released as
  usual), and a small bar at the top (mouse state, the hint, Exit full screen) appears when
  entering, when capture changes, or when the free pointer moves, then fades.
* **POST floppy seek**: the first FDC recalibrate after power-on (bios/post.c
  detect_devices) plays `Sound.fdPostSeek`: the head stepping out and back in twice, about
  half a second. Later recalibrates are a short step out. No emulator hook was needed:
  main.js already wraps `machine.fdc.command`.
* **Your own ISO as D:** "Choose ISO…" in the CD-ROMs box, or drop an `.iso` anywhere on the
  page. The primary volume descriptor is checked (anything else gets a friendly refusal),
  and the file is served to the ATAPI drive through its streamed data-track interface by
  `LazyFileImage` (cdrom.js): 64 KB blocks read from the File on demand, at most 64 MB
  kept, nothing uploaded, so a 700 MB image costs little memory. Data only (one track);
  BIN/CUE isn't supported.

## Adding disks

Add an entry to `web/disks.json`:

```json
{ "id": "games", "label": "Games", "sub": "Volume 1", "file": "build/floppy-games.img",
  "description": "One sentence for the tooltip.", "colour": "#8a2f2a", "stripe": "#f1c40f",
  "writeProtected": true }
```

`file` is relative to the project root; `make site` gzips it into `images/`. A
missing file is shown as a greyed "still being written" sleeve, so entries can be
added before the image exists. The `site` rule depends on `build/floppy-*.img`.

## Tests

`web/tests/test_stream.py`: the streamed disk on a throttled network (boot and DOOM on demand with
MB/time report, prefetch to 100%, DOS-written file across reload, a failed chunk -> Abort, Retry,
Fail?, offline relaunch via the service worker, offline uncached read -> disk error).


`web/tests/test_mobile.py`: WebKit iPhone 13 and Chromium Pixel 5 (Keyboard button focus inside the
tap, insertText/deleteContentBackward/line breaks and the composition fallback reach DOS), PC keys
(Ctrl latch + C, held-arrow repeat), app layout in both orientations with the pad and multi-touch,
the disk menu, manifest and icons, and the service worker (offline boot, a new release replacing
the shell cache and the old disk image).


`web/tests/test-fat.mjs` (node): fat.js against mtools (`mdir`, `mcopy` round trips) and
`fsck.fat -n` on a floppy and on the hard disk's partition, 8.3 names, disk full,
root directory full, subdirectory growth.


`web/tests/test_sb_audio.py` (Chromium, no autoplay override): no AudioContext or SB audio node
before a gesture; after the power switch the worklet runs at the context's rate and receives
audio; SBTEST.EXE's chime and chord come out of the node (AnalyserNode). It stages its own
site in build/sb-web around the image apps/sbtest's test builds.

`web/tests/test_site.py` (Chromium, SwiftShader WebGL): first paint, power-on to POST,
speaker and disk callbacks, focus/Escape/typing, turbo + MHz display, insert/boot/eject,
printer + tear off, pause/step/breakpoint/JIT restore, interrupt log, hex view, CRT
toggle, HD + CMOS persistence across reload, factory reset, 2D fallback, power off,
and a 390x844 phone (no horizontal scroll, boot, extra keys, soft keyboard).

`web/tests/test_memmap.py` (Chromium): the MEMORY MAP while DOOM runs its timedemo: the
A0000 framebuffer lit by writes (all 250 cells), DOOM's MCB block by execution, the busiest
list, hover readout, click-to-zoom, detaching when the section or the inspector closes; prints
the panel's frame time and the host MIPS with the map open and closed. Stages build/memmap-web
around the DOOM timedemo image; screenshots in build/memmap-web/*.png.

## The memory map (js/memmap-panel.js)

A section of the inspector (MEMORY MAP, closed by default; OPEN/CLOSE is remembered in
`armdos.prefs.memmap`): the machine's address space as a heat map, every cell glowing **amber
for instructions executed** there, **green for reads** and **magenta-red for writes**, on a
log brightness scale (4 decades, relative to a slowly falling peak) with a 0.18 s time
constant (a burst is gone after about half a second). Drawn at ~15 Hz: one ImageData pixel
per cell, scaled up with a dark grid over it (the LED-matrix look) and the same image blurred
and added on top (phosphor bloom, where `ctx.filter` exists).

* **Top: all 16 MB** as 64x64 cells of 4 KB (a row is 256 KB; the first four rows are the
  first megabyte), with side strips for the BIOS ROM (1 MB, 4 KB cells), the ISA I/O ports (16
  ports per cell, 0-3FFh, plus one for the rest) and the VGA font RAM. The white frame is the
  window shown below; a white box marks the cell the PC is in. Click a row (or the ROM strip) to
  move the window there.
* **Bottom: a 1 MB window** as 64x64 cells of 256 bytes (a row is 16 KB), conventional memory by
  default. Right of it the regions, read from guest memory once a second by
  `emu/memmap.mjs memoryRegions()`: IVT, BIOS data area, the DOS kernel below the MCB arena,
  every MCB block named after its owner (program blocks by their MCB name, environments and
  data as "NAME data", free blocks, DOS system blocks), video RAM (VGA graphics, MDA window,
  text/CGA, or the mode 62h linear frame buffer), the adapter ROM hole, the BIOS's HMA data and
  stacks, HIMEM's XMS handles (found through its device header "XMSXXXX0" and handle table),
  the empty bus above the SIMMs, the ROM. The block of the program the PC is in is tinted
  brighter and its label gets a ▸. Click a cell: the hex viewer below jumps there.
* **Hover** any cell: address range, region (start, size), and its execute/read/write rates per
  second (and, for written lines, the share of recent frames that stored to it).
* **BUSIEST**: the six regions with the most activity, with three bars and the rates.
* The state line says `SAMPLING 1/2048` (reads and execution are sampled, see emu/README.md),
  `HALTED`, or `JIT OFF: WRITES + I/O ONLY` while breakpoints keep the JIT off.

Its cost: nothing while closed (the counters are detached). Open, the emulator runs ~1-3%
slower (emu/README.md, "Memory activity map") and the panel itself takes ~2-3 ms per frame
in headless Chromium with its software canvas (drain + decay of the 70k cells, two ImageData
maps, labels, the busiest list every 8th frame).

## The modem (docs/MODEM.md)

`js/modem-line.js` owns the page's PhoneExchange and is wired into main.js with a few lines
(`line.machineOptions()`, `line.frame()`, `line.mount($('case'))`, `line.powerOff()`).
`js/modem-audio.js` synthesises the speaker (dial tone, DTMF, pulse clicks, ringback, busy,
2100 Hz answer tone, V.22bis / V.32bis-style training generated as real DPSK/QAM carriers),
`js/modem-panel.js` + `css/modem.css` draw the Smartmodem-style lamp panel, phone list, Host Link
prompt and BBS sysop view. `js/modem-bbs-worker.js` runs the ARM Pit BBS (build/bbs.img, staged
as `images.bbs` when it exists) in a Web Worker, booted on the first call to 555-1989. The Host
Link (555-0100, `emu/hostlink.mjs`) uses the browser file picker and downloads.
`tests/test_modem.py` checks lamps, the sound sequence, Host Link, hang-up and the BBS worker.

The modem panel has a four-position **speed switch** (2400 / 14.4 / 33.6 / 56K, saved in `armdos.prefs.modemSpeed`) that sets the visitor's modem card; the BBS worker's modem and the Host Link take up to 56K, so the visitor's switch decides. The training sounds come from `emu/modemsound.mjs` (8 kHz buffers).

## The monitor swap (Hercules card + mono monitor)

The toolbar's **Monitor** selector (next to CRT): VGA Color, or Hercules Mono Green (P39) /
Amber (P134) / Paper White; saved in `armdos.prefs.monitor`. The card is chosen when the machine
is switched on (`createMachine` passes `video`/`monitor`; a later power-on calls
`m.setVideo()` before `powerCycle()`), so a change while running shows a note asking for a power
cycle. The emulator already draws in the phosphor colour (emu/render-hgc.mjs); display.js
(`setMonitor`, `PHOSPHOR`) adds the mono tube: no shadow mask, softer scanlines, more bloom and
a halo, the glass tinted by the phosphor, and an afterglow (the last frame's light decays by
140/124/92 of 256 per frame, green/amber/white) while the CRT effect is on. The bezel reads
MONOCHROME DISPLAY. `tests/test_monitor.py`: selector, POST/boot in mode 7, the amber and green
tints measured on the canvas, the note, HERCULES.EXE in graphics, back to VGA.


## Open the box (js/openbox.js)

The **OPEN CASE** thumbscrews on the system unit's lid (also "Open case" in the toolbar, CASE in
the app bar) open a panel under the desk: the beige lid lifts and slides off to the rear and shows
the ARM/AT from above, one SVG (viewBox 1200 x 820) drawn by `boardSvg()`: the steel tray, the
green system board (ARM926EJ-S in a ceramic PGA with a gold lid, the VFP9-S module on its header,
the EMS chipset, the 8742, the RTC and friends, the labelled BIOS EPROM, the NiCd barrel, J1 the
clock jumper, two banks of 30-pin SIMM sockets), seven ISA slots (J5-J6 8-bit, J7-J11 16-bit with
the AT extension) holding their cards, ribbon cables to the drives, the power supply with its fan,
and the drive cage. Below it: the parts bin (pink anti-static foam) and the configuration sheet
(SIMMs, J1, a summary, "Factory configuration", "Put the lid back").

* **Cards** (`cardSvg`): VGA 256K, Hercules-compatible mono, Sound Blaster 16 (with the GS
  wavetable daughterboard = the MPU-401 on its J11), AdLib-compatible FM, the modem (its SW1
  DIP switch is the modem panel's speed switch), the multi-I/O card and the IDE/floppy
  controller (these two are fixed: a red dab on the screw). Drag a card between a slot and the
  bin, or click it (in: first free slot that fits, or the slot of the card it replaces; out: to
  the bin). 16-bit cards refuse 8-bit slots; dropping on an occupied slot swaps with where the
  card came from or shifts the other card over. One display card and one sound card at a time:
  putting in the other swaps them; the display card cannot just be removed.
* The **daughterboard**, the **CD-ROM drive** (5.25" bay) and the **mouse** (plug in the PS/2
  port) are dragged or clicked the same way. Clicking the SIMMs cycles 1-16 MB; J1's pins move
  the jumper cap. Every drag is pointer events, so it works with a finger (parts have
  `touch-action: none`; the rest of the board scrolls and zooms).
* **Power**: while the machine is on nothing can be changed - a red warning shakes and the board
  flashes; the PSU fan turns and its lamp is lit. Changes take effect at the next power-on:
  `createMachine` spreads `box.machineOptions()`, a later power-on calls `box.apply(m)`
  (`m.setHardware`) before `powerCycle()`.
* **State**: `armdos.prefs.hw` = `{ ram, mhz, sound, modem, cdrom, mouse, slots }`; the display
  card is `prefs.monitor` (the Monitor selector; a Hercules card brings back the last phosphor,
  `prefs.phosphor`), the daughterboard `prefs.gmSynth` (the MIDI switch), the modem speed
  `prefs.modemSpeed`. Both ways: the selector, the MIDI switch and the speed switch move the
  cards (`box.external()`). Without the modem the modem panel goes dark (NO MODEM CARD); without
  the CD-ROM the case shows a blank 5.25" plate and the disc box says so; without a mouse the
  Mouse button is disabled.
* **Layouts**: the panel spans the page width (the board needs it); below ~640 px the board
  scrolls sideways in its frame (the page does not). In the app layout it is a sheet over the
  screen with a close button.
* `tests/test_openbox.py`: lid, drag and click in both directions, the 16-bit/occupied-slot
  rules, SIMMs + J1, localStorage, POST's banner and box and MEM after power-on, the powered
  refusal, reload persistence, the Hercules/Monitor and daughterboard/MIDI couplings, factory
  reset, a 390 px phone and the app sheet.


## The joystick (js/joystick.js)

The machine's game port (201h, emu/dev/gameport.mjs, ARCH.md 4.5) takes two analogue joysticks.
`JoystickHost` polls `navigator.getGamepads()` every animation frame and moves the sticks'
potentiometers (`machine.joy.setAxis`, -1..1 = 0-100 kOhm) and buttons:

* **First controller**: left stick (the D-pad wins while pressed) -> joystick A; face buttons
  south/east/west/north (Xbox A/B/X/Y) -> buttons 1/2/3/4 (3 and 4 are the port's stick-B
  buttons, as four-button PC sticks wired them); the right stick -> joystick B's axes.
  **Second controller**: joystick B (its stick; A/B -> buttons 3/4).
* Radial dead zone of 15%, and the controller's round gate stretched to a PC stick's square
  one (a diagonal push reaches both potentiometers' ends).
* A stick is plugged in while a controller is connected. Browsers reveal a controller only
  after one of its buttons is pressed, and DOS games look for the stick when they start, so:
  press a button, then start the game (the lamp's tooltip says so).
* **The on-screen pad's JOY switch** (the round key in the D-pad's hub, `armdos.prefs.padJoy`):
  the D-pad pushes stick A to its stops and FIRE / STRAFE / RUN / OPEN become joystick buttons
  1 / 2 / 3 / 4 (DOOM's default joyb_fire 0, joyb_strafe 1, joyb_speed 2, joyb_use 3); the
  small keys stay keys. Stick A is plugged in while JOY is on.
* **Lamps**: a joystick lamp on the front panel (after X86) and in the app bar: green while a
  controller (or JOY) is plugged in, amber while a button is held; the tooltip names the
  controller.
* **Open the box**: the Sound Blaster 16 has a game port; the multi-I/O card has one too, with
  jumper **J2 GAME** (click it; `prefs.hw.game`, default on). The machine has 201h when either
  is in (`joystick` option), and the summary says which ("Game port").
* `tests/test_joystick.py` (Chromium, `navigator.getGamepads` replaced before the page loads by
  a scripted standard-mapping controller): lamps and tooltip, the stick plugged/centred, JOYTEST's
  crosshair following the stick, dead zone, D-pad, buttons 1 and 4 on JOYTEST's lamps, the
  right stick as joystick B, unplugging, the pad's JOY mode (D-pad and FIRE; no keys reach DOS),
  the open case's game port summary and J2, and a machine without a game port.
