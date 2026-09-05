// `using B::f;` inside a class, refused by name.
//
// The one at namespace scope is an alias and this compiler has it. This is a
// different rule: it *redeclares* a base member in the derived class, which
// changes the member's access and puts it into the derived class's own
// overload set beside anything already there. Neither is a redirection of a
// name, so neither comes free with the alias.
struct B {
    int f(void) { return 1; }
};

struct D : B {
    using B::f;
};

int main(void) {
    D d;
    return d.f();
}
