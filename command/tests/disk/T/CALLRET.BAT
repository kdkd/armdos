@echo off
call callee %1
echo after call errorlevel check
ret 7
if errorlevel 7 echo seven
