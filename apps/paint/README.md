# ARM Paint (PAINT.EXE) and ARM Banner (BANNER.EXE)

Original programs written for ARM-DOS (no third-party source), in the spirit of
PC Paintbrush, Deluxe Paint and MacPaint, and of Print Shop / Banner Mania.
Also here: the page's printer became an Epson FX-80 (see "The printer" below).

## PAINT.EXE - ARM Paint 1.00

    PAINT [picture] [/N]          (C:\DOS\PAINT.BAT runs C:\PAINT\PAINT.EXE)

`/N` skips the splash box. A picture named on the command line is opened (a
name without an extension gets .PCX; a name that does not exist becomes the
name of a new picture).

* **Screen**: VGA mode 13h, 320x200x256. Menu bar (File, Edit, Options, and the
  tool name / picture coordinates / messages on the right), the tool box (16
  tools, 4 brush sizes), the colour strip (all 256 colours, 32 x 8; the
  foreground cell is notched; the swatch shows foreground over background) and
  a 288x166 view of the 320x200 picture (scroll it with the Scroll tool or
  PgUp/PgDn/Home/End; Tab = full screen, the whole picture).
* **Mouse** through INT 33h (MOUSE.COM): left button = foreground colour, right =
  background colour, everywhere (drawing, fill, text, palette strip, picker).
  The program polls AX=0003h and draws its own cursor (arrow over the chrome,
  an inverting crosshair over the picture); AX=0005h press counts catch clicks
  that come and go between two polls. It sleeps in WFI (the CPU halts) when
  nothing happens.
* **Without a mouse** (or with one): the arrow keys move the cursor (Shift: 8
  pixels), Space clicks, Ins holds the left button down (for drags). F1 lists
  the keys.
* **Tools**: pencil, brush (round, 1/3/5/8), airbrush, line, box, filled box,
  ellipse, filled ellipse (Zingl's rectangle ellipse; lines and outlines use the
  brush size), flood fill (scan-line, 4-connected), text (the ROM 8x8 font from
  INT 10h AX=1130h BH=03h; type, Backspace, Enter; Esc or a click ends it),
  eraser (a square of the background colour), colour picker (returns to the
  previous tool), zoom (FatBits: x4 with a grid and an actual-size window in the
  corner; every tool works in it), scroll (hand), undo, palette. Shift while
  dragging: 45-degree lines, squares, circles.
  Keys: P B A L R Shift+R E Shift+E F T K Z H, 1-4 brush size, [ ] and { }
  step the foreground / background colour, X swaps them, U or Ctrl+Z undo.
* **Undo**: 8 levels (the picture and its palette), in XMS when conventional
  memory is short.
* **Menus**: File (New ^N, Open... ^O, Save ^S, Save As..., Print... ^P, About...,
  Quit Alt+X), Edit (Undo ^Z, Clear, Flip Horizontal, Flip Vertical, Swap
  Colours), Options (Palette..., Default Palette, Zoom Grid, Full Screen, Keys...).
  Mouse press-drag-release or click-click; Alt+F/E/O or F10, arrows, Enter,
  underlined hot keys. "Save the changes to X?" Yes/No/Cancel on New, Open and Quit.
