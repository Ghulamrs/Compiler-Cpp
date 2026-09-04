// **`std::hex` and its two neighbours**, which are the whole of the formatting
// state this library has. The standard's `ios_base` carries width, fill,
// precision and a dozen flags beside the base; none of those is here, and a
// program that reaches for one gets a diagnostic rather than silence.
//
// The manipulator needed no new machinery: `operator<<` taking a function
// pointer already existed for `endl`, and `ss << std::hex` is that overload
// calling this one.
//
// **In hexadecimal and octal a signed value prints its unsigned
// reinterpretation** - [ostream.inserters.arithmetic] converts through the
// corresponding unsigned type - so -1 is `ffffffff` rather than `-1`. And the
// base is sticky, holding until `dec` or `oct` says otherwise, which is why a
// program that writes one usually writes its partner afterwards.
//
// Zero and ten both mean decimal, so the aggregate initialisers in <iostream> -
// which name four members and leave this one value-initialised - start in
// decimal without naming it.
#include <iostream>
#include <sstream>
#include <string>

int main() {
    std::ostringstream ss;
    long long a = 48879;
    ss << "0x" << std::hex << a;
    std::cout << ss.str() << std::endl;

    std::ostringstream t;
    t << std::hex << 255 << " " << -1 << " " << std::oct << 8 << " "
      << std::dec << 255 << " sticky " << 16;
    std::cout << t.str() << std::endl;

    unsigned long u = 4294967295UL;
    std::ostringstream w;
    w << std::hex << u;
    std::cout << w.str() << std::endl;

    // On `cout` itself, which is the aggregate rather than a constructed stream.
    std::cout << std::hex << 4095 << std::dec << " " << 4095 << std::endl;
    return 0;
}
