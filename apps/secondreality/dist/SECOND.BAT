@ECHO OFF
REM Second Reality (Future Crew, 1993) under ELBOW.  SECOND [1-5] starts at a part.
REM TURBO MAX lifts the clock limit for the demo; TURBO RESTORE puts it back.
CLS
ECHO Second Reality - Future Crew, 1993
ECHO.
ECHO On the next screen, press the Left arrow once to pick "Sound Blaster Pro"
ECHO (the demo's first choice, a Gravis UltraSound, isn't fitted in this PC),
ECHO then press Enter to start.  Esc skips a part; hold Ctrl to end the demo.
ECHO.
ECHO While it runs, the clock limiter is off: the ARM goes as fast as your
ECHO computer allows, which you can watch on the front panel's MHz display.
ECHO.
ECHO A work in progress: Second Reality uses a lot of tricks that this PC does
ECHO not fully emulate yet, so some parts may lag, look different from the
ECHO original or not work right. It is here to show how far the emulator has
ECHO come, not as a perfect performance, and it will get better.
ECHO.
PAUSE
CD \ELBOW\DEMOS\SECOND
TURBO MAX >NUL
ELBOW SECOND %1
TURBO RESTORE >NUL
CD \ELBOW\DEMOS
