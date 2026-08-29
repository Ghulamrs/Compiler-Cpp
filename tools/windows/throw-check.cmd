@echo off
rem  Can something cxx1 threw be caught by cl?
rem
rem  Usage:  throw-check.cmd <root>
rem
rem  The Windows half of tools/unwind-check, and it exists for the same
rem  reason: this is machinery that either works or ends the program, so the
rem  check has to be a run. cxx1 compiles the thrower and cl compiles the
rem  catcher, because cxx1 cannot catch on this target yet - the funclets and
rem  the FuncInfo tables are the next step. What is being checked is the
rem  ThrowInfo chain: a wrong descriptor is caught by nobody.
setlocal
if "%~1"=="" (echo throw-check.cmd: needs the tree root & exit /b 2)
set ROOT=%~1
set WORK=%ROOT%\winthrow
if not exist %WORK% mkdir %WORK%
del /q %WORK%\* 2>nul

> %WORK%\risky.cpp echo void risky(int n) { if (n ^> 0) throw n; }
>> %WORK%\risky.cpp echo void riskyDouble(double x) { if (x ^> 0) throw x; }

> %WORK%\catcher.cpp echo #include ^<stdio.h^>
>> %WORK%\catcher.cpp echo void risky(int n);
>> %WORK%\catcher.cpp echo void riskyDouble(double x);
>> %WORK%\catcher.cpp echo int main() {
>> %WORK%\catcher.cpp echo   try { risky(7); } catch (int e) { printf("int %%d\n", e); }
>> %WORK%\catcher.cpp echo   try { riskyDouble(2.5); } catch (double d) { printf("double %%.1f\n", d); }
>> %WORK%\catcher.cpp echo   risky(0);
>> %WORK%\catcher.cpp echo   return 0;
>> %WORK%\catcher.cpp echo }

rem  The same line build.cmd uses, and for the same reason: cl and link reach
rem  PATH only after vcvars64 has run.
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (echo throw-check: no vcvars64 & exit /b 1)

%ROOT%\cxx1-msvc.exe -c %WORK%\risky.cpp -o %WORK%\risky.obj
if errorlevel 1 (echo throw-check: cxx1 refused the thrower & exit /b 1)
cl /nologo /EHsc /c /Fo%WORK%\catcher.obj %WORK%\catcher.cpp >nul
if errorlevel 1 (echo throw-check: cl refused the catcher & exit /b 1)
link /nologo /OUT:%WORK%\prog.exe %WORK%\risky.obj %WORK%\catcher.obj
if errorlevel 1 (echo throw-check: the link failed & exit /b 1)
%WORK%\prog.exe > %WORK%\out.txt 2>&1
type %WORK%\out.txt
