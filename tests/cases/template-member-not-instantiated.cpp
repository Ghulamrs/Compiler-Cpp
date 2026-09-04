// **[temp.inst]/2: instantiating a class template instantiates the declarations
// of its members, not the definitions.** A member's body is compiled only where
// something calls it - which is what lets `std::vector<T>` hold a `T` with no
// default constructor while still declaring `vector(size_type)`, whose body
// says `T()`.
//
// cxx1 gated a replayed body on the member's **name**, and every constructor of
// a class shares one key - so `Box<NoDefault> b;` marked the key used and
// replayed *every* constructor's body with it. The one nothing called was
// compiled for a `T` it cannot be compiled for, and the class would not
// instantiate at all. Four of Compiler++'s sixteen sources broke this way the
// moment `vector(size_type)` was added to the header.
//
// The gate is the overload now, recorded when the member is declared: the body
// carries the index of the signature its declaration added, and falls back to
// the name where the declaration added none.
extern "C" int printf(const char *, ...);

struct NoDefault {
    int n;
    NoDefault(int v);
};
NoDefault::NoDefault(int v) : n(v) {}

template <class T> struct Box {
    T held;
    Box() : held(0) {}
    // Never called for Box<NoDefault>. Its body would not compile for that T,
    // and the standard says it is never asked to.
    explicit Box(int) { held = T(); }
    // Nor is this, and a member function is the easier half - it has a key of
    // its own, so the name gate was already right for it.
    T copy() { return T(); }
    int get() { return held.n; }
};

// The same class with a T that does have a default constructor, so the bodies
// above are reachable and must still work when something does call them.
struct HasDefault {
    int n;
    HasDefault();
};
HasDefault::HasDefault() : n(4) {}

int main() {
    Box<NoDefault> a;
    Box<HasDefault> b(1);
    printf("%d %d %d\n", a.get(), b.get(), b.copy().n);
    return 0;
}
