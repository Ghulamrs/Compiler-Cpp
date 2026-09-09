// A virtual function in a class template, which needs the class's *name* in
// three symbols - the vtable, the type_info and the string it points at - and
// a template-id is not a name that can be spelled by counting its letters.
// cxx1 spelled it from the display tag: `.globl _ZTS6P<int>`, which reaches
// the assembler as it stands and is answered with `unexpected token`. So any
// polymorphic class template stopped there, in the assembler rather than in
// the compiler. It was the second entry of tests/open/ to close.
//
// What the ABI actually asks for is the encoding the mangler already writes
// wherever such a type is named in a signature, measured against clang for the
// four shapes that differ from a plain class:
//
//     _ZTS1PIiE          a type argument
//     _ZTS3BoxIiLi3EE    a non-type argument
//     _ZTS6HolderIS_IiEE an argument that is itself a specialization of the
//                        same template, which takes a substitution
//     _ZTSN2ns1RIiEE     one in a namespace - the wrapper `std` does without
//
// The Microsoft spelling is `??_7?$P@H@@6B@`, measured the same way.
extern "C" int printf(const char *, ...);

template <class T> struct P {
    T v;
    P(T x) : v(x) {}
    virtual ~P() {}
    virtual T twice() const { return v + v; }
};

template <class T> struct Holder {
    T v;
    // **By reference on purpose.** A class parameter taken by value is
    // destroyed by the callee on the Microsoft ABI, so this constructor would
    // own an unwind region - and a function other than `main` that owns one
    // emits a `.pdata` entry pointing at a `$cppxdata$` label this compiler
    // never lays down. ml64 answers `A2006: undefined symbol`, and it has
    // nothing to do with the names this case is about. Recorded in CLAUDE.md.
    Holder(const T &x) : v(x) {}
    virtual ~Holder() {}
    virtual int depth() const { return 1; }
};

template <class T, int N> struct Box {
    T a[N];
    virtual ~Box() {}
    virtual int count() const { return N; }
};

namespace ns {
    template <class T> struct R {
        T v;
        R(T x) : v(x) {}
        virtual ~R() {}
        virtual T half() const { return v / 2; }
    };
}

// Dispatched, not inlined: the call goes through the vtable whose symbol this
// case is about.
int through(const P<int> &p) { return p.twice(); }

int main() {
    P<int> a(5);
    P<double> c(1.5);
    Holder<int> hi(3);
    Holder<Holder<int> > hh(hi);
    Box<int, 3> d;
    ns::R<int> e(9);
    printf("%d %d %d %d\n", through(a), hh.depth(), d.count(), e.half());
    printf("%.1f\n", c.twice());
    return 0;
}
