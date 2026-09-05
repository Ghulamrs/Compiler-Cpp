// **`std` is written `St`** - [mangle.substitution] gives it one of the
// predefined abbreviations - so `std::string` is `St6string` and not
// `N3std6stringE`. Every cxx1 object that named a type from its own headers had
// the wrong symbol, which is invisible in a single-file program and invisible
// in an all-cxx1 link, both being self-consistent. It shows the moment a cxx1
// object meets a clang one.
//
// Measured, and two of the four rules are not guessable:
//
//   std::string          St6string        no `N...E` around it
//   std::deep::inner     NSt4deep5innerE  the wrapper is back once it is deeper
//   nn::thing            N2nn5thingE      only `std` gets an abbreviation
//   f(std::string, std::string)  St6stringS_
//
// The last says `St` takes **no numbered slot**: the second parameter is `S_`,
// the first candidate, so the whole of `St6string` is candidate zero and the
// namespace alone is not a candidate at all.
//
// A `std::` *template* specialization is still spelled without it - cxx1 keys
// every template by its bare name, so the specialization does not know which
// namespace it came from. docs/CONFORMANCE.md records that.
namespace std {
    struct string { int n; };
    namespace deep { struct inner { int n; }; }
}
namespace nn { struct thing { int n; }; }

void a(std::string s) { (void)s; }
void b(const std::string &s) { (void)s; }
void d(nn::thing t) { (void)t; }
void e(std::deep::inner i) { (void)i; }
void f(std::string s, std::string t) { (void)s; (void)t; }

int main() {
    std::string s;
    s.n = 1;
    nn::thing t;
    t.n = 2;
    std::deep::inner i;
    i.n = 3;
    a(s); b(s); d(t); e(i); f(s, s);
    return s.n + t.n + i.n;
}
