// **A unity build, for x86_64-windows only.**
//
// This compiler has no COMDAT or weak linkage on that target, so a member
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
