// A local class inside a *member* function, which is where both ABIs wrap a
// member's own name rather than a free function's - `_ZZN5Outer1mEvEN1A3getEv`.
//
// It is also the case that found a memory bug older than local classes. The
// signature a definition looks up was held as a pointer into the vector of
// every function while its body was read; declaring anything during that body
// can grow the vector and move it. Nothing declared anything there until a
// class could be defined inside a function - its member functions are declared
// exactly then - and `Outer::m` came out under whatever string was left at
// that address: `m` in one build and `a` in the next.
extern "C" { int printf(const char *, ...); }

struct Outer {
    int scale;
    int m(int n) {
        struct A { int v; int get() { return v * 2; } };
        A a;
        a.v = n + scale;     // both the parameter and a member, after the class
        return a.get();
    }
};

int main(void) {
    Outer o;
    o.scale = 3;
    printf("%d\n", o.m(4));
    return 0;
}
