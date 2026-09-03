// `static ... const` on a member function, refused by name.
//
// [class.static]/1: a static member function has no `this`, and `const` on a
// member function qualifies exactly that. There is nothing here for it to say.
// Worth refusing rather than dropping: a `const` quietly ignored would change
// the mangled name on one ABI and nothing else, which is the kind of silence
// that shows up as a link error in another translation unit.
struct S {
    static int f(int a) const { return a; }
};

int main(void) {
    return S::f(1);
}
