// `<map>` and `<set>` - ordered, and kept as a sorted array.
//
// **A map here is a sorted `vector<pair<K, V> >` and its iterator is a pointer
// into it**, so `it->first`, `it->second` and `++it` are the built-in
// operations on a pointer to a pair. A real map is a balanced tree with stable
// nodes; the two costs of not being one are written in the header, and the one
// worth repeating is that **every insertion invalidates every iterator**, where
// a real map invalidates none.
//
// The keys are written `std::string("ada")` and not `"ada"` throughout, which
// is not how anyone writes this. A literal where a `const std::string &` is
// wanted needs a user-defined conversion in overload resolution - the
// converting constructor called to make the argument - and this compiler does
// that in an initialisation but not in a call. It is the next thing the library
// needs and the reason this case reads the way it does.
//
// What is checked: that `operator[]` inserts a default value and then replaces
// rather than adds, that `find` and `count` answer without inserting, that the
// iteration order is sorted, that `m[k]++` works on a fresh key, and that a set
// keeps one of a repeated element.
//
// Names are not compared against clang's - it reads its own headers. See
// include-map-set.nonames.

#include <map>
#include <set>
#include <string>
extern "C" int printf(const char *, ...);
int main(void) {
    std::map<std::string, int> ages;
    ages[std::string(std::string("ada"))] = 36;
    ages[std::string(std::string("alan"))] = 41;
    ages[std::string(std::string("grace"))] = 85;
    ages[std::string(std::string("ada"))] = 37;                                 // replaces, not adds
    printf("%d %d %d\n", (int)ages.size(), ages[std::string(std::string("ada"))], ages[std::string(std::string("grace"))]);

    printf("%d %d\n", (int)ages.count(std::string("alan")), (int)ages.count(std::string("nobody")));

    std::map<std::string, int>::iterator it = ages.find(std::string("alan"));
    printf("%s %d %d\n", it->first.c_str(), it->second,
           ages.find(std::string("nobody")) == ages.end());

    int total = 0;
    for (std::map<std::string, int>::iterator i = ages.begin(); i != ages.end(); ++i)
        total += i->second;
    printf("%d\n", total);

    // sorted order, which is what a map promises
    for (std::map<std::string, int>::iterator i = ages.begin(); i != ages.end(); ++i)
        printf("%s ", i->first.c_str());
    printf("\n");

    std::map<int, int> counts;
    counts[7]++;
    counts[7]++;
    counts[3] = 5;
    printf("%d %d %d\n", counts[7], counts[3], (int)counts.size());

    std::set<std::string> seen;
    seen.insert(std::string("b"));
    seen.insert(std::string("a"));
    seen.insert(std::string("b"));
    printf("%d %d %d\n", (int)seen.size(), (int)seen.count(std::string("a")),
           (int)seen.count(std::string("z")));
    for (std::set<std::string>::iterator i = seen.begin(); i != seen.end(); ++i)
        printf("%s ", i->c_str());
    printf("\n");
    return 0;
}
