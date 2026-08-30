// Rung 7.6: lambdas, and the last thing on the ladder.
//
// A closure is a class with a call operator, generated where the lambda is
// written - so this needed both of the things that came before it: a class
// that can be defined inside a function, and `operator()`.
//
// **The object lives in the enclosing frame.** A lambda expression is a class
// temporary and this compiler has none, so the closure gets a frame slot and
// the expression answers with its name. Its lifetime is the function rather
// than the full expression, which is longer than the standard asks for and
// harmless while a closure has nothing to destroy.
//
// **The body is replayed as a member function**, through the same held-body
// path a class written inside a function already used - so the definition
// machinery needed to learn nothing about lambdas. The tokens are synthesised
// to look like one, and the return type is spelled as a hidden typedef,
// because `int` is one token and `const char *` is three and a class is its
// tag.
extern "C" { int printf(const char *, ...); }

int outer(int k) {
    auto add = [](int a, int b) { return a + b; };
    auto none = []() { return 7; };
    auto expl = [](int a) -> double { return a; };
    auto shout = [](int a) { printf("[%d]", a); };   // deduces void
    shout(3);
    return add(1, 2) + none() + (int)expl(10) + k;
}

int main(void) {
    auto twice = [](int a) { return a * 2; };
    // A body that is more than one `return`, and a lambda inside a lambda -
    // the inner one's `return` must not be taken for the outer one's.
    auto nested = []() { auto i = []() { return 1; }; return i() + 10; };
    auto steps = [](int a) { int t = a * 2; return t + 1; };
    int total = 0;
    for (int i = 0; i < 3; i++) total += twice(i);
    // immediately invoked, which needs no name at all
    printf("%d %d %d %d %d\n",
           twice(21), outer(5), nested(), steps(5), [](int a) { return a + 1; }(5));
    printf("%d\n", total);
    return 0;
}
