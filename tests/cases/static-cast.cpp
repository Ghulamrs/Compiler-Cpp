// static_cast to a non-reference type does what the C-style cast does, which
// is the implicit conversion machinery - a subset of what static_cast means in
// C++, and the subset is refused out loud rather than half-done.
extern "C" int printf(const char *, ...);

struct Base { int b; };
struct Derived : Base { int d; };

int main() {
    printf("%d\n", static_cast<int>(3.9));
    printf("%d\n", static_cast<int>('A'));
    printf("%lld\n", static_cast<long long>(1) << 40);

    double d = static_cast<double>(7) / static_cast<double>(2);
    printf("%.1f\n", d);

    Derived x;
    x.b = 11;
    x.d = 22;
    Base *p = static_cast<Base *>(&x);
    printf("%d\n", p->b);

    // A reference cast that does not move: an lvalue reference cast leaves an
    // lvalue, and writing through it writes to the object named.
    int i = 5;
    static_cast<int &>(i) = 6;
    printf("%d\n", i);
    return 0;
}
