// **`long long` is a type of its own, not a spelling of `long`** - and on the
// two Itanium targets it is the same width, which is exactly why the gap was
// easy to miss. `<ostream>` had overloads down to `unsigned long` and no
// further, so a 64-bit value written through `<<` reached every arithmetic
// overload by a conversion and none of them better than the rest: the call was
// ambiguous, and the diagnostic listed seven candidates without naming the one
// that was missing.
//
// A program that pins its own integer width writes this all day - three of
// Compiler++'s sixteen sources stop here, all of them printing a `vmword`,
// which is `long long` everywhere but MSVC.
//
// The extraction pair is here for the same reason and was added with it: an
// `in >> x` for a `long long` lvalue had the same seven-way tie.
#include <iostream>
#include <sstream>
#include <string>

int main() {
    // A value no `double` can hold exactly and no 32-bit type can hold at all,
    // so a conversion to any of the other overloads would show.
    long long a = -9007199254740993LL;
    unsigned long long b = 18446744073709551615ULL;
    long c = 42;

    std::ostringstream ss;
    ss << "n=" << a << " u=" << b << " l=" << c << " i=" << 7;
    std::cout << ss.str() << std::endl;

    std::istringstream in("-5 6");
    long long x;
    unsigned long long y;
    in >> x >> y;
    std::cout << x << " " << y << std::endl;
    return 0;
}
