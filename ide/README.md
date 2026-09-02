# IDE projects for cxx1

Two projects that build the whole compiler, from `ide/` inside the tree: every
path in them is `..` and one more step. Nothing in the build depends on them -
`make`, `make test` and `tools/verify-three` are unchanged - and both were built
and *run* before this file was written, see "What was measured" below.

    cxx1.xcworkspace / cxx1.xcodeproj    Xcode, on macOS
    cxx1.sln / cxx1.vcxproj              Visual Studio 2022, on Windows
    generate.py                          writes all four from the source tree
    build-vs.cmd, check-vs.cmd           MSBuild and a smoke test, from a shell

## Building

Open `cxx1.xcworkspace` and press Run, or from a terminal:

    xcodebuild -project cxx1.xcodeproj -scheme cxx1 -configuration Release build

Open `cxx1.sln` in Visual Studio 2022 and build, or from a shell on that box:

    C:\path\to\the\checkout\ide\build-vs.cmd    (Release; pass Debug for the other)

**Run `build-vs.cmd` by its full path and with no `cmd /c` in front.** The ssh
shell on the Windows box is already cmd, and a chain of quoted paths typed at it
loses a quote - the same note msvc\build.cmd carries, and the reason both of
these are files rather than command lines.

## The flags are the Makefile's, not Xcode's or MSBuild's

Each project carries the line the tree is already gated on, and nothing else:

* `-std=c++14 -O2 -Wall -Wextra -pedantic` with warnings as errors, and
  `CXX1_INCLUDE_DIR` pointing at `../C++/lib`, which the driver compiles into
  the binary as an absolute path.
* **Xcode's own template adds `-Wshorten-64-to-32`, which is off here.** It is
  not in `-Wall -Wextra`, and it fires on the bitfield arithmetic that is
  deliberately done in `long long` and narrowed after a check. A project that
  built this tree under a different warning set would be a fourth opinion that
  nothing else gates on.
* MSVC gets `/std:c++14 /permissive- /EHsc /W4 /WX` and the five disabled
  warnings from `msvc\build.cmd`, which are inherited from Compiler-C and fire
  on code this tree forked rather than wrote. The include directory is spelled
  with forward slashes through an MSBuild `Replace`, because it becomes a C
  string literal and a backslash there starts an escape.

## Regenerating

The Makefile finds its sources with a wildcard, so a project listing them by
hand rots the first time somebody adds a file:

    ./generate.py            rewrite all four from what is on disk
    ./generate.py --check    say whether they are current, and change nothing

## What was measured

* **Xcode**: `xcodebuild ... build` succeeded, and the binary it produced was
  put in the checkout as `cxx1.exe` and run against the whole case suite -
  **266 passed, 0 failed**. It builds the compiler, not merely something that
  links.
* **Visual Studio 2022**: `build-vs.cmd` compiled all 28 sources at `/W4 /WX`
  on the Windows box, and `check-vs.cmd` then had that binary compile
  `tests/cases/class.cpp`, assemble it with ml64, link it and run it - the same
  four lines the Mac build prints. Both were run **inside the sandbox
  `tools/verify-three` unpacks**, which is what the tar list now ships them for.

One transient worth knowing about, since it will look alarming if it happens to
you: on one Windows run, 158 of the 263 cases reported "did not run" and the
next run of the same tree passed all 263. Nothing here explains it and nothing
was changed between the two, so it is recorded rather than diagnosed - the
shape is the one CLAUDE.md already describes for executables that are new on
that box.

## What this is not

**Not what a change is judged by.** `make test` and `tools/verify-three` are,
and neither runs anything here; these projects exist so the compiler can be read
and stepped through in an IDE, not so it can be gated by one. What they do carry
is the same flags, so a build that passes here is the build the suites ran.

`tools/verify-three` ships this directory to the other two boxes with the rest
of the tree, which is what makes `build-vs.cmd` usable inside a verify sandbox
on the Windows machine. `ide/build/`, `xcuserdata` and `.vs` are ignored.
