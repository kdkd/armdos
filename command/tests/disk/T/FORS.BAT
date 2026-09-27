@echo off
for %%f in (a b c) do echo item %%f
for %%f in (*.txt) do echo file %%f
for %%x in (\t\sub\*.*) do echo path %%x
for %%f in () do echo never
for %%f in (x) echo bad
