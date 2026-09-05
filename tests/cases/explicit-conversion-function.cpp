// **`explicit` on a conversion function, [class.conv.fct].** An explicit
// conversion is a candidate only where a conversion is asked for by name - a
// cast, or the contextual conversion to bool a condition performs - and never
// as an implicit conversion. Refused by name until now; the conversion itself
// has worked in both directions for a while, and this is the keyword on top.
//
// The two allowed contexts, measured against clang: a condition (`explicit
// operator bool`), and a `static_cast` / C-style cast to the target type.
extern "C" int printf(const char *, ...);

struct Flag {
    int v;
    explicit operator bool() const { return v != 0; }
};

struct Celsius { double t; };
struct Fahrenheit {
    double t;
    explicit operator Celsius() const { Celsius c; c.t = (t - 32.0) * 5.0 / 9.0; return c; }
};

int main() {
    Flag on;  on.v  = 5;
    Flag off; off.v = 0;

    // A condition is the contextual conversion to bool, which an explicit
    // operator bool is made for.
    int a = on  ? 1 : 0;
    int b = off ? 1 : 0;
    int c = !on ? 1 : 0;
    int d = (on && !off) ? 1 : 0;

    Fahrenheit f; f.t = 212.0;
    Celsius viaC   = (Celsius)f;              // C-style cast
    Celsius viaS   = static_cast<Celsius>(f); // static_cast

    printf("%d %d %d %d | %g %g\n", a, b, c, d, viaC.t, viaS.t);
    return 0;
}
