// default-arg-base-init.cpp is the base a constructor did not name; this is
// every other call the compiler builds by hand and used to walk one argument
// per *declared* parameter. [class.ctor]/5: a constructor whose every
// parameter has a default is a default constructor, so `M(int a = next())`
// is what builds the member the implicit constructor does not name, the
// array member beside it, a local array, and `new M` - and `new M(1)` of a
// two-parameter constructor is the partial shape, one door over from the
// crash. The counter is the proof that [dcl.fct.default]/9 holds at each of
// them: a default is evaluated at every call that uses it, so an array of
// three reads `next()` three times and the numbers come out in construction
// order - clang's order, members before the locals that follow them.
//
// Each of these was refused by name before this: "has no constructor taking
// nothing", or for `new`, "takes 2 argument(s), given 1" - a refusal handed
// down after overload resolution had already accepted the call.
extern "C" int printf(const char *, ...);
int n = 0;
int next() { return ++n; }
struct M { int v; M(int a = next()) : v(a) {} };
struct P { int v; P(int a, int b = 20) : v(a + b) {} };
struct T { M m; };
struct U { M two[2]; };
struct Base { int total; Base(int a = 1, int b = 6) : total(a + b) {} };
struct Der : Base { };
int main() {
    T t; U u; M arr[3]; M one; M *p = new M; M *q = new M();
    P *r = new P(1); P *s = new P(1, 2);
    Der d;
    printf("%d %d %d %d %d %d %d %d\n", t.m.v, u.two[0].v, u.two[1].v,
           arr[0].v, arr[2].v, one.v, p->v, q->v);
    printf("%d %d %d\n", r->v, s->v, d.total);
    return 0;
}
