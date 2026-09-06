// An inline namespace's members are visible in the namespace enclosing it,
// which is a lookup rule rather than a spelling.
namespace N { inline namespace M { int f() { return 0; } } }
int main() { return N::f(); }
