@echo off
rem  cl as the measurement venue for the Microsoft ABI.
rem
rem  Usage:  measure.cmd <basename>
rem  Reads   C:\Users\GRA\source\measure\<basename>.cpp
rem  Writes  <basename>.asm beside it and prints the assembly listing and the
rem  external symbols cl put in the object.
rem
rem  One argument and no quotes anywhere on purpose: a quoted argument through
rem  ssh arrives with a quote still attached to %1 and fails the first test.
rem
rem  /std:c++14 rather than c++11 - cl has no C++11 mode at all, its floor is
rem  c++14. So cl answers about the Microsoft ABI, not about which language
rem  version a construct belongs to; clang -std=c++11 is what answers that.
rem  /GR- /EHsc- keeps RTTI and exception data out of the listing, the same
rem  reason tools/mangled-names asks clang with -fno-rtti -fno-exceptions.

setlocal
if "%~1"=="" (echo measure.cmd: needs a basename & exit /b 2)
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d C:\Users\GRA\source\measure
del /q %~1.asm %~1.obj 2>nul

cl /nologo /c /std:c++14 /GR- /FAsc /Fa%~1.asm /Fo:%~1.obj %~1.cpp
if errorlevel 1 (echo *** cl refused it *** & exit /b 1)

echo === listing ===
type %~1.asm
echo === external symbols ===
dumpbin /nologo /symbols %~1.obj | findstr /C:"External"
endlocal
