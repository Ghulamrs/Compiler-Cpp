// A range-based `for` over a class with no `begin` and `end`, refused.
//
// [stmt.ranged]/1 looks the two up as **members** first, and only if neither is
// found does it look for free `begin(r)` and `end(r)` by argument-dependent
// lookup. cxx1 does the member half and refuses the fallback by name, so this
// class - which has neither - stops here rather than at a message about the
// free functions it does not look for.
//
// clang refuses it too, for the same shape and a longer message.

struct Plain { int x; };

int main(void) {
    Plain p;
    p.x = 1;
    for (int n : p) return n;
    return 0;
}
