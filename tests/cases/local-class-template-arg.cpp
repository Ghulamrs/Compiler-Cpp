// A class written inside a function, handed to a function template as its
// argument. The type's own name has to carry the function it was written in:
// Itanium as `Z <function> E <name>` and the Microsoft ABI as `?1?` round the
// whole of it. Spelling the tag whole instead put a `::` in the symbol -
// `_Z1fI7main::LEiT_` - which no assembler takes, so this never linked. The
// emit suite could not see it either: it stops at assembly, and the compiler
// was exiting 0.
extern "C" int printf(const char *, ...);

template <class T>
int size_of(T t) { return static_cast<int>(sizeof t); }

int main() {
    struct L { int a; int b; };
    L l;
    l.a = 1;
    l.b = 2;
    printf("%d %d\n", size_of(l), l.a + l.b);
    return 0;
}
