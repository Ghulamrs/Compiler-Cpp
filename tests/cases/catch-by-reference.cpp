// **A handler may catch by reference** - [except.handle]/3 makes `cv T &`
// match exactly what `T` matches, so the type_info names the referent and the
// reference is a slot holding the pointer __cxa_begin_catch already returned.
// Measured against clang: it stores %rax and reads through it, where a
// by-value handler dereferences and copies.
//
// **No handler returns**, because `return` inside a `catch` is refused on
// x86_64-windows - a handler is a funclet there and leaving one early is a
// return of the address to carry on at. Each writes a variable instead.
extern "C" int printf(const char *, ...);

int main() {
    int a = -1;
    try { throw 7; } catch (const int &e) { a = e; }

    // A non-const reference names the exception object itself, so writing
    // through it writes what the runtime is holding.
    int b = -1;
    try { throw 13; } catch (int &e) { e = e * 2; b = e; }

    // The referent's own type is what the runtime matches on, so a handler
    // further down the list is reached exactly as a by-value one would be.
    int c = -1;
    try { throw 'x'; }
    catch (const double &d) { c = 1; }
    catch (const char &ch) { c = ch; }

    double d = -1.0;
    try { throw 2.5; } catch (const double &v) { d = v * 4.0; }

    // Unnamed, so nothing is declared and only the match matters.
    int e = -1;
    try { throw 5; } catch (const int &) { e = 99; }

    // A by-value handler beside them, unchanged by any of this.
    int f = -1;
    try { throw 21; } catch (int v) { v = v + 1; f = v; }

    printf("%d %d %d %g %d %d\n", a, b, c, d, e, f);
    return 0;
}
