// **[stmt.return]/2 copy-initialises the returned object**, and a converting
// constructor is part of that: `return "v";` from a function returning
// `std::string` is `string("v")`. The machinery was already here -
// `userConversion`, which a by-value argument has used since conversion
// functions landed - and what was missing was this second door to it.
//
// Two of Compiler++'s sixteen sources stop on exactly this line, and the
// message they got named the return type and the literal without saying that a
// constructor stood between them.
extern "C" int printf(const char *, ...);

struct Text {
    int n;
    Text(const char *s);
    Text(const Text &o);
    ~Text();
};
Text::Text(const char *s) { n = 0; while (s[n] != 0) n++; }
Text::Text(const Text &o) : n(o.n) {}
Text::~Text() {}

// The literal reaches the return type through the converting constructor.
Text code(int t) { if (t == 0) return "v"; return "int"; }
// And so does a `?:` whose arms are both literals - the conversion happens at
// the return, the conditional itself never seeing a class.
Text pick(bool b) { return b ? "yes" : "no"; }
// An explicit temporary still works, which is the form that always did.
Text written() { return Text("abcd"); }

int main() {
    printf("%d %d %d %d\n", code(0).n, code(1).n, pick(true).n, written().n);
    return 0;
}
