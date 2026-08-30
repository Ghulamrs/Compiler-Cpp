// `return s;` where the class's copy constructor is explicit, refused.
//
// **The surprising one**, and worth its own case: the function is ill-formed
// on its own, before any caller is looked at. `return` copy-initializes the
// caller's object, so the copy constructor is selected and checked - and
// [class.copy]/31 says that happens even where the copy is elided, which here
// it is: this compiler builds the object straight into the caller's storage
// and calls no copy constructor at all. The rule is checked all the same.
struct Guarded {
    int v;
    Guarded(int a) { v = a; }
    explicit Guarded(const Guarded &o) { v = o.v; }
};

Guarded make(void) {
    Guarded g(5);
    return g;
}

int main(void) {
    return make().v;
}
