// **An alias declaration is not a using-declaration** - it names a type where
// the other names an entity - so the three scopes that refuse the second must
// not answer for it. `typedef int Int;` says the same thing here.
using Int = int;
int main() { Int x = 0; return x; }
