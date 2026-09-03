// `virtual static`, refused by name.
//
// [class.static]/1 again, from the other side: `static` says the call needs no
// object and `virtual` says the object chooses which function is called. A
// declaration cannot say both, and the two words are refused together rather
// than one of them being taken as the winner.
struct S {
    virtual static int f(int a) { return a; }
};

int main(void) {
    return S::f(1);
}
