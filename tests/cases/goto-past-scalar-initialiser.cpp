// The neighbouring shape: no constructor and no destructor, just `int x = 5`.
// [stmt.dcl]/3 forbids the jump all the same, because it is the initialiser
// that is bypassed - and without the rule this returned whatever the slot
// held, which is a wrong answer with nothing to point at. clang refuses it
// with the same message as the class case.
int f(int n) {
    if (n) goto done;
    int x = 5;
    n = x;
done:
    return n + x;
}
int main() { return f(1); }
