// **A unity build, for `-masm=masm` on x86_64-windows.**
//
// It is no longer the default path: since 1.2 that target assembles through
// the GNU spelling, which can mark a definition COMDAT, and Makefile.windows
// builds one object per source like the other three. This file is what
// `-masm=masm` still needs, ml64 having no COMDAT directive at all.
//
// With the MASM spelling this compiler has no way to fold a definition, so a member
// function defined inside its class - `std::string`'s own, in this tree's
// <string> - becomes an ordinary strong symbol in every object that includes
// the header. Two such objects do not link: `link.exe` answers LNK2005 for
// every one of them. The two Itanium targets emit weak definitions and have no
// such trouble, which is why Makefile and Makefile.macos and Makefile.linux
// build one object per source and only this target's does not.
//
// Compiling the whole program as one translation unit is the way round it, and
// is what Makefile.windows does. It is a real technique - a "unity build" - and
// costs a full rebuild for any edit, which for three files is nothing.
//
// README.md, "Known shortcomings in 1.1", is where this is written down.
#include "shapes.cpp"
#include "report.cpp"
#include "demo.cpp"
