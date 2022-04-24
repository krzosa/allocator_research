@echo off


set f=-nologo -GR- -EHa- -Oi -GS- -Zi -W4 
clang-cl main.cpp %f% -fdiagnostics-absolute-paths

rem cl main.cpp %f% -FC