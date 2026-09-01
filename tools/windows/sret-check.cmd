@echo off
rem  Does a cxx1 member function returning a struct agree with cl about which
rem  register holds `this` and which holds the hidden return pointer?
rem
rem  Usage:  sret-check.cmd <root>
rem
rem  This is the one question a same-compiler test cannot ask. cxx1 passed the
rem  return pointer first and `this` second, on both sides of every call it
rem  generated - self-consistent, and the opposite of what cl does, so its
rem  objects were silently incompatible with anything cl compiled. Measured
rem  with clang for this ABI first: `this` in rcx, the return buffer in rdx,
rem  the first written argument in r8.
rem
rem  cxx1 compiles the member function and cl compiles the caller, which is
rem  the direction that fails loudly: cl's caller puts the pointers where cl
rem  believes they go, and a callee that disagrees writes its result through
rem  the object.
setlocal
if "%~1"=="" (echo sret-check.cmd: needs the tree root & exit /b 2)
set ROOT=%~1
set WORK=%ROOT%\winsret
if not exist %WORK% mkdir %WORK%
del /q %WORK%\* 2>nul

rem  The small struct asks the other half of the member rule: cl gives a
rem  class returned from a member function the hidden pointer whatever its
rem  size - Small comes back through rdx, with x moved along to r8 - where a
rem  free function would return the same struct in eax. cxx1 kept the size
rem  test for members and returned it in eax, on both sides of every call it
rem  generated, so only this mixed link can see it.
> %WORK%\holder.cpp echo struct Big { long long a, b, c; };
>> %WORK%\holder.cpp echo struct Small { int v; };
>> %WORK%\holder.cpp echo struct W { long long k; Big get(int x); Small sm(int x); };
>> %WORK%\holder.cpp echo Big W::get(int x) { Big r; r.a = k + x; r.b = k * 2; r.c = x; return r; }
>> %WORK%\holder.cpp echo Small W::sm(int x) { Small r; r.v = (int)k + x; return r; }

> %WORK%\caller.cpp echo #include ^<stdio.h^>
>> %WORK%\caller.cpp echo struct Big { long long a, b, c; };
>> %WORK%\caller.cpp echo struct Small { int v; };
>> %WORK%\caller.cpp echo struct W { long long k; Big get(int x); Small sm(int x); };
>> %WORK%\caller.cpp echo int main() {
>> %WORK%\caller.cpp echo   W w; w.k = 10;
>> %WORK%\caller.cpp echo   Big b = w.get(7);
>> %WORK%\caller.cpp echo   Small s = w.sm(9);
>> %WORK%\caller.cpp echo   printf("%%lld %%lld %%lld %%d\n", b.a, b.b, b.c, s.v);
>> %WORK%\caller.cpp echo   return 0;
>> %WORK%\caller.cpp echo }

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (echo sret-check: no vcvars64 & exit /b 1)

%ROOT%\cxx1-msvc.exe -c %WORK%\holder.cpp -o %WORK%\holder.obj
if errorlevel 1 (echo sret-check: cxx1 refused the member function & exit /b 1)
cl /nologo /EHsc /c /Fo%WORK%\caller.obj %WORK%\caller.cpp >nul
if errorlevel 1 (echo sret-check: cl refused the caller & exit /b 1)
link /nologo /OUT:%WORK%\prog.exe %WORK%\holder.obj %WORK%\caller.obj
if errorlevel 1 (echo sret-check: the link failed & exit /b 1)
%WORK%\prog.exe > %WORK%\out.txt 2>&1
type %WORK%\out.txt
