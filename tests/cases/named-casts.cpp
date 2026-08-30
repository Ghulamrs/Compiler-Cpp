// const_cast and reinterpret_cast. static_cast was already here; dynamic_cast
// is refused by name, and dynamic-cast-refused.cpp says why.
//
// **Neither of these two generates anything.** Every conversion they allow is
// between things of the same size, so the value is unchanged and what moves is
// the type. That is the whole reason C++ gave each of them a name of its own
// rather than letting the C cast do all of it silently: the cast is a claim
// about what the program means, and the compiler's job is to say which claim
// was made.
//
// **The line between them is const.** `reinterpret_cast` may not take it off -
// `const_cast` is what does that, and doing both means writing both. Getting
// that wrong in either direction would make one of the two redundant.
//
// **Byte order.** The reinterpretations below read an int's bytes as a char,
// which is little-endian-dependent. All three targets here are little-endian
// (x86_64 twice and arm64 as configured), so 0x41 is the first byte and the
// answer is 'A'. A case like this on a big-endian target would be wrong, and
// that is a property of the test rather than of the cast.

extern "C" int printf(const char *, ...);

int writeThrough(int *p) { *p = 7; return *p; }

int byName(int a) { return a * 2; }

int main(void) {
    // const_cast, through a pointer. The object really is non-const here -
    // writing through a pointer to something that was declared const is
    // undefined, and the cast does not make it defined.
    int mutableStorage = 1;
    const int *readOnlyView = &mutableStorage;
    int written = writeThrough(const_cast<int *>(readOnlyView));

    // ...and the other way, which needs no cast but is legal to write.
    int n = 3;
    const int *added = const_cast<const int *>(&n);

    // Through a reference. The object is not moved and its address is not
    // changed; the expression comes back naming the same storage with the
    // const gone.
    int counter = 10;
    const int &frozen = counter;
    int &thawed = const_cast<int &>(frozen);
    thawed = 11;

    // Nested, where the qualifiers sit at more than one level. [expr.const.cast]
    // asks only that the two be *similar* - the same shape ending at the same
    // type - and const at any level may go.
    int *inner = &n;
    const int *const *outer = &inner;
    int **plain = const_cast<int **>(outer);

    // reinterpret_cast between pointers, and the bytes underneath are read.
    int letters = 0x41;
    char *asBytes = reinterpret_cast<char *>(&letters);

    // A pointer as an integer, and back. The integer has to be wide enough to
    // hold the address - reinterpret-cast-narrow-refused.cpp pins the other
    // half of that.
    //
    // **`long long` and not `long`.** A `long` is 8 bytes on x86_64-linux and
    // arm64-darwin and 4 on x86_64-windows, so writing `long` here compiles on
    // two targets and is refused on the third - which `emit.sh` reported
    // before any box was asked. `long long` is 8 everywhere.
    long long address = reinterpret_cast<long long>(&letters);
    int *recovered = reinterpret_cast<int *>(address);

    // Through a reference, which is the same object under another type.
    char &firstByte = reinterpret_cast<char &>(letters);

    // A function pointer, which is a pointer this cast is allowed to move.
    int (*original)(int) = &byName;
    void (*erased)(void) = reinterpret_cast<void (*)(void)>(original);
    int (*back)(int) = reinterpret_cast<int (*)(int)>(erased);

    // Keeping the const is not the same as taking it off, and is allowed.
    const int frozenValue = 5;
    const char *frozenBytes = reinterpret_cast<const char *>(&frozenValue);

    printf("%d %d %d %d\n", written, *added, counter, **plain);
    printf("%d %d %d\n", *asBytes, *recovered, firstByte);
    printf("%d %d %d\n", back(21), *frozenBytes, address != 0);
    return 0;
}