* **File dialog**: the directory (it starts in PAINT.EXE's own directory),
  subdirectories, `[..]`, drives `[-A-]` `[-C-]` (a drive that is not ready gives
  a message: the program's INT 24h handler fails the call), *.PCX and *.BMP;
  type a name or pick one (double click, Enter), scroll arrows.
* **Palette editor**: the 256 colours, R/G/B sliders 0-63 (drag, or r/R g/G
  b/B), Default, Spread (a gradient between two colours), live on the DAC,
  Cancel restores. The tool box is drawn with the colours of the picture's
  palette nearest to black / greys / white / blue, so it stays readable with
  any palette.
* **Files**: PCX version 5 (ZSoft), 8 bits, 1 plane, RLE, 320x200, 256-colour
  palette (0Ch + 768 bytes, 6-bit DAC values scaled to 8 bits) - what PC
  Paintbrush 3+ wrote; any tool reads it. Save As with a .BMP name writes a
  Windows 3 256-colour BMP. Reads: PCX 8-bit (any size: cropped / placed top
  left), 1-bit 1-4 planes (monochrome, 16-colour EGA palette), BMP 8-bit and
  4-bit uncompressed, 8-bit RLE8, top-down or bottom-up.
* **Print** (to LPT1 through INT 17h, the status checked first): Draft = ESC * 0
  (60 x 72 dpi, 384 dots a line) or Letter quality = ESC * 1 (120 x 72 dpi, 768
  dots); Floyd-Steinberg (serpentine) or ordered (8x8 Bayer) dithering to black
  and white of the picture's luminance, sampled bilinearly, so the 4:3 picture
  comes out 6.4" x 4.8" centred on the 8" line. The stream: `ESC @`, the title
  (`ESC E` "ARM Paint - path" `ESC F`), `ESC O` (no skip over the perforation),
  `ESC 3 24` (8/72", the bands touch), 44 bands of `8 spaces, ESC * m n1 n2
  data, CR LF` (trailing white columns trimmed, white bands just CR LF), `ESC 2`.
  A progress box; Esc stops after the current band (and sends `ESC @`).
* **Samples**: C:\PAINT\SAMPLES\SPLASH.PCX (ARM-DOS 4.00 in chrome over a
  synthwave grid), SUNSET.PCX (a sunset over the sea with a palm tree),
  ARMAT.PCX (a pixel-art ARM/AT: monitor with a C:\> prompt, system unit,
  keyboard, mouse). Drawn by code: `node apps/paint/samples/mksamples.mjs`
  writes them (colours 0-31 are left as the VGA's defaults).

## BANNER.EXE - ARM Banner 1.00

    BANNER [message] [/F:n] [/B:n]
      /F:n  font: 1 Block (ROM 8x8), 2 Smooth (VGA 8x16, smoothed), 3 Outline, 4 Shadow
      /B:n  border: 0 None, 1 Line, 2 Double, 3 Stars, 4 Hearts

Without a message it asks (Message, Font (1-4) [2], Border (0-4) [3]). Huge
letters sideways along continuous paper: each glyph row spans ~1/16 of the 8"
line, the letters' tops toward the right-hand edge (the banner reads left to
right with the first sheet on the left), 1" of lead-in and lead-out, 1/4"
between letters. Printed as `ESC K` (60 dpi) bands with `ESC 3 24` and `ESC O`;
"Printing letter n of m", Esc stops. Smooth = bilinear interpolation of the
font bitmap thresholded at 1/2; Outline = the smooth letter's edge; Shadow = a
50% grey copy offset down-right. BANNER.EXE is in C:\DOS.

## The printer (web/js/printer.js)

The page's DMP-9 is now an Epson FX-80 compatible 9-pin printer: ESC @; ESC E/F
emphasized, ESC G/H double-strike, ESC 4/5 italic, ESC - n underline (the 9th
pin), ESC W n / SO / DC4 double width, SI / ESC SI / DC2 condensed (17.16 cpi),
ESC M/P elite/pica, ESC S n / T super/subscript, ESC ! n; ESC 0/1/2, ESC 3 n,
ESC A n line spacing, ESC J n immediate feed (n/216"), ESC C page length, ESC N /
ESC O skip over the perforation, ESC l / Q margins, ESC $ / ESC \ head position,
ESC D tabs; bit images ESC K (60 dpi), L (120), Y (120 double speed), Z (240),
ESC * m (0-6: 60, 120, 120, 240, 80, 72, 90 dpi) and ESC ^ m (9 pins, 2 bytes a
column), ESC ? reassignment; other ESC sequences take their FX-80 parameter
counts and are ignored (ESC & user characters are skipped). Positions are kept
in 1/216" vertically and canvas pixels (144 dpi) across; a band that runs over
the perforation continues on the next sheet. Bit-image dots are round (r = 1/140"),
overlap at 120/240 dpi, jitter a little, and carry per-pass ribbon variation
and a slight misregistration between passes; the head moves left to right at
12"/s in graphics (24"/s for ESC Y), each ESC K/L/Y/Z/* pass plays the page's
existing print buzz with the pin rate as its pitch, and each carriage return
takes time after graphics. Plain text: exactly the old code path (same glyph
function, same Math.random calls, same positions, 220 cps), checked pixel for
pixel. `window.armdosPrinter` exposes the object (`speed`, `stats`, `x`, `y`).

## Tests

`make paint-test` (`tests/run.mjs`, ~15 s, 75 checks): start via PAINT.BAT, splash,
every tool with the emulated mouse (pixels checked in VGA memory: pencil,
8-pixel brush, palette strip left/right, hollow box, flood fill with the right
button, Ctrl+Z and the undo button, ellipses, line, airbrush radius, text in
the ROM font bit for bit, eraser, picker, a single FatBits pixel, Edit > Flip
Horizontal), Alt+X with "Save the changes?"; keyboard only (no MOUSE.COM):
Ins-held line, Space click; Save -> PCX header/palette checked, PIL opens it
and its pixels and palette equal the screen and the DAC; New, Open again =
pixel for pixel; Save As .BMP read by PIL; PIL-written PCX and BMP load with
their palettes; a 400x240 PCX; a missing file; printing SUNSET.PCX in draft,
letter and ordered modes: the ESC/P stream parsed (ESC @, title, ESC O, ESC 3 24,
44 bands of ESC * m n1 n2 + data + CR LF, ESC 2), decoded to
build/paint-test/print-*.png, and the ink density of 32 regions compared with
the picture's darkness (worst ~0.015, limit 0.08); the three samples read by PIL equal what
ARM Paint shows; BANNER from the command line and interactively (bands
decoded to banner.png), invalid switch.

`make paint-web-test` (`tests/test_printer.py`, Chromium): stages
build/paint-web/site and a copy with the text-only printer.js
(`tests/printer-before.js`); the same text printed on both with a seeded
Math.random gives the same canvas (<= 4 pixels differ: Chromium alone varies by
a pixel between runs), line spacing and ESC $ positions, the head travel of
ESC K/L/Y/Z/* 4-6/^ at their densities, a picture (ARM Paint's letter-quality
print stream) on the paper with the head moving, tear off. Screenshots:
build/paint-test/web-*.png.

## Deviations / limits

* Single-level text entry (no fonts or sizes besides the ROM 8x8), no
  selection / cut and paste, no polygon or curve tools, pictures are 320x200.
* The printer ignores international character sets, proportional spacing,
  user-defined characters and the FX-80's 128-255 italics (it prints CP437, as
  an IBM-mode printer does); the 120 dpi double-speed "no adjacent dots" rule of
  ESC Y / ESC Z is not enforced.
