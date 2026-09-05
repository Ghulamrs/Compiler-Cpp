// Two user-defined conversions in a row, refused.
//
// [over.ics.user]/1: an implicit conversion sequence holds at most one
// user-defined conversion. A `Near` is made from an `int` and a `Far` from a
// `Near`, and neither of those makes a `Far` from an `int` - so this call has
// no viable candidate, and saying so is the answer rather than chaining them.
//
// In this compiler the rule is also what stops the search recursing: asking
// whether a constructor's own parameter could be converted would ask the same
// question again. clang refuses the same program.
struct Near { int v; Near(int x) : v(x) {} };
struct Far  { int v; Far(const Near &n) : v(n.v) {} };

static int take(const Far &f) { return f.v; }

int main(void) {
    return take(7);
}
