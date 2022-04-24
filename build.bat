@echo off


set f=-nologo -GR- -EHa- -Oi -GS- -Zi -W4 -Wno-unused-function 
clang-cl -O2 main.cpp %f% -fdiagnostics-absolute-paths

rem cl main.cpp %f% -FC