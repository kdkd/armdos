@ECHO OFF
REM GETKEEN.BAT - gets Commander Keen in Keen Dreams (shareware) for KEEN.EXE
REM Calls The ARM Pit BBS with TERM, downloads KEENDRMS.ZIP, unzips it here
REM and starts the game. KEEN.EXE, GETKEEN.BAT, GETKEEN.SCR: ARM-DOS.
C:
CD \GAMES\KEEN
SET GETKEEN=
IF NOT EXIST KDREAMS.EXE GOTO FETCH
IF NOT EXIST KDREAMS.EGA GOTO FETCH
IF NOT EXIST KDREAMS.MAP GOTO FETCH
IF NOT EXIST KDREAMS.AUD GOTO FETCH
IF NOT EXIST KDREAMS.CMP GOTO FETCH
IF NOT EXIST LAST.SHL GOTO FETCH
GOTO PLAY

:FETCH
CLS
ECHO  様様様様様様様様様様様様様様様様様様様様様様様様様様様様様様様様様様様様様様
ECHO   COMMANDER KEEN in "KEEN DREAMS"         (C) 1991-92 Softdisk Publishing
ECHO  様様様様様様様様様様様様様様様様様様様様様様様様様様様様様様様様様様様様様様
ECHO.
ECHO   KEEN.EXE is the game engine, ported to the ARM PC from the source code
ECHO   of Keen Dreams that was released under the GNU GPL in 2014. The game
ECHO   itself - graphics, levels, sounds - is not part of ARM-DOS.
ECHO.
ECHO   Keen Dreams is shareware. This will call The ARM Pit BBS and download
ECHO   it: TERM dials 555-1989, logs on as a guest and fetches KEENDRMS.ZIP
ECHO   (358,438 bytes) with ZMODEM - a few minutes at 14400 bps. UNZIP then
ECHO   unpacks it into C:\GAMES\KEEN and the game starts.
ECHO.
ECHO   KEENDRMS.ZIP is the unmodified shareware release, which Softdisk let
ECHO   BBS SysOps distribute freely. Its terms: LICENSE.DOC and VENDOR.DOC.
ECHO   Press Esc during the call to stop the script, Ctrl-C now to cancel.
ECHO.
PAUSE
IF EXIST KEENDRMS.ZIP GOTO TEST

:CALL
SET GETKEEN=CALLED
C:\TERM\TERM.EXE /S:GETKEEN.SCR
IF ERRORLEVEL 3 GOTO BBSFAIL
IF ERRORLEVEL 2 GOTO NOCALL
IF ERRORLEVEL 1 GOTO STOPPED

:TEST
IF NOT EXIST KEENDRMS.ZIP GOTO BBSFAIL
UNZIP -t KEENDRMS.ZIP >NUL
IF NOT ERRORLEVEL 1 GOTO UNZIP
IF "%GETKEEN%"=="CALLED" GOTO BADZIP
ECHO KEENDRMS.ZIP is incomplete - calling the BBS to finish the download...
GOTO CALL

:UNZIP
CLS
UNZIP -o KEENDRMS.ZIP
IF ERRORLEVEL 1 GOTO BADZIP
ECHO.
ECHO KEENDRMS.ZIP stays here: the shareware licence says to pass on the whole
ECHO unmodified zip. Type KEEN (in C:\GAMES\KEEN) to play again. Have fun!
ECHO.
PAUSE

:PLAY
SET GETKEEN=
KEEN
GOTO END

:NOCALL
ECHO.
ECHO Could not connect to The ARM Pit BBS (555-1989). Try GETKEEN again later.
GOTO END

:STOPPED
ECHO.
ECHO GETKEEN stopped. Run GETKEEN again to continue the download.
GOTO END

:BBSFAIL
ECHO.
ECHO The download from The ARM Pit BBS failed. Run GETKEEN to try again.
GOTO END

:BADZIP
ECHO.
ECHO KEENDRMS.ZIP is damaged. Type DEL KEENDRMS.ZIP and run GETKEEN again.

:END
SET GETKEEN=
