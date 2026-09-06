// `double[3][2]` is not a `double[2][3]`: every dimension has to agree, and
// a reference to an array converts nothing on the way in. clang refuses it.
static int two(const double (&a)[2][3]) { return (int)a[1][2]; }

int main(void) {
    double t[3][2] = {{1, 2}, {3, 4}, {5, 6}};
    return two(t);
}
