@echo off
if "%1"=="yes" echo said yes
if not "%1"=="yes" echo did not say yes
if exist a.txt echo a.txt exists
if not exist nofile.txt echo nofile.txt missing
ret 5
if errorlevel 5 echo level 5 or more
if errorlevel 6 echo level 6 or more
if not errorlevel 6 echo level below 6
if NOT not==not echo double not
if a==
echo after syntax error
