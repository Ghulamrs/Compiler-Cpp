// **A name declared as an object is not a template name**, which is
// [basic.lookup.unqual]'s nearest-declaration rule and was not being applied.
//
// `findTemplate` keys every template by its bare name on purpose, so that
// `std::vector` finds `vector` - the note there says two namespaces cannot
// each have a template of one name anyway, and that widening what can be
// *named* does not widen what can be *declared*. What it also did was let an
// **unqualified** `count` find `std::count` from anywhere, with no
// using-directive in sight: including <algorithm> broke any program with a
// local called `count`, `find`, `swap`, `min`, `sort`, `fill` or `copy`. One
// of Compiler++'s sixteen sources stops on `count = pop().i;`.
//
// **Only unqualified lookup gets the test**, and the first attempt at this got
// that wrong: the namespace branch consumes `std::` and then asks about the
// bare name, so testing there killed `std::count(...)` beside a local `count` -
// which is exactly what a program writes. A qualified name has said which
// namespace it means and nothing local can be intended.
#include <algorithm>
#include <vector>

extern "C" int printf(const char *, ...);

// A global with an <algorithm> name, which shadows it just as a local does.
int find = 100;

int main() {
    // Three locals named after function templates, one of them assigned to
    // after its declaration - which is where the old refusal fired.
    int count = 0, swap = 2, sort = 3;
    count = 5;

    std::vector<int> v;
    v.push_back(7);
    v.push_back(9);

    // And the templates are still reachable where nothing shadows them.
    int n = (int)std::count(v.begin(), v.end(), 9);
    std::vector<int>::iterator it = std::find(v.begin(), v.end(), 7);

    printf("%d %d %d %d %d %d\n",
           count, swap, sort, find, n, (int)(it - v.begin()));
    return 0;
}
