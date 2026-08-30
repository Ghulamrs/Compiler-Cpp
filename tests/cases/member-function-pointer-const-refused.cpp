// `int (S::*f)() const` - a different type, refused by name. The constness of
// the `this` a member function takes is decided where the member is declared
// and is not part of a function type here, so taking the word would make a
// pointer that could be given a non-const member's address and then called on
// a const object.
struct S {
    int x;
    int get() const;
};

int S::get() const { return x; }

int main(void) {
    int (S::*f)() const = &S::get;
    S s;
    return (s.*f)();
}
