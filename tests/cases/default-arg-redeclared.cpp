// [dcl.fct.default]/4: a later declaration in the same scope may add a
// default where the earlier one gave none. cxx1's redeclaration path cleared
// the `noexcept` it had just checked and left the defaults sitting in the
// parser, so `g`'s were dropped here - and then re-read as the *next*
// function's, which made `h()` a legal call that evaluated g's token stream.
// It returned 50.
extern "C" int printf(const char *, ...);

int g(int a);
int g(int a = 5) { return a; }
int h(int b) { return b * 10; }

int main() {
    printf("%d %d\n", g(), h(3));   // g's default is honoured; h has none
    return 0;
}
