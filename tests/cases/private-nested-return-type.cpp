// **[class.access]/6: a member's own definition may name its class's private
// types**, and the return type is written before the `C::` that says whose
// member it is. `VM::Value VM::pop()` reads the type first, when nothing yet
// says this is a member of VM - so the access check refused a definition the
// class had every right to write. One of Compiler++'s sixteen sources stops
// there, and it is the ordinary way to return a private nested type.
//
// The declarator ahead is asked instead: the scan stops at the first `(` or
// `;` at depth zero, which bounds it to this declaration, and takes the
// longest run of `A::B::` that names a type - so `Outer::Inner::f` asks about
// `Outer::Inner` rather than stopping at `Outer`. A class nested inside the
// owner counts, because its members reach the owner's private names exactly as
// the owner's own do.
//
// What it must not do is let anyone else in, which is what
// private-nested-return-type-refused.cpp is for.
extern "C" int printf(const char *, ...);

struct VM {
private:
    // A union, as Compiler++ writes it - the access rule is the same for any
    // nested type.
    union Value { int n; double d; };
    Value held;
public:
    Value pop();
    void push(int v);
};

VM::Value VM::pop() { Value v; v.n = held.n; return v; }
void VM::push(int v) { held.n = v; }

struct Outer {
private:
    struct Hidden { int n; };
public:
    // A nested class's member reaches the enclosing class's private names.
    struct Inner { Hidden make(); };
    Hidden own();
};

Outer::Hidden Outer::Inner::make() { Hidden h; h.n = 5; return h; }
Outer::Hidden Outer::own() { Hidden h; h.n = 6; return h; }

int main() {
    VM m;
    m.push(7);
    Outer::Inner i;
    Outer o;
    printf("%d %d %d\n", m.pop().n, i.make().n, o.own().n);
    return 0;
}
