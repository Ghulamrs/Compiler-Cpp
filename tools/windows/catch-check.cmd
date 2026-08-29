@echo off
rem  Can cxx1 catch, on this target, what cxx1 threw?
rem
rem  Usage:  catch-check.cmd <root>
rem
rem  The mirror of throw-check.cmd, and the thing rung 6.5b is for. That one
rem  has cl catch what cxx1 threw, which tests the ThrowInfo chain and nothing
rem  about this frame; this one compiles both halves with cxx1, so what is
rem  under test is the funclet, the FH3 tables and the unwind data that lets
rem  the runtime find them. It either prints what it should or the program
rem  ends, which is why the check is a run and not a listing.
setlocal
if "%~1"=="" (echo catch-check.cmd: needs the tree root & exit /b 2)
set ROOT=%~1
set WORK=%ROOT%\wincatch
if not exist %WORK% mkdir %WORK%
del /q %WORK%\* 2>nul

> %WORK%\prog.cpp echo extern "C" int printf(const char *, ...);
>> %WORK%\prog.cpp echo void risky(int n) { if (n == 1) throw 7; }
>> %WORK%\prog.cpp echo int main() {
>> %WORK%\prog.cpp echo   printf("before\n");
>> %WORK%\prog.cpp echo   try { risky(1); printf("not reached\n"); }
>> %WORK%\prog.cpp echo   catch (int e) { printf("caught %%d\n", e); }
>> %WORK%\prog.cpp echo   printf("after\n");
>> %WORK%\prog.cpp echo   return 0;
>> %WORK%\prog.cpp echo }

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (echo catch-check: no vcvars64 & exit /b 1)

%ROOT%\cxx1-msvc.exe %WORK%\prog.cpp -o %WORK%\prog.exe
if errorlevel 1 (echo catch-check: cxx1 refused it & exit /b 1)
%WORK%\prog.exe
if errorlevel 1 (echo catch-check: the program failed & exit /b 1)
