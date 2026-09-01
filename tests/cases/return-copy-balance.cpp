// **Every object built is destroyed exactly once**, which is the invariant a
// double free breaks and the one thing about copies that no elision can
// change. Counting *calls* is what CLAUDE.md rules out - clang elides at -O0
// where cl does not, so there is no one right number - but the totals must
// agree with each other whichever choice a compiler makes.
//
// Both shapes here return a glvalue this function does not own, where
// [class.copy]/31 forbids the elision: a by-value parameter, which it
// excludes by name because the caller destroys the argument, and `*p`, which
// is not an automatic object at all. Before this case, the return path copied
// the bytes and called no constructor, then skipped the source's destructor
// to make the tally look right - so `pass` built two objects and destroyed
// three, and any class owning memory freed it twice.
extern "C" int printf(const char *, ...);

int made = 0;
int gone = 0;

struct T {
    int v;
    T(int n) { v = n; made = made + 1; }
    T(const T &o) { v = o.v; made = made + 1; }
    ~T() { gone = gone + 1; }
};

T pass(T t) { return t; }
T deref(T *p) { return *p; }

int main() {
    {
        T a(7);
        { T r = pass(a);    printf("%d ", r.v); }
        { T s = deref(&a);  printf("%d ", s.v); }
    }
    printf("%s\n", made == gone ? "balanced" : "unbalanced");
    return 0;
}
