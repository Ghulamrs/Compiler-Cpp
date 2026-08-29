// `static_cast<S &&>(f())` is legal C++ - the operand is already a prvalue and
// the cast is an identity - but it needs a temporary materialised for the
// reference to bind to, and this compiler has no path that does that here.
// Refused by name rather than half-built.
struct S { int a; };
S make() { S s; s.a = 1; return s; }
int main() {
    S &&r = static_cast<S &&>(make());
    return r.a;
}
