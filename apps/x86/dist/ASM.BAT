@ECHO OFF
REM ASM name - assemble, link and run name.ASM with the MS-DOS 2.0 tools
ELBOW MASM %1;
ELBOW LINK %1;
ELBOW EXE2BIN %1.EXE %1.COM
ELBOW %1.COM
