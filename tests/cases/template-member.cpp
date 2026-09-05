// **Member function templates** - a template written inside a class, [temp.mem]
// and [temp.mem.func]. Called with explicit arguments, v.head<3>(): the
// specialization is made with the member's own parameters bound, and - for a
// member of a class template - the class's parameters too, both layers at once.
// The body is replayed synchronously through the inline-member path, which saves
// the enclosing function's state, since a member template is always used when
// called and there is nothing to defer.
//
// Measured against clang on all three targets. Microsoft is exact; on Itanium a
// return type that depends on the member's parameter - V<M> - is encoded by
// clang as the pattern S_IXT_EE where cxx1 writes the substituted S_ILi3EE. That
// needs a non-type template argument spelled as a parameter reference, which is
// its own step; recorded in template-member.nonames for the two Itanium targets.
extern "C" int printf(const char *, ...);

struct S {
    int base;
    S(int b) : base(b) {}
    template <int M> int scaled() const { return base * M; }
    template <int M> int plus(int x) const { return base + M + x; }
};

template <int N> struct V {
    int a[N];
    V() { for (int i = 0; i < N; ++i) a[i] = i + 1; }
    template <int M> int nth() const { return a[M]; }
    template <int M> V<M> head() const {
        V<M> r;
        for (int i = 0; i < M; ++i) r.a[i] = a[i];
        return r;
    }
};

int main() {
    S s(10);
    printf("%d %d %d\n", s.scaled<3>(), s.scaled<5>(), s.plus<7>(100));
    V<4> v;
    printf("%d %d\n", v.nth<0>(), v.nth<3>());
    V<3> h = v.head<3>();
    printf("%d %d %d\n", h.a[0], h.a[1], h.a[2]);
    return 0;
}
