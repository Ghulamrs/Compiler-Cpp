// A class with member functions and no data members.
//
// This is the ordinary shape of a class that carries behaviour and no state,
// and it did not work: the empty-class rule - size 1, so that two objects
// have different addresses - returned from the middle of the class body
// before the held member bodies were replayed, before the implicit special
// members were declared and before a vtable would have been emitted. Calling
// a member of such a class compiled and linked to nothing.
//
// It had shipped since member functions arrived, because no case had a class
// with behaviour and no state. Found from the other end: a class template
// with two type parameters would not link, and an empty one is what it
// happened to be.
extern "C" { int printf(const char *, ...); }

struct Behaviour {
    int answer() { return 42; }
    int twice() { return answer() * 2; }
};

struct Base {
    virtual int which() { return 1; }
    virtual ~Base() { }
};
struct Derived : Base {
    virtual int which() { return 2; }
};

int main() {
    Behaviour b;
    printf("%d %d %d\n", b.answer(), b.twice(), (int)sizeof(Behaviour));

    Behaviour copy = b;         // the implicit special members exist
    printf("%d\n", copy.answer());

    Derived d;
    Base *p = &d;
    printf("%d\n", p->which());
    return 0;
}
