@echo off
rem  **Assemble, link and run one .asm on the Windows box, in seconds.**
rem
rem  tools/verify-three rebuilds the compiler on two remote machines to answer
rem  one question, which is twenty minutes for a yes or no. A question about
rem  *this target's ABI* does not need a compiler there at all: cxx1 on the Mac
rem  writes the MASM, and only assembling, linking and running have to happen
rem  here. That is what this does, and it turned a twenty-minute loop into a
rem  fifteen-second one while the Microsoft unwind schedule was being measured.
rem
rem  From the Mac:
rem    ./cxx1.exe -S -arch x86_64-windows p.cpp -o p.asm
rem    scp p.asm windows:C:/cxx1/fast/
rem    ssh windows "C:\cxx1\fast\asm-run.cmd p"
rem
rem  Use verify-three for the suites and before committing; use this while
rem  chasing one answer.
setlocal
if "%~1"=="" (echo asm-run.cmd: needs the base name of a .asm & exit /b 2)
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (echo asm-run: no vcvars64 & exit /b 1)
cd /d C:\cxx1\fast
ml64 /nologo /c /Fo %1.obj %1.asm
if errorlevel 1 (echo asm-run: the assembler failed & exit /b 1)
link /nologo /subsystem:console /out:%1.exe %1.obj libcmt.lib libucrt.lib libvcruntime.lib kernel32.lib legacy_stdio_definitions.lib
if errorlevel 1 (echo asm-run: the link failed & exit /b 1)
%1.exe
echo exit=%errorlevel%
