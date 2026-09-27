# TURBO.COM

The ARM PC's turbo switch from the DOS prompt, plus one thing a 1988 turbo
button never had.

    TURBO              show the clock
    TURBO ON | OFF     the same as pressing the TURBO button (100 / 12 MHz)
    TURBO MAX          no clock limit
    TURBO RESTORE      back to the setting TURBO MAX replaced

Normally the emulator holds the ARM926 to its nominal clock, so programs see
the same machine on every computer. `TURBO MAX` sets bit 1 of the system
board's port F2h, and the page's real-time driver then picks the clock frame
by frame (`RealtimeDriver._unlockedAfter`), erring high:

* each slice measures how much wall time the CPU needed per emulated
  millisecond; with room to spare (under 60% of the frame) the clock rises,
  up to 2x per frame, to at most 999 MHz;
* it only comes down when emulation falls behind the wall clock (by up to
  half per frame), and never below the turbo clock;
* falling behind also sets a ceiling at 80% of the clock that failed, which
  lifts about 35% a second, so the clock doesn't bounce straight back into
  the same wall.

Why never below turbo: much of what a heavy program costs doesn't scale with
the clock (planar VGA writes go through the emulator's JavaScript; scan-line
waits cost per line), and frame-locked code spends extra cycles in polling
loops the spin detector skips cheaply. A lower clock rarely buys time back,
and it makes timing-sensitive code miss its deadlines (Second Reality's
credits fade over each other at 25 MHz). Emulated time always follows the
wall clock, so timers, music and the modem keep their speed; programs just
get more instructions per second. The front panel's MHz display shows the
clock four times a second.

`TURBO MAX` leaves the setting it replaced in the BIOS data area's
intra-application communications area (0040:00F0h, "TB" + the old port
value), so a batch file can bracket one heavy program:

    TURBO MAX >NUL
    ELBOW SECOND
    TURBO RESTORE >NUL

That is what `C:\ELBOW\DEMOS\SECOND.BAT` (Second Reality) does. A reset or
pressing the TURBO button also puts the limiter back. Headless runs
(`Machine.runFor`) have no driver, so there the clock stays at 100 MHz and
tests stay deterministic.

Tests: `make turbo-test` (DOS side, the batch bracket, the controller) and
`web/tests/test_turbo.py` (the page's button, LED and MHz display).
