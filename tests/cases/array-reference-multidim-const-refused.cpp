// A `int (&)[2][3]` may not bind to a `const int[2][3]`: the const is on the
// innermost element, and a reference that can write would drop it. clang
// refuses it. The message spells the dimensions outermost first, as written.
int main(void) {
    const int c[2][3] = {{1, 2, 3}, {4, 5, 6}};
    int (&r)[2][3] = c;
    return r[0][0];
}
