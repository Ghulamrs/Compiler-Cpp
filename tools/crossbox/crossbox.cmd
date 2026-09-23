@echo off
rem Windows leg of crossbox.sh: vcvars, then the script under Git bash.
( call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul ) < NUL
"C:\Program Files\Git\bin\bash.exe" -c "sh /c/cxx1dev/crossbox.sh windows /c/cxx1dev/x /c/cxx1dev/Compiler++ /c/cxx1dev/C++/cxx1-msvc.exe /c/o12study/bench.cpp all" < NUL
