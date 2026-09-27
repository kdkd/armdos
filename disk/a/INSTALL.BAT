@ECHO OFF
REM Copies ARM-DOS from the Startup diskette to drive C:.
IF NOT EXIST C:\COMMAND.COM GOTO NOSYS
ECHO Installing ARM-DOS 4.00 on drive C: ...
IF NOT EXIST C:\DOS\COMMAND.COM MD C:\DOS
COPY A:\DOS\*.* C:\DOS > NUL
COPY A:\COMMAND.COM C:\DOS > NUL
COPY A:\SETUP\DOSSHELL.BAT C:\DOS > NUL
IF NOT EXIST C:\CONFIG.SYS COPY A:\SETUP\CONFIG.SYS C:\ > NUL
IF NOT EXIST C:\AUTOEXEC.BAT COPY A:\SETUP\AUTOEXEC.BAT C:\ > NUL
ECHO.
ECHO Done. Take the diskette out (the button on drive A:) and press
ECHO Ctrl+Alt+Del to start ARM-DOS from the hard disk.
GOTO END
:NOSYS
ECHO Drive C: cannot start DOS yet. Type FORMAT C: /S first (this erases
ECHO everything on C:), or SYS C: to keep what is there, then INSTALL again.
:END
