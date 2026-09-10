#ifndef CXX1_VERSION_H
#define CXX1_VERSION_H

// **The release, in one place.** The compiler prints these, `tools/seal` reads
// them out of this file rather than keeping a second copy, and README.1ST
// quotes them - so a release is renamed by editing three lines here and
// running `tools/seal write`.
//
// The date is the release's own, written the way the request wrote it:
// day-month-year.
//
// **1.2 rather than a re-sealed 1.1**, and the seal is the reason: 1.1 named a
// set of sources and this one is not that set. A version that names two
// different trees is the one thing a seal exists to stop.
#define CXX1_VERSION      "1.2"
#define CXX1_SEAL_DATE    "10-09-2026"
#define CXX1_SEAL_FILE    "cxx1-1.2.dat"

// The line every run prints before it starts. First line, and exactly this.
#define CXX1_BANNER "c2026 G. R. Akhtar - ISO C++ 11,  Compiler"

#endif
