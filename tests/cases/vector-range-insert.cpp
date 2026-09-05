// **`v.insert(v.end(), o.begin(), o.end())`** - how one vector is appended to
// another without a loop at the call site, and common enough that leaving it
// out shows: one of Compiler++'s sixteen sources stops on exactly this line.
// `erase(first, last)` is its pair and went in with it.
//
// **The position is taken as an offset before anything grows.** `reserve` moves
// the buffer, so a caller's `at` pointer would be left behind - and the same
// reallocation is why [sequence.reqmts] says `first` and `last` may not point
// into the vector being inserted into.
//
// The shifting runs backwards, so a move within one buffer cannot overwrite
// what it has not read yet - the same reason `string::insert` walks that way.
#include <vector>
#include <string>

extern "C" int printf(const char *, ...);

void show(const char *what, const std::vector<int> &v) {
    printf("%s", what);
    for (std::size_t i = 0; i < v.size(); i++) printf(" %d", v[i]);
    printf("\n");
}

int main() {
    std::vector<int> a;
    a.push_back(1); a.push_back(2); a.push_back(9);
    std::vector<int> rest;
    rest.push_back(5); rest.push_back(6); rest.push_back(7);

    a.insert(a.end(), rest.begin(), rest.end());              show("append ", a);
    a.insert(a.begin(), rest.begin(), rest.end());            show("front  ", a);
    a.insert(a.begin() + 2, rest.begin(), rest.begin() + 1);  show("middle ", a);
    // An empty range changes nothing and must not disturb the position.
    std::vector<int> none;
    a.insert(a.begin(), none.begin(), none.end());            show("empty  ", a);
    a.erase(a.begin(), a.begin() + 4);                        show("erase  ", a);

    // A class element, so the copies go through a constructor rather than bytes.
    std::vector<std::string> s;
    s.push_back("x");
    std::vector<std::string> more;
    more.push_back("y"); more.push_back("z");
    s.insert(s.end(), more.begin(), more.end());
    printf("strings %d %s %s %s\n", (int)s.size(),
           s[0].c_str(), s[1].c_str(), s[2].c_str());
    return 0;
}
