// `: r()` on a reference member is ill-formed - a reference is bound, and
// value-initialisation has nothing to bind it to. clang refuses it too.
struct R {
    int &r;
    R() : r() {}
};
int main() { return 0; }
