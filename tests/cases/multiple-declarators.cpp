// **`std::string a, b;`** - two declarators in one declaration of a class with
// constructors, which is as ordinary as C++ gets and was refused with
// `expected ';'` pointing at the second name.
//
// The cause is one line, three times over. The declarator loop is a `do`/
// `while (consume(","))`, and the branches that build a class ended with
//
//     if (!consume(",")) break;
//     continue;
//
// `continue` in a `do`/`while` jumps to the **condition**, which consumes a
// comma of its own - so the comma was taken twice, the loop ended, and the
// second declarator was left for `expect(";")` to trip over. The comma belongs
// to the condition; the branches just `continue`.
//
// CLAUDE.md recorded this as being about constructor *arguments* -
// `P a(1), b(2);` - because that is the shape it was found in. It was never
// about the arguments: `P a, b;` failed the same way, and so did every class
// with a constructor.
#include <vector>
#include <string>

extern "C" int printf(const char *, ...);

int live = 0;

struct P { int n; P(); P(int v); P(const P &o); ~P(); };
P::P() : n(1) { live++; }
P::P(int v) : n(v) { live++; }
P::P(const P &o) : n(o.n) { live++; }
P::~P() { live--; }

int main() {
    { P a, b; printf("plain %d %d\n", a.n, b.n); }
    // The shape the old message blamed, and a third declarator with none.
    { P a(4), b(5), c; printf("args %d %d %d\n", a.n, b.n, c.n); }
    { std::string s, t; s = "xy";
      printf("strings %d %d\n", (int)s.size(), (int)t.size()); }
    { std::vector<int> u, v; u.push_back(3);
      printf("vectors %d %d\n", (int)u.size(), (int)v.size()); }
    // Copy-initialisation of each, which takes a different branch again.
    { P a = 7, b = 8; printf("copyinit %d %d\n", a.n, b.n); }
    // Every one of them destroyed, which is what says the loop did not lose a
    // declarator somewhere quieter than a parse error.
    printf("live %d\n", live);
    return 0;
}
