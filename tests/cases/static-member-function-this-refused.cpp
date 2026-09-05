// `this` inside a static member function, refused by name.
//
// The class is in scope inside one - which is what makes this worth a case.
// The parser knows which class the body belongs to, so a `this` written here
// finds a class and would have taken the offset the *last* member function
// used, reading whatever that left behind. A wrong answer no suite would see,
// so it is refused where it is written.
struct S {
    int v;
    static int f(void) { return this->v; }
};

int main(void) {
    return S::f();
}
