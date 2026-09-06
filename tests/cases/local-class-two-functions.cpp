// A class defined in a function body belongs to that function - [class.local]
// - so two functions may each define `struct L` and they are two types. Both
// ABIs wrap the enclosing function's whole name round the members' names for
// exactly that reason, and CLAUDE.md records the Itanium and Microsoft forms.
//
// That wrapping was applied to a *named* member and to nothing else. A
// constructor and a destructor were spelled as though the class were at file
// scope, so the two `L`s below both produced _ZN1LC1Ev and ??0L@@QEAA@XZ -
// one symbol for two different types, with different constructors. It is not
// a silent wrong answer: the assembler refuses the second definition, and a
// program C++11 accepts did not compile at all.
//
// It went unnoticed because a local class needs a constructor or a destructor
// of its own to reach it, and because it took a second local class of the same
// name in the same translation unit to collide. Lambdas made it common: every
// closure is a local class, and giving one the implicit special members every
// class gets is what surfaced this.
extern "C" int printf(const char *, ...);

static int one() {
    struct L { int v; L() : v(1) {} ~L() {} };
    L a;
    return a.v;
}

static int two() {
    struct L { int v; L() : v(2) {} ~L() {} };
    L b;
    return b.v;
}

// And through a lambda, whose closure is a local class the compiler names.
struct Held { int v; Held(int n); Held(const Held &o); ~Held(); };
Held::Held(int n) : v(n) {}
Held::Held(const Held &o) : v(o.v) {}
Held::~Held() {}

static int first()  { Held h(3); auto f = [h]() { return h.v; };      return f(); }
static int second() { Held h(4); auto g = [h]() { return h.v * 10; }; return g(); }

int main() {
    printf("%d %d %d %d\n", one(), two(), first(), second());
    return 0;
}
