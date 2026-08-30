// `S b = a;` where the *copy* constructor is explicit, refused.
//
// Same rule as the converting case beside it, reached through the copy
// constructor instead: copy-initialization may not pick an explicit
// constructor, and `b = a` is copy-initialization however ordinary it looks.
// `Guarded b(a);` compiles - see explicit.cpp.
struct Guarded {
    int v;
    Guarded(int a) { v = a; }
    explicit Guarded(const Guarded &o) { v = o.v; }
};

int main(void) {
    Guarded a(1);
    Guarded b = a;
    return b.v;
}
