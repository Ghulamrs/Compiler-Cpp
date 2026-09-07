// A `try` inside a `catch` handler, refused - and it was **miscompiled** until
// 2026-09-07 rather than refused, which is why this case exists.
//
// A `try` inside another one's *body* has been refused by name for a long
// time: the call-site table holds ranges that do not overlap and a nested one
// has to split its parent. A handler's body is the same table and the same
// clash, and it was not refused, because `inTryBody_` is restored before the
// handlers are parsed.
//
// **What that cost was a silent wrong answer**, the worst kind this compiler
// can give. The parser numbers a `try`'s handlers when it reads them - the
// outer `int` before its body is parsed - while the backend registers rows in
// the order the regions close, innermost first. So the parser writes
// `sel == 1` for the outer `int` and the table says index 1 is the inner
// `double`. Measured at e731456: clang prints `outer int 7 inner double 2.5`
// and cxx1 called terminate on an uncaught `int`.
//
// Found by an agent checking whether the parser's numbering and the backend's
// could ever disagree, while the type table was being deduplicated - not by
// any of the four suites, none of which had this shape.

extern "C" int printf(const char *, ...);

void boom(int k) { if (k == 1) throw 7; throw 2.5; }

int main() {
    try { boom(1); }
    catch (int e) {
        printf("outer int %d ", e);
        try { boom(2); } catch (double d) { printf("inner double %.1f ", d); }
    }
    catch (char c) { printf("outer char %c ", c); }
    printf("| end\n");
    return 0;
}
