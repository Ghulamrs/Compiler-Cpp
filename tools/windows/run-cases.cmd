@echo off
rem  Build cxx1 with cl and run every case that has recorded output.
rem
rem  Usage:  run-cases.cmd <root>
rem  Writes  <root>\winout\<case>.out for each case, and nothing else. The
rem  comparison is deliberately NOT done here: a .expected file arrives with
rem  Unix line endings and a program's own output leaves through the CRT with
rem  Windows ones, so `fc` reports every case as different. tools/verify-three
rem  pulls these back and diffs them on the Mac, where one `tr -d` settles it.
setlocal enabledelayedexpansion
if "%~1"=="" (echo run-cases.cmd: needs the tree root & exit /b 2)
set ROOT=%~1

call %ROOT%\msvc\build.cmd
if errorlevel 1 exit /b 1

if not exist %ROOT%\winout mkdir %ROOT%\winout
del /q %ROOT%\winout\* 2>nul

for %%f in (%ROOT%\tests\cases\*.expected) do (
    set NAME=%%~nf
    set SKIP=
    rem  A case may say it cannot be compiled for this target, one reason per
    rem  line in <case>.notarget - the same file tests/emit.sh reads. The
    rem  reason is printed rather than swallowed: an exclusion nobody sees is
    rem  an exclusion nobody removes.
    if exist %ROOT%\tests\cases\!NAME!.notarget (
        findstr /C:"x86_64-windows" %ROOT%\tests\cases\!NAME!.notarget >nul 2>&1 && set SKIP=1
    )
    if defined SKIP (
        echo   skip !NAME! for x86_64-windows:
        findstr /C:"x86_64-windows" %ROOT%\tests\cases\!NAME!.notarget
    ) else (
        %ROOT%\cxx1-msvc.exe %ROOT%\tests\cases\!NAME!.cpp -o %ROOT%\winout\!NAME!.exe >%ROOT%\winout\!NAME!.build 2>&1
        if errorlevel 1 (
            echo COMPILE-FAILED !NAME!
        ) else (
            %ROOT%\winout\!NAME!.exe > %ROOT%\winout\!NAME!.out 2>&1
        )
    )
)
echo run-cases.cmd: done
endlocal
