// The other side of const-uninitialised.cpp: the shapes [dcl.init]/7 allows,
// each measured against clang before it was written down. What makes a const
// object initialisable is that nothing in it would be left unset - a
// constructor somebody wrote, or every member carrying its own initialiser, all
// the way down through the bases.
extern "C" int printf(const char *, ...);

struct Written    { int a; Written() : a(1) {} };      // a constructor of its own
struct EveryMember{ int a = 2; int b = 3; };           // every member initialised
struct OfWritten  { Written w; };                      // a member that answers for itself
struct Derived : Written { };                          // and a base that does
struct Plain      { int a; int b; };                   // neither - but initialised here

int main() {
    const Written w;
    const EveryMember e;
    const OfWritten o;
    const Derived d;
    const Plain p = Plain();          // what the refusal tells you to write
    const Plain q = {7, 8};           // and the older spelling, still fine
    const int n = 9;
    printf("%d %d %d %d %d %d %d %d %d\n",
           w.a, e.a, e.b, o.w.a, d.a, p.a, q.a, q.b, n);
    return 0;
}
