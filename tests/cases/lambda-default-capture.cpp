// `[=]` - everything the body reads, copied.
//
// **Finding what the body reads is a scan of its tokens**, which an earlier
// refusal said this parser does not make. It makes one now, and it is a scan
// and not a parse: an identifier that names a local of the enclosing function
// is captured unless the token before it says it is a member name - `p.k`,
// `p->k` and `N::k` name no local.
//
// **Over-capturing is harmless and under-capturing is not**, which settles
// every doubtful case. A name the body declares itself shadows the member,
// because a local is looked up before a member, and a parameter does the same
// - so a copy nobody reads is the worst this can do. `unused` below is
// captured and never read, and `shadowed` proves the shadowing.
extern "C" { int printf(const char *, ...); }

struct P {
    int k;                       // the same name as a local, reached through p
};

int main(void) {
    int k = 5;
    double d = 1.5;
    int unused = 99;
    int r = 3;
    int &alias = r;              // a reference captured by value copies r
    P p;
    p.k = 40;

    auto sum = [=](int a) { return a + k + (int)d; };
    auto viaMember = [=]() { return p.k; };
    auto viaAlias = [=]() { return alias; };
    auto shadowed = [=]() { int k = 1; return k; };
    auto shadowedParam = [=](int d) { return (int)d; };

    k = 100;                     // after the copies were taken
    r = 900;
    printf("%d %d %d %d %d\n",
           sum(1), viaMember(), viaAlias(), shadowed(), shadowedParam(7));
    return 0;
}
