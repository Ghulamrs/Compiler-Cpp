// A `try` inside a `catch` handler - which was **miscompiled**, then refused,
// and now works. The file has been all three, and the order matters.
//
// A `try` inside another one's *body* has been refused by name for a long time:
// the call-site table holds ranges and a nested one has to split its parent. A
// handler's body is the same table and the same clash, and it was not refused,
// because `inTryBody_` is restored before the handlers are parsed.
//
// **What that cost was a silent wrong answer.** The parser numbers a `try`'s
// handlers when it reads them - the outer `int` before its body - while the
// backend registered rows in the order the regions closed, innermost first. So
// the parser wrote `sel == 1` for the outer `int` and the table said index 1
// was the inner `double`. Measured at e731456: clang printed
// `outer int 7 inner double 2.5` and cxx1 called terminate on an uncaught int.
//
// **The first fix was the wrong one, and it is worth writing down.** The blame
// went to that numbering, and the numbering was made single-sourced - the
// parser's indices now ride on the `Try` node. The miscompile survived it
// unchanged. What actually broke it was the *overlap*: the enclosing region's
// row and this one covered the same addresses, and the gap row written in front
// of each real one, `[at, begin)`, went negative and was assembled as a uleb128
// of about 5.4e8, swallowing the rest of the function.
//
// Regions are split now - `Walker::openRegion` closes the enclosing one at the
// inner's first label and reopens it at the inner's last - so no two rows
// overlap, and the rows can be sorted into address order, which is what the gap
// arithmetic needs. The sort that was reverted for silently breaking nested
// unwinding was not wrong about address order; it was premature, because
// overlapping rows need the innermost first and a linear scan cannot be given
// both.
//
// The enclosing region reopens at the inner's *end*, not past its landing pad,
// so a throw from inside this handler still belongs to the `try` outside it.

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
