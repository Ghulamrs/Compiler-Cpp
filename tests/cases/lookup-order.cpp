// [basic.lookup.unqual]/1 and [basic.scope.hiding]/1: the nearest declaration
// wins. There are three scopes for an unqualified name in a member function
// and they are asked in this order - the block, then the class, then the
// namespace - and this parser used to ask them in very nearly the reverse:
// namespace-scope enumerators first, then locals and globals together, then
// members and only if no global of that name existed.
//
// Both consequences were silent. An enumerator hid a local and a parameter,
// so `enum { A = 1 }; int A = 5;` printed 1. A global hid a data member, so a
// class with an `int k` in a program with an `int k` at file scope read the
// global - which also made a `[k]` capture and a `[this]` capture read it.
// Neither said a word; each is four or five lines of ordinary C++.
//
// What each line below pins is which of the three scopes owns the name.
extern "C" int printf(const char *, ...);

enum { A = 1, K = 2, E = 7 };
int k = 10;
int shadowed = 100;

// A parameter and a local each hide a namespace-scope enumerator.
static int fromParameter(int K) { return K; }

struct S {
    int k;
    int shadowed;
    // Out of line, because a constructor written inside its class is inline
    // and clang then emits only its C2 form on x86_64-linux where cxx1 emits
    // C1 and C2 - a difference about emission the names suite would report as
    // though it were about mangling.
    S();
    // A member hides a global of the same name, reached three ways: directly,
    // through a captured `this`, and through a by-copy capture of the member.
    int direct() const { return k; }
    int viaThisCapture() const { auto f = [this]() { return k; }; return f(); }
    int viaValueCapture() const { int m = k; auto f = [m]() { return m; }; return f(); }
    // A local inside a member function hides the member in its turn.
    int localWins() const { int k = 55; return k; }
};

struct T {
    // An enumerator of the class is at class scope, so it beats a global and
    // loses to a local - the middle rung, which no order that merely swapped
    // the ends would get right.
    enum { E = 9 };
    int classEnum() const { return E; }
    int localBeatsClassEnum() const { int E = 11; return E; }
};

S::S() : k(3), shadowed(4) {}

int main() {
    int A = 5;
    S s;
    T t;
    printf("%d %d\n", A, fromParameter(7));
    printf("%d %d %d %d\n", s.direct(), s.viaThisCapture(),
                            s.viaValueCapture(), s.localWins());
    printf("%d %d %d\n", t.classEnum(), t.localBeatsClassEnum(), E);
    printf("%d %d\n", k, shadowed);
    return 0;
}
