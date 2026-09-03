// The comparison [over.ics.rank]/3.2.6 *does* answer: two reference parameters
// to the same type, one more const-qualified than the other. The less qualified
// one wins for an argument that can bind it, and the const one takes everything
// else - a const object, and a value with no address at all.
//
// This is the neighbour of ambiguous-value-vs-reference.cpp: the qualification a
// reference binding is charged exists for these three lines, which is why it was
// not simply deleted when a by-value parameter was let past it.
extern "C" { int printf(const char *, ...); }

int hold(int &a)         { return 1 + 0 * a; }
int hold(const int &a)   { return 2 + 0 * a; }

int main(void) {
    int v;
    const int c = 5;
    v = 3;
    printf("%d %d %d\n", hold(v), hold(c), hold(7));
    return 0;
}
