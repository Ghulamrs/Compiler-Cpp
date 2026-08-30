// A pointer to a member *function*, which is a different animal from a pointer
// to a data member and is refused by name. What it holds is not an offset but a
// function to call and how to find its `this` - on the Microsoft ABI a
// structure of up to four words, on Itanium a pair. A data member pointer is
// one integer, which is why that one needed nothing of any backend.
struct S {
    int f();
};

int S::f() { return 1; }

int main(void) {
    int (S::*p)() = &S::f;
    S s;
    return (s.*p)();
}
