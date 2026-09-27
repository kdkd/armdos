@ECHO OFF
REM GETPAK.BAT - brings PAK0.PAK (Hexen II demo data) from your own computer
REM into C:\GAMES\HEXEN2\DATA1 through the Host Link (555-0100) with TERM.
C:
CD \GAMES\HEXEN2\DATA1
CLS
ECHO  ============================================================================
ECHO   HEXEN II - fetching PAK0.PAK through the Host Link
ECHO  ============================================================================
ECHO.
ECHO   TERM will call the ARM-DOS Host Link (555-0100) at 14,400 bps and ask it
ECHO   for a file.  When your web browser opens its file window, choose the
ECHO   pak0.pak you took out of the Hexen II demo (see README.TXT).  It arrives
ECHO   by ZMODEM in C:\GAMES\HEXEN2\DATA1 as PAK0.PAK.
ECHO.
ECHO   27,750,257 bytes at 1,300 a second take about SIX HOURS.
ECHO   Leave the page open.  If the call breaks off, type GETPAK again: TERM
ECHO   continues where it stopped.  (Much quicker: the Files panel - README.TXT.)
ECHO.
ECHO   Press Esc during the call to stop the script, Ctrl-C now to cancel.
ECHO.
PAUSE
C:\TERM\TERM.EXE /S:C:\GAMES\HEXEN2\GETPAK.SCR
IF ERRORLEVEL 3 GOTO FAILED
IF ERRORLEVEL 2 GOTO NOCALL
IF ERRORLEVEL 1 GOTO STOPPED
CD \GAMES\HEXEN2
C:\GAMES\HEXEN2\INSTALL
GOTO END

:NOCALL
ECHO.
ECHO Could not connect to the Host Link (555-0100).  Try GETPAK again.
GOTO BACK

:STOPPED
ECHO.
ECHO GETPAK stopped.  Type GETPAK to continue the transfer later.
GOTO BACK

:FAILED
ECHO.
ECHO The transfer did not finish.  Type GETPAK to continue it.

:BACK
CD \GAMES\HEXEN2
:END
