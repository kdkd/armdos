# ARM-DOS Online (ONLINE.EXE + the service at 555-0199)

An online service in the manner of the early 1990s commercial services - an original design
with its own names, no borrowed trademarks, logos or sounds. You dial it with the ARM-PC's
modem and it reaches into **real, live data**: Wikipedia, Wiktionary, Open-Meteo weather and
the Hacker News wire, at the speed of the modem (the page's 2400 / 14.4 / 33.6 / 56K switch).

    C:\> ONLINE

* `C:\ONLINE\ONLINE.EXE`, `C:\ONLINE\ONLINE.CFG` (screen name, number, init string, port,
  DTE speed), `C:\DOS\ONLINE.BAT`, pictures you save go to `C:\ONLINE\PICTURES\`.
* The service is JavaScript on the page's phone exchange (`emu/online/`), like the Host Link.

## The client

* **Sign-on screen**: the big half-block logo, Screen Name and Password (any password is
  accepted; the name is remembered in ONLINE.CFG), and three little pictures that light up in
  turn - *Dialing...*, *Connecting...*, *Verifying password...* - while the page plays the
  modem's dial tone, DTMF and training noise. `ATZ`, the init string (`AT&C1&D2`) and
  `ATDT555-0199`; Esc cancels. BUSY / NO ANSWER / NO CARRIER come back as a note.
* **Main menu**: eight big coloured channel buttons - Encyclopedia, Dictionary, Weather,
  Technology News, Today in History, Random Article, About, Sign Off (1-8, arrows + Enter, or
  the mouse). A title bar with the clock and a status bar with the connect speed, online time
  and bytes received are on every screen.
* **Reader**: documents are laid out by the service for 76 columns and shown *as they arrive*
  (you can read and scroll the top while the rest is still on the line). Links are highlighted:
  Tab / Shift-Tab select, Enter follows, or type the link's number and Enter, or click it.
  Esc/Backspace goes back (the last 12 documents are kept in memory; one that was cut short
  when you left it is fetched again), F2 searches, P prints the document on PRN (the page's
  dot-matrix printer), F10 main menu, Alt-X signs off, F1 help.
* **Picture viewer** (CompuServe-style): a picture link switches to VGA mode 13h and you watch
  the interlaced GIF come down the line - the first pass fills the screen with 8-line blocks,
  each later pass sharpens it - with a status line: name, bytes, CPS, pass, percent and a
  progress bar. Esc stops the transfer; when it is complete, S saves the .GIF, any key returns.
* **Sign off** sends a goodbye, the host hangs up, the modem says `NO CARRIER`, and the
  sign-on screen shows the online time. A dropped line at any point gives "You have been
  disconnected".
* The 16550 on COM2 is driven directly, IRQ-driven, with TERM's `lib/comm.c` (and its
  `lib/scr.c`), port locked at 115200 so the modem, not the port, sets the pace.
  The mouse works through INT 33h when MOUSE.COM is loaded.

## The service (emu/online/)

| file | what |
|---|---|
| `service.mjs` | the PhoneExchange endpoint `OnlineService` at `ONLINE_NUMBER` (555-0199): answers after one ring at up to 56K (the caller's switch decides), sends a text banner, then speaks the frame protocol; one request at a time, paced by the caller's modem buffer like the Host Link; cancel; sign off; a person dialling in with TERM gets a note and a hang-up |
| `protocol.mjs` | frames (below) |
| `channels.mjs` | the channels -> documents; the "not available right now" messages |
| `wiki.mjs` | Wikipedia parser HTML -> document: headings, paragraphs, lists, quotes, simple tables, the infobox as "Quick facts" after the introduction, pictures as picture links, wiki links; footnote markers, navboxes, edit links and the reference sections left out; 40 KB cap with the names of the sections not sent |
| `html.mjs` | a small forgiving HTML parser (no DOM, so node tests work) |
| `doc.mjs` | the document format and layout (word wrap, indents, styles, link numbers) |
| `picture.mjs` | image -> <= 320x184 (aspect corrected for mode 13h's 1.2:1 pixels), median cut to 240 colours (240-255 are the viewer's), Floyd-Steinberg, **interlaced GIF89a** with LZW; browser decoding via createImageBitmap + canvas |
| `net.mjs` | fetch with a 10-minute cache, a per-host rate limit (200 ms between requests to Wikipedia), 4 in parallel, 20 s timeout |
| `cp437.mjs` | Unicode -> code page 437 (accents it has, look-alikes for the rest) |

Sources (all keyless, CORS-enabled, fetched by the visitor's browser): the Wikipedia action API
(`list=search`, `action=parse`, `list=random`) and REST feed (`onthisday/selected`), the
Wiktionary REST definition API, Open-Meteo geocoding + forecast, the Hacker News Firebase API.
Attribution is printed in each document (and in About).

### Protocol

After CONNECT both sides send frames `STX(02h) type len-lo len-hi payload sum` (sum = type +
length bytes + payload, mod 256); anything outside a frame is ignored.

| client -> service | | service -> client | |
|---|---|---|---|
| `H` name\0password\0version | sign on | `W` name\0text | welcome |
| `S` query / `A` title | search / article | `D` id(2) kind title\0 channel\0 | a document begins |
| `D` word / `W` place | dictionary / weather | `T` bytes | document text |
| `N` / `R` / `T` mmdd | news / random / on this day | `E` lines(2) flags | document ends |
| `F` doc-id(2) link(2) | follow link n of a document | `G` name\0caption\0 w(2) h(2) size(4) / `B` data / `F` | a picture (GIF bytes) |
| `X` | cancel | `Z` | cancel acknowledged (the client drops frames until it) |
| `Q` | sign off | `Q` text | goodbye; then the service hangs up |
| | | `R` class text / `S` text | error (E) or info (I) message / status ("Searching...") |

Document text: lines end with LF; `01h a b` starts link number (a-32)*96+(b-32), `02h` ends it,
`03h s` sets a style (n normal, t title, h/H/s headings, b bold, i italic, k dim, l link,
p picture link, y g c r m w colours). Every line stands on its own. Links are resolved by the
service (it keeps each call's last 40 documents), so no link table crosses the slow line.
Link kinds: article, search, picture, dictionary word, weather place, news story, and web
URLs (answered with an info message: that is the World Wide Web, outside the service).

## Tests

* `make online-test` = `tests/service.mjs` (the endpoint alone in fixture mode, driven by a JS
  client: every channel, links, the picture checked by decoding the GIF, cancel, network down
  -> "The Encyclopedia is not available right now.", sign off, a terminal caller) and
  `tests/run.mjs` (ONLINE.EXE on a headless ARM-PC with the real modem and exchange at 14400,
  the service in fixture mode: sign on, Encyclopedia search "ARM architecture", Tab + Enter to
  the article, Tab Tab to "RISC" and follow it, Back, the Acorn ARM Evaluation System picture
  in mode 13h mid-download and complete, S saves it byte-identical, print to PRN, weather for
  Berlin, sign off with NO CARRIER). `node apps/online/tests/run.mjs 2400` does it at 2400.
  Screenshots: `build/online-test/*.png`.
* `tests/test_live.py` (Playwright, after `make site`): every channel and the browser's picture
  pipeline on the real APIs, then a 56K call from the page's ARM-PC end to end. Network failures
  are SKIPs, not failures.
* Fixtures (`tests/fixtures/`, recorded with `node apps/online/tests/record.mjs`): Wikipedia /
  Wiktionary text (CC BY-SA), Open-Meteo data (CC BY 4.0), Hacker News items; the pictures are
  Wikimedia Commons files (the Acorn ARM Evaluation System photo by Peter Howkins, CC BY-SA 3.0)
  stored as gzipped PPM for the node decoder. Test data only; nothing of it is on the disks.

## Deviations / notes

* No real accounts: any screen name and password sign on. No e-mail, chat or message boards.
* The "Technology News" wire is Hacker News; "Today in History" uses the visitor's DOS date.
* Articles are capped at about 40 KB of text (almost three minutes at 2400 bps);
  the end says which sections were left out.
* Pictures are converted in the page and cached (12 per page load); they are Wikimedia
  Commons files under their own licences - the GIF's comment block names the file.
