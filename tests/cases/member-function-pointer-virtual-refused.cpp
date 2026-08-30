// A pointer to a *virtual* member function, refused by name. Itanium keeps the
// vtable index in the low bit of the first word and branches on it at every
// call; Microsoft calls a thunk the compiler emits. Neither is written, and a
// pointer that quietly called the wrong override would be worse than a refusal.
struct S {
    virtual int v();
};

int S::v() { return 1; }

int main(void) {
    int (S::*f)() = &S::v;
    S s;
    return (s.*f)();
}
