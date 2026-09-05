// `<string>` - the class, exercised through the header this compiler ships.
//
// **This is the first case in the tree that uses a C++ standard library
// header**, and there is something worth knowing about how it is checked: the
// suites hand every case to clang as well, and clang reads *its* `<string>`,
// not this compiler's. So the two implementations are compared through their
// behaviour on the same program - which is a far better oracle than reading the
// header and believing it, and it is why nothing below reaches for a member
// this compiler's version happens to spell differently. `begin()` here answers
// a `const char *` where a conforming library answers an iterator; that is a
// simplification recorded in the header and deliberately not used here.
//
// Four operators and two compiler fixes had to land before the header could be
// written: `operator[]`, `operator=`, `operator->`, the compound assignments,
// a class temporary built inside a member of its own class, and the address of
// one. The last was found by this case's `substr` and is the one to remember -
// the value leaves a function as *bytes* through the hidden pointer, so
// destroying the temporary before the caller copies out of it hands back a
// shallow copy of an object whose buffer has been freed. It printed the right
// length and an empty string, which is the failure that looks like a formatting
// bug.
//
// Sizes are printed beside the text throughout, because a string with the right
// characters and the wrong length is a fault that hides until something walks
// it.

#include <string>

extern "C" int printf(const char *, ...);

static int sumThrough(const std::string &s) {      // read through a const ref
    int total = 0;
    for (std::string::size_type i = 0; i < s.size(); i++) total += s[i];
    return total;
}

int main(void) {
    std::string a("hello");
    std::string b = a;                             // copy construction
    b += " world";                                 // compound assignment
    b.push_back('!');
    printf("%d %d %s\n", (int)a.size(), (int)b.size(), b.c_str());

    std::string c = b.substr(6, 5);                // the temporary that found it
    std::string d;
    d = b.substr(0, 5);                            // and through assignment
    printf("%s %s %d %d\n", c.c_str(), d.c_str(), (int)c.size(), (int)d.size());

    printf("%d %d %d %d\n", a == "hello", "hello" == a, a != b, a < b);

    std::string e = a + " " + c;                   // concatenation, both ways
    std::string f = "say: " + a;
    printf("%s|%s|%d\n", e.c_str(), f.c_str(), (int)e.size());

    printf("%d %d %d\n", (int)b.find("wor"), (int)b.find('o'),
           b.find("nothing") == std::string::npos ? 1 : 0);

    std::string g("xyz");
    g[1] = 'Y';                                    // subscripting as an lvalue
    printf("%s %c %d %d\n", g.c_str(), g[0], g.empty(), std::string().empty());

    std::string h;
    for (int i = 0; i < 40; i++) h += 'z';         // forces the buffer to grow
    printf("%d %c %d\n", (int)h.size(), h[39], sumThrough(g));

    std::string i("trim me");
    i.clear();
    printf("%d %d\n", (int)i.size(), i.empty());
    return 0;
}
