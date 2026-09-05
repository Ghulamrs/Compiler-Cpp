// The C library under `std::` - `<cstddef>` and the four beside it.
//
// **These are the only headers in `include/` that are not classes**, and they
// are the layer everything above them rests on. Each is `lib/`'s C header
// included, and then every name it declares brought into `std` by a
// using-declaration - which is the natural spelling of what the standard says
// these headers do, and the reason a using-declaration was worth building.
//
// The point of a case rather than trust: a wrapper naming something its C
// header does not declare will not compile, and one that misses a name is a
// hole nothing finds until a program wants it. So every function is called here
// and every typedef used, both qualified and - for the ones a program is likely
// to write unqualified - through the global name too, which is still declared:
// `<cstring>` puts `memcpy` in `std` without taking `::memcpy` away.
//
// `<cstring>` is not `<string>`. They are unrelated headers with confusable
// names, and a program wanting the class and reaching for this one gets no
// error and no class.

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cctype>

extern "C" int printf(const char *, ...);

int main(void) {
    // <cstddef>: the types, which are types and not values
    std::size_t n = sizeof(int);
    std::ptrdiff_t d = 3;
    printf("%d %d %d\n", (int)n, (int)d, (int)sizeof(std::size_t));

    // <cstring>
    char buf[16];
    std::strcpy(buf, "abcd");
    char other[16];
    std::memcpy(other, buf, 5);
    std::memset(other + 1, 'x', 2);
    printf("%d %s %s %d %d\n", (int)std::strlen(buf), buf, other,
           std::strcmp(buf, "abcd"), (int)(std::strchr(buf, 'c') - buf));

    // <cctype>
    printf("%d %d %d %d %d\n", std::isdigit('7') != 0, std::isalpha('q') != 0,
           std::isspace(' ') != 0, std::toupper('a'), std::isxdigit('f') != 0);

    // <cstdlib>
    printf("%d %d %d\n", std::atoi("42"), (int)std::labs(-7), std::abs(-3));

    // <cmath>, printed as integers so no formatting choice can differ
    printf("%d %d %d %d\n", (int)std::sqrt(144.0), (int)std::pow(2.0, 10.0),
           (int)std::floor(3.7), (int)std::ceil(3.2));
    printf("%d %d %d\n", (int)(std::fabs(-2.5) * 2), (int)std::fmod(7.0, 3.0),
           (int)(std::log10(1000.0) + 0.5));

    // The global names are still there, which is what a wrapper must not break
    printf("%d %d\n", (int)strlen("ab"), isdigit('3') != 0);
    return 0;
}
