# JOYTEST.EXE - joystick test and calibration

`C:\DOS\JOYTEST.EXE` tests and calibrates the joysticks on the game adapter at 201h (ARCH.md
4.5, emu/dev/gameport.mjs). It is an ARM-DOS original in the style of the calibration
utilities that came with game cards and sticks around 1990 (CH Products' and Gravis's
JOYCAL/JOYTEST programs): one full screen, blue, box-drawn.

```
JOYTEST          interactive
JOYTEST /C       start with the calibration
JOYTEST /R       report the readings once on standard output (redirectable) and exit
JOYTEST /?       help
```

The screen shows:

* **Joystick A / Joystick B**: each axis's raw count as a number and a bar, the calibration
  (min / centre / max) under it, "not connected" for an axis that never ends its pulse, and
  the button lamps (buttons 1-2 are stick A's, 3-4 stick B's, which four-button sticks use).
* **Position**: a crosshair (the sun symbol) that follows stick A inside a box, scaled by
  the calibration; the centre lines are dotted. "No joystick in port A" when it is unplugged.
* What the BIOS says: the game adapter bit of INT 11h and the INT 15h AH=84h readings
  (the AT's units) and switches.
* Keys: **C** calibrate, **S** save JOYSTICK.CFG, **D** defaults, **Esc** exit.

**Reading.** The classic way: wait for the previous pulses to end (the 558 is not
retriggerable), write 201h to fire the one-shots, then count polls, interrupts off, until
each axis bit drops; 4000 polls without an end = not connected. Each poll takes 1 us on the
ISA bus plus the loop, so the counts are about 20 (0 Ohm) to 940 (100 kOhm) at 100 MHz, a
centred stick about 480; slower clocks read a little lower. Axes found open are looked at
again every 32 frames (a stick plugged in later is noticed).

**Calibration.** "Centre the joystick and press button 1", "Move the joystick to the UPPER
LEFT corner and press button 1", "... LOWER RIGHT ...": each step takes the readings at the
press of button 1 and waits for its release; Esc cancels. The corners must lie on both sides
of the centre, else the calibration is refused. Stick B is calibrated along with A when it
is plugged in and moved. Uncalibrated, the first reading of a stick is taken as its centre
and the ends are derived from the 558's timing (4.2% and 195.8% of the centre count).

**JOYSTICK.CFG** (current directory; read at start-up):

```
; JOYSTICK.CFG - written by JOYTEST 1.00
; raw game port counts (201h), minimum / centre / maximum
[JoystickA]
XMin=110
XCenter=493
XMax=831
...
[JoystickB]
...
```

The games keep their own calibrations, as they did in DOS: Wolfenstein 3D and Keen Dreams in
their control panels, DOOM, Quake and Duke Nukem 3D at start-up (see their READMEs).

Monochrome (Hercules, mode 7) works too: plain attributes, retrace from 3BAh.

## Tests

`make joytest-test` (tests/run.mjs, node, a simulated stick through `machine.joy`):

* the BIOS through `JOYBIOS.EXE` (tests/joybios.c, built to build/joytest-test/): INT 11h bit
  12; INT 15h AH=84h DX=0 (F0h, C0h with buttons 1+2, B0h with button 3), DX=1 against the 558
  timing in AT units for one and two sticks (0 Ohm ~4, 75 kOhm ~127, centre ~86) at 100 and
  12 MHz, 0 with nothing plugged in, DX=2 = CF/86h; a machine without a game port;
* `JOYTEST /R > JOY.TXT`: counts, buttons, stick B absent, the BIOS line;
* the full screen: no stick, a stick plugged in centred (crosshair in the middle), right and
  up (crosshair and counts), button lamps 1 and 4, the calibration's three prompts, the
  calibrated corner and centre positions of the crosshair, S, JOYSTICK.CFG on the disk,
  loaded again by a second run; build/joytest-test/joytest.png.

`make joy-web-test` (web/tests/test_joystick.py): the page's Gamepad API and on-screen pad
moving this program's crosshair.
