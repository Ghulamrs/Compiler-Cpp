// A constexpr function that never stops recursing. The standard lets an
// implementation set a limit on how deep a constant expression may go and
// requires it to say so; this is that, said where it was reached rather than
// as a compiler that stops responding.
constexpr int forever(int n) { return forever(n + 1); }
int deep[forever(1)];
int main() { return sizeof deep; }
