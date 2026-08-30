// `int S::*p = &S::x;` - a pointer to a data member.
//
// **It is not a pointer to anything.** What it holds is an *offset* into an
// object of the class, which is why no backend had to be told this kind
// exists: `s.*p` is `*(T *)((char *)&s + p)`, an add and two casts they all
// already knew. The cast to `char *` is the one thing that can be got wrong
// and give a wrong answer rather than an error - without it the add would be
// scaled by the member's size.
//
// **`&S::x` is read where `&` is**, because nothing else would: the qualified
// name path answers for a *static* member, which is an ordinary object with an
// ordinary address, and a non-static member has no address of its own to take.
//
// Its size is the one thing that is not the same everywhere - Itanium keeps a
// ptrdiff_t and Microsoft an int, so 8 and 4. Measured, and the reason this
// case does not print sizeof.
extern "C" { int printf(const char *, ...); }

struct S {
    int a;
    int b;
    double d;
};

int readIt(S s, int S::*p) { return s.*p; }

int main(void) {
    S s;
    S *ps;
    s.a = 1;
    s.b = 2;
    s.d = 2.5;
    ps = &s;

    int S::*p = &S::a;
    double S::*q = &S::d;

    int throughObject = s.*p;
    int throughPointer = ps->*p;
    p = &S::b;                    // the same variable, a different member
    int reassigned = s.*p;
    s.*p = 9;                     // and written through
    int written = s.b;
    int passed = readIt(s, &S::a);
    int viaDouble = (int)(s.*q * 2);

    printf("%d %d %d %d %d %d\n", throughObject, throughPointer, reassigned,
           written, passed, viaDouble);
    return 0;
}
