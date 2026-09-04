// io.h - the Microsoft CRT's low-level I/O, as much of it as a program here
// has asked for.
//
// Not Microsoft's header, and not a copy of one, for the reason stdio.h gives
// at length: what a program wants from it is the prototypes, and a prototype
// is ordinary C. These must agree with the CRT's, because the program links
// against libcmt - a declaration that lied would fail at the link or, worse,
// pass the wrong bytes.
//
// This exists for `_setmode(_fileno(stdout), _O_BINARY)`, which is how a
// Windows program stops the CRT translating '\n' into "\r\n" on the way out.
// A compiler whose output is compared byte for byte against a recorded file
// has to do that, and there is no portable spelling of it.
//
// It is a Windows header and nothing else declares these, so a program that
// includes it and is built for another target links against symbols that
// platform has never had - which is the honest failure and the same one a
// real toolchain gives.
#ifndef _CXX1_IO_H
#define _CXX1_IO_H

#ifdef __cplusplus
extern "C" {
#endif

// Sets the translation mode of an already-open file descriptor and answers
// the previous one, or -1. The mode is one of the _O_ values in <fcntl.h>.
int _setmode(int fd, int mode);

#ifdef __cplusplus
}
#endif

#endif
