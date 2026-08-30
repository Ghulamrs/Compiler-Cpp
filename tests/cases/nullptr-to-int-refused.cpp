// `int n = nullptr;`, refused - and this refusal is the feature.
//
// The whole reason C++11 added a keyword for a value that is already spelled 0
// is that 0 is an int, and an int converts to things a null pointer has no
// business converting to. `nullptr` converts to pointers and to nothing else,
// so a program that means "a pointer that points nowhere" can say so without
// also saying "the number zero".
int main(void) {
    int n = nullptr;
    return n;
}
