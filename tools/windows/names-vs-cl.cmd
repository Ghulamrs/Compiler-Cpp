@echo off
rem  Every case's symbols, from cxx1 and from cl, side by side.
rem
rem  Usage:  names-vs-cl.cmd <root>
rem  Writes  <root>\winnames\<case>.mine.txt  dumpbin /symbols on cxx1's object
rem          <root>\winnames\<case>.cl.txt    dumpbin /symbols on cl's object
rem          <root>\winnames\<case>.skip      when cl would not compile it
rem
rem  **Why this exists when names.sh already checks the names.** That one asks
rem  clang with -target x86_64-pc-windows-msvc, which is a second
rem  implementation of the Microsoft ABI. This asks the ABI itself. CLAUDE.md
rem  says a Microsoft question goes to cl first and clang second, and until now
rem  every mangled name this compiler emits was checked only against the
rem  second - on a Mac, by a compiler that is not the one anybody links with.
rem  The two have already been seen to disagree: the secondary vtable and the
rem  biased `this` in thunk.cpp were settled by cl and not by clang.
rem
rem  **No comparison happens here**, for the reason run-cases.cmd gives about
rem  its own output: cmd is a poor place to cut up text and this box has no
rem  awk. The raw listings go back to the Mac and tools/verify-three does the
rem  reading, where one grep settles what a screenful of `for /f` would not.
rem
rem  /std:c++14 because cl has no C++11 mode - its floor is c++14 - so cl
rem  answers about the ABI and not about which language version a construct
rem  belongs to. /GR- and /EHsc- keep RTTI and exception tables out, the same
rem  flags measure.cmd uses and the same reason tools/mangled-names asks clang
rem  for -fno-rtti -fno-exceptions: this is a question about names.
setlocal enabledelayedexpansion
if "%~1"=="" (echo names-vs-cl.cmd: needs the tree root & exit /b 2)
set ROOT=%~1

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (echo names-vs-cl.cmd: no vcvars & exit /b 1)

if not exist %ROOT%\winnames mkdir %ROOT%\winnames
del /q %ROOT%\winnames\* 2>nul

for %%f in (%ROOT%\tests\cases\*.expected) do (
    set NAME=%%~nf
    set SKIP=
    rem  The same two exclusion files the other runners read. `.notarget` says
    rem  this case is not compiled for this target at all; `.nocl` says cl and
    rem  cxx1 differ here for a reason somebody wrote down - the counterpart of
    rem  `.nonames`, which records a difference against clang.
    if exist %ROOT%\tests\cases\!NAME!.notarget (
        findstr /C:"x86_64-windows" %ROOT%\tests\cases\!NAME!.notarget >nul 2>&1 && set SKIP=1
    )
    if exist %ROOT%\tests\cases\!NAME!.nocl set SKIP=1

    if defined SKIP (
        echo skipped > %ROOT%\winnames\!NAME!.skip
    ) else (
        %ROOT%\cxx1-msvc.exe -c %ROOT%\tests\cases\!NAME!.cpp -o %ROOT%\winnames\!NAME!.obj >nul 2>&1
        if errorlevel 1 (
            echo cxx1-refused > %ROOT%\winnames\!NAME!.skip
        ) else (
            cl /nologo /c /std:c++14 /GR- /EHsc- /Fo:%ROOT%\winnames\!NAME!.cl.obj %ROOT%\tests\cases\!NAME!.cpp >nul 2>&1
            if errorlevel 1 (
                rem  cl refusing is not a cxx1 failure: this corpus is C++11 and
                rem  cl has no C++11 mode. Recorded so the count says how much
                rem  was really compared.
                echo cl-refused > %ROOT%\winnames\!NAME!.skip
            ) else (
                dumpbin /nologo /symbols %ROOT%\winnames\!NAME!.obj > %ROOT%\winnames\!NAME!.mine.txt 2>&1
                dumpbin /nologo /symbols %ROOT%\winnames\!NAME!.cl.obj > %ROOT%\winnames\!NAME!.cl.txt 2>&1
            )
        )
    )
)

echo names-vs-cl.cmd: done
endlocal
