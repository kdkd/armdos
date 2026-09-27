@ECHO OFF
REM ELBOW gallery, business & productivity: real x86 DOS programs on the ARM.
REM   OFFICE      shows the list      OFFICE n    runs program n
IF "%1"=="1" GOTO P1
IF "%1"=="2" GOTO P2
IF "%1"=="3" GOTO P3
IF "%1"=="4" GOTO P4
IF "%1"=="5" GOTO P5
IF "%1"=="6" GOTO P6
IF "%1"=="7" GOTO P7
CLS
ECHO   ELBOW GALLERY - BUSINESS AND PRODUCTIVITY.   Type OFFICE and a number.
ECHO   (The games are on MENU.)  Everything prints to the printer on LPT1.
ECHO.
ECHO    1  GW-BASIC           Microsoft's BASIC, assembled from its MIT source
ECHO                          on this ARM PC.  LOAN.BAS prints a table; SYSTEM quits
ECHO    2  GW-BASIC graphics  SPIRAL.BAS in CGA 320x200
ECHO    3  VDE 1.97           WordStar-style word processor, opens LETTER.TXT
ECHO                          (Esc = menu, Print from the menu, Alt-X quits)
ECHO    4  SC 6.21            spreadsheet, opens BUDGET.SC  (W then PRN
ECHO                          prints it, q quits)
ECHO    5  DataPerfect 2.6    WordPerfect Corp's database, sample TEST db
ECHO    6  MS-DOS Kermit 3.14 communications on COM2, the ARM PC's modem
ECHO    7  Build GW-BASIC     MASM 5.10 + LINK 3.65 assemble all 38 modules
ECHO.
ECHO   Licences and where each came from: README.TXT in this directory.
GOTO END
:P1
CD \ELBOW\APPS\BASIC
ELBOW GWBASIC LOAN
GOTO BACK
:P2
CD \ELBOW\APPS\BASIC
ELBOW GWBASIC SPIRAL
GOTO BACK
:P3
CD \ELBOW\APPS\VDE
ELBOW VDE LETTER.TXT
GOTO BACK
:P4
CD \ELBOW\APPS\SC
ELBOW SC BUDGET.SC
GOTO BACK
:P5
CD \ELBOW\APPS\DATAPERF
ELBOW DP TEST
GOTO BACK
:P6
CD \ELBOW\APPS\KERMIT
ELBOW KERMIT
GOTO BACK
:P7
CD \ELBOW\APPS\BASIC\SOURCE
CALL BUILD
GOTO END
:BACK
CD \ELBOW\APPS
:END
