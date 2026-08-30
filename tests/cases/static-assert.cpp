// static_assert.
//
// **A declaration that declares nothing and emits nothing.** The condition is
// folded in the parser and a zero is a diagnostic; nothing reaches the AST, so
// no backend and no emitter heard about this feature at all. That is why it is
// one function called from three loops rather than three implementations: file
// scope, a block, and a class body are three different places in two different
// files, and the rule is one rule.
//
// The value of it here is that it turns the compiler's own answers into
// something a test can pin. Every assertion below is a fact about this
// compiler's targets that nothing else in the suite states outright.

extern "C" int printf(const char *, ...);

// File scope, and the fundamental type sizes these three targets agree on.
static_assert(sizeof(char) == 1, "a char is one byte by definition");
static_assert(sizeof(int) == 4, "int is 4 on all three targets");
static_assert(sizeof(void *) == 8, "all three targets are 64-bit");
static_assert(sizeof(nullptr) == sizeof(void *),
              "std::nullptr_t is pointer-sized");

// The message may be written in pieces, the way any string literal may.
static_assert(sizeof(short) == 2, "a short is " "two bytes");

enum Size { Two = 2, Four = 4 };
static_assert(Four == 2 * Two, "an enumerator is a constant expression");

const int kWidth = 8;
static_assert(kWidth * 2 == 16, "a const int initialised by a constant is one");

constexpr int square(int a) { return a * a; }
static_assert(square(5) == 25, "and so is a constexpr call");

struct Pair {
    int a;
    int b;
    // Class scope. Asked inside the very class it is about, which is the shape
    // that makes an assertion travel with the thing it constrains.
    static_assert(sizeof(int) * 2 == 8, "two ints and no padding between them");
};

static_assert(sizeof(Pair) == 8, "and the class is what those two ints make");

namespace N {
    static_assert(sizeof(long long) == 8, "a namespace is a scope like any other");
}

template <int N>
struct Buffer {
    // Inside a template, where the condition mentions the parameter.
    static_assert(N > 0, "a buffer with no room is not a buffer");
    int slot[N];
};

int main(void) {
    // Block scope, and it may sit among ordinary statements.
    static_assert(sizeof(double) == 8, "a double is 8");
    Buffer<3> b;
    b.slot[0] = 1;
    static_assert(sizeof(Buffer<3>) == 12, "three ints");

    Pair p;
    p.a = 4;
    p.b = 5;
    // `square` is called and `kWidth`'s address is taken, as well as both
    // being folded above. Without that clang emits neither symbol - a
    // constexpr function nobody odr-uses folds away, and so does a const int
    // every reader of which is a constant expression, even a `printf`
    // argument. The names suite would then report cxx1 emitting symbols clang
    // did not, which is a difference about *emission* and not about names, and
    // does not belong to this case.
    const int *addr = &kWidth;
    printf("%d %d %d %d %d\n", p.a + p.b, (int) sizeof(Pair), b.slot[0],
           square(p.a), *addr);
    return 0;
}
