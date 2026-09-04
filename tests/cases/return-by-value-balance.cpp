// **A class returned by value was never destroyed**, in every shape a call's
// result can appear in: discarded, read for one member, used to initialise a
// variable, and passed straight into another call. One object per call leaked
// for the rest of the function.
//
// Two halves, and each alone left the other visible. The **caller** never
// registered the slot the callee builds its result into, so nothing destroyed
// it - `completeCall` allocates that slot and now puts it on the pending list
// like any other temporary, with `claimCallResult` taking the entry back off
// wherever something redirects the result into storage of its own. The
// **callee** released the temporary in `return Owner(n);` - correctly, since
// its bytes travel out through the hidden pointer and the caller owns them -
// and then copied it into a return slot as well, which built a second object
// and left the first undestroyed. A released temporary *is* the object going
// out, so the copy does not run for it.
//
// **This counts live objects and whether constructions balance destructions,
// and never the number of constructor calls.** Copy elision is permitted
// rather than required in C++11 and the oracles take different options - clang
// elides at -O0, cl does not - so a count of constructors has no single right
// answer, while a leak or a double destroy moves these two numbers and nothing
// else does.
extern "C" int printf(const char *, ...);

int live = 0, ctors = 0, dtors = 0;

// Written out of line, because on x86_64-linux clang emits only the C2 form of
// an *inline* constructor where cxx1 emits C1 and C2 - a recorded divergence
// with nothing to do with lifetimes, and one this case need not carry.
struct Owner {
    int v;
    Owner(int n);
    Owner(const Owner &o);
    ~Owner();
};
Owner::Owner(int n) : v(n) { live++; ctors++; }
Owner::Owner(const Owner &o) : v(o.v) { live++; ctors++; }
Owner::~Owner() { live--; dtors++; }

// A class whose copy is trivial and whose destructor is not: the copy costs
// nothing to make and the *destruction* is what makes it observable, which is
// why the elision has to ask about more than the copy.
struct Plain {
    int v;
    Plain(int n);
    ~Plain();
};
Plain::Plain(int n) : v(n) { live++; ctors++; }
Plain::~Plain() { live--; dtors++; }

Owner make(int n) { return Owner(n); }
Plain plain(int n) { return Plain(n); }
int take(Owner o) { return o.v; }

void report(const char *what) {
    printf("%-12s live %d  balanced %d\n", what, live, ctors == dtors);
}

int main() {
    { make(6); }                             report("discarded");
    { int x = make(6).v; (void)x; }          report("one member");
    { Owner b = make(6); (void)b.v; }        report("init");
    { Owner b(make(6)); (void)b.v; }         report("direct");
    { take(make(6)); }                       report("argument");
    { int y = take(make(6)); (void)y; }      report("arg value");
    { Owner c(7); Owner d = c; (void)d.v; }  report("from lvalue");
    { Owner a = Owner(5); (void)a.v; }       report("from temp");
    { Owner e(8); Owner f = make(e.v);
      (void)f.v; }                           report("nested");
    { Owner g = make(1); g = make(2);
      (void)g.v; }                           report("assigned");
    { plain(4); }                            report("trivial cp");
    { Plain p = plain(4); (void)p.v; }       report("trivial in");
    return 0;
}
