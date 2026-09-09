// [class.ctor]/5: a class with a `const` member and no initialiser for it has
// its *implicit* default constructor deleted - there is nothing such a
// constructor could write, and the member would begin life holding the frame
// with a const promising it would not change. cxx1 built the constructor and
// did exactly that. A class with a written constructor is a different rule and
// is not refused here; nor is one with a base, whose flattened member list
// cannot say which class a member came from.
extern "C" int printf(const char *, ...);
struct S { const int a; };
int main() { S s; printf("%d\n", s.a); return 0; }
