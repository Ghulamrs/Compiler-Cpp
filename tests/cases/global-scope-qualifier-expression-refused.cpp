// `::f()` in an expression, refused by name. The type form works - see
// global-scope-qualifier.cpp - and the two are not the same lookup: a type is
// found by one look in one table, which reaches only the global scope by
// construction, while a name in an expression goes through qualifyForLookup,
// where a namespace and a using-directive get their say. Restricting that for
// one name needs a flag put down again before the call's arguments are parsed,
// and half of it silently finds `cc::f` where the program asked for `::f`.
namespace cc { int f() { return 100; } }
int f() { return 3; }
namespace cc { int use() { return ::f(); } }
int main() { return cc::use(); }
