// The other side of [expr.cond]: where the arms are not both lvalues of one
// type the result is a value, and a value has no address for a non-const
// reference to bind to. clang refuses this too, in its own words.
int main() {
    int a = 1;
    int &r = true ? a : 2;
    return r;
}
