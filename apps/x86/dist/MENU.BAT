@ECHO OFF
REM ELBOW gallery: real DOS programs for the IBM PC, run on the ARM PC.
REM   MENU        shows the list      MENU n     runs program n
REM   MENU n v    runs volume v of a multi-volume game (e.g. MENU 14 2)
IF "%1"=="1" GOTO G1
IF "%1"=="2" GOTO G2
IF "%1"=="3" GOTO G3
IF "%1"=="4" GOTO G4
IF "%1"=="5" GOTO G5
IF "%1"=="6" GOTO G6
IF "%1"=="7" GOTO G7
IF "%1"=="8" GOTO G8
IF "%1"=="9" GOTO G9
IF "%1"=="10" GOTO G10
IF "%1"=="11" GOTO G11
IF "%1"=="12" GOTO G12
IF "%1"=="13" GOTO G13
IF "%1"=="14" GOTO G14
IF "%1"=="15" GOTO G15
IF "%1"=="16" GOTO G16
IF "%1"=="17" GOTO G17
CLS
ECHO   ELBOW GALLERY - genuine IBM PC programs from the 1980s, unmodified.
ECHO   This PC has no Intel chip: as each one runs, ELBOW translates its x86
ECHO   machine code into ARM code on the fly, in real time, right here in your
ECHO   browser. Watch the amber X86 lamp.          Type MENU and a number.
ECHO.
ECHO    1  Kingdom of Kroz (1987)           Scott Miller / Apogee   text mode
ECHO    2  Caverns of Kroz II (1990)        Scott Miller / Apogee   text mode
ECHO    3  Dungeons of Kroz (1989)          Scott Miller / Apogee   text mode
ECHO    4  Return to Kroz (1990)            Scott Miller / Apogee   text mode
ECHO    5  Temple of Kroz (1990)            Scott Miller / Apogee   text mode
ECHO    6  Final Crusade of Kroz (1990)     Scott Miller / Apogee   text mode
ECHO    7  Kingdom of Kroz II (1990)        Scott Miller / Apogee   text mode
ECHO    8  The Lost Adventures of Kroz      Scott Miller / Apogee   text mode
ECHO    9  ZZT 3.2 (1991) + editor          Tim Sweeney / Epic      text mode
ECHO   10  Sopwith, Author's Edition        David L. Clark / BMB    CGA
ECHO   11  Beyond the Titanic (1986)        Scott Miller            text adventure
ECHO   12  Supernova (1987)                 Scott Miller            text adventure
ECHO   13  Word Whiz, volume 1 of 4         Scott Miller            word quiz
ECHO   14  Arctic Adventure, volume 1 of 4  George Broussard        CGA
ECHO   15  Monuments of Mars, vol. 1 of 4   Todd Replogle           CGA
ECHO   16  Pharaoh's Tomb, volume 1 of 4    George Broussard        CGA
ECHO   17  Second Reality (Future Crew, 1993)  the demo - 386, VGA, SB (Ctrl ends)
ECHO   Licences and sources: README.TXT.  Sopwith: Break (Scroll Lock) ends.
ECHO   Business programs (GW-BASIC, VDE, SC, DataPerfect, Kermit): type OFFICE
GOTO END
:G1
CD \ELBOW\APPS\KROZ\KINGDOM
ELBOW KINGDOM
GOTO BACK
:G2
CD \ELBOW\APPS\KROZ\CAVERNS
ELBOW CAVERNS
GOTO BACK
:G3
CD \ELBOW\APPS\KROZ\DUNGEONS
ELBOW DUNGEONS
GOTO BACK
:G4
CD \ELBOW\APPS\KROZ\RETURN
ELBOW RETURN
GOTO BACK
:G5
CD \ELBOW\APPS\KROZ\TEMPLE
ELBOW TEMPLE
GOTO BACK
:G6
CD \ELBOW\APPS\KROZ\CRUSADE
ELBOW CRUSADE
GOTO BACK
:G7
CD \ELBOW\APPS\KROZ\KINGDOM2
ELBOW KINGDOM
GOTO BACK
:G8
CD \ELBOW\APPS\KROZ\LOST
ELBOW LOST
GOTO BACK
:G9
CD \ELBOW\APPS\ZZT
ELBOW ZZT
GOTO BACK
:G10
CD \ELBOW\APPS\SOPWITH
ELBOW SOPWITH
GOTO BACK
:G11
CD \ELBOW\APPS\TITANIC
ELBOW BEYOND
GOTO BACK
:G12
CD \ELBOW\APPS\SUPERNOV
ELBOW NOVA
GOTO BACK
:G13
CD \ELBOW\APPS\WORDWHIZ
IF "%2"=="" ELBOW WW1
IF NOT "%2"=="" ELBOW WW%2
GOTO BACK
:G14
CD \ELBOW\APPS\ARCTIC
IF "%2"=="" ELBOW AA1
IF NOT "%2"=="" ELBOW AA%2
GOTO BACK
:G15
CD \ELBOW\APPS\MARS
IF "%2"=="" ELBOW MARS1
IF NOT "%2"=="" ELBOW MARS%2
GOTO BACK
:G16
CD \ELBOW\APPS\PTOMB
IF "%2"=="" ELBOW PTOMB1
IF NOT "%2"=="" ELBOW PTOMB%2
GOTO BACK
:G17
CALL \ELBOW\DEMOS\SECOND %2
GOTO BACK
:BACK
CD \ELBOW\APPS
:END
