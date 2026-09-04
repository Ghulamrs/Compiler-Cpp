// fcntl.h - the file-control constants, as much of them as a program here has
// asked for.
//
// Only the two translation modes, because only they are reachable: `_setmode`
// in <io.h> is the one function that takes one. The open flags a POSIX
// <fcntl.h> also carries are not here, since `open` is not declared anywhere
// in this library - <stdio.h> is how a program here opens a file.
//
// The values are Microsoft's own and are what libcmt reads. They are numbers
// rather than an enum so that `#if` can see them, which is what a header of
// constants is for.
#ifndef _CXX1_FCNTL_H
#define _CXX1_FCNTL_H

#define _O_TEXT   0x4000
#define _O_BINARY 0x8000

#endif
