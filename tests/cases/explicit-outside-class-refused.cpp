// `explicit` repeated on the out-of-class definition, refused.
//
// The keyword belongs to the declaration inside the class. Writing it again on
// the definition is an error rather than a harmless restatement, which is
// worth pinning because `virtual` follows the same rule and for the same
// reason - both say something about how the name may be *used*, which is
// settled where the class is declared.
struct Later {
    int v;
    explicit Later(int a);
};

explicit Later::Later(int a) { v = a; }

int main(void) {
    Later l(3);
    return l.v;
}
