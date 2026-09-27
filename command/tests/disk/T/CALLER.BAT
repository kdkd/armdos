@echo off
echo in caller
call callee x y
echo back in caller
chain
echo never printed
