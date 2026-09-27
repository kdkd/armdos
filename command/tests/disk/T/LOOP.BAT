@echo off
:top
if "%1"=="" goto done
echo arg %1
shift
goto top
:done
echo finished
