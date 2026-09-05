// [stmt.select]/2 gives a condition-declaration an initialiser, and there is
// nothing to test without one. clang refuses this too.
int main() {
    if (int k) return 1;
    return 0;
}
