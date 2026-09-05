// **A static member function has no `this`, and the definition said it had
// one.** [class.static]/1: `static` on a member means there is no implicit
// object parameter. cxx1 decided that question from the *qualifier* alone - a
// definition written `A::f` is a member, therefore it has a `this` - which is
// true of every member except the static ones.
//
// It matters only where the two orderings differ, and that is the Microsoft
// ABI: cl passes `this` first and the hidden return pointer second, so a
// function believed to have a `this` looks for that pointer in the second slot
// while every caller puts it in the first. The callee then writes its result
// through whatever the first argument happens to be. On Itanium the hidden
// pointer is first either way and nothing shows.
//
// Compiler++ has one such function - `SemanticAnalyzer::parameterText`, a
// static member returning std::string - and calling it overwrote a
// `Function`'s return type with a string's length, so the next dynamic_cast on
// that field read address 0x11 and the program died. 127 of its 200 test
// comparisons were failing on this one line.
//
// The guard object is what makes the fault visible: the corruption lands on
// whatever the first argument pointed at, so the case passes a pointer to an
// object it can check afterwards.
extern "C" int printf(const char *, ...);

struct Box {
    int guard;
    int n;
};

struct Text {
    int len;
    char ch;
};

struct Maker {
    // No `this`. The hidden return pointer is the first argument.
    static Text describe(Box *b, int width);
    // For contrast: a member, where `this` comes first and the pointer second.
    Text member(Box *b, int width);
    int tag;
};

Text Maker::describe(Box *b, int width) {
    Text t;
    t.len = b->n + width;
    t.ch = 's';
    return t;
}

Text Maker::member(Box *b, int width) {
    Text t;
    t.len = b->n + width + tag;
    t.ch = 'm';
    return t;
}

int main() {
    Box b;
    b.guard = 0x5A5A;
    b.n = 7;

    Maker m;
    m.tag = 100;

    Text one = m.member(&b, 1);
    printf("member %d %c guard %X\n", one.len, one.ch, b.guard);

    Text two = Maker::describe(&b, 2);
    printf("static %d %c guard %X\n", two.len, two.ch, b.guard);

    // Called again through the same object, to show the guard survives both.
    Text three = Maker::describe(&b, 3);
    printf("again  %d %c guard %X n %d\n", three.len, three.ch, b.guard, b.n);
    return 0;
}
