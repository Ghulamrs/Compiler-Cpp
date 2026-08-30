// A lambda inside a lambda, taking the capture of the one around it.
//
// **The name is not a local by then.** The outer lambda's capture is a member
// of the outer *closure*, so `findLocal` answers nothing and the capture
// machinery has to reach through the outer `this` instead. One helper does
// that, and it is asked in three places that must agree: where a named capture
// is looked up, where the `[=]`/`[&]` scan decides what to take, and where the
// closure object is built and the copy actually made.
//
// **The tag needed a counter, and the reason is not obvious.** A closure is
// named after the function that writes it - but inside a replay that function
// is `operator()`, so every level of nesting built `operator()::$_0` and the
// third was told its own call operator was declared twice. The *owners* differ,
// each closure's operator() having its own linkage name, so the owner decides
// and a counter separates the display tags - the same disambiguation a local
// class already uses.
extern "C" { int printf(const char *, ...); }

int main(void) {
    int k = 2;
    int a = 1;
    int b = 2;

    auto viaDefault = [=]() {
        auto inner = [=]() { return k; };
        return inner() * 10;
    };

    auto named = [k]() {
        auto inner = [k]() { return k; };
        return inner() + 1;
    };

    auto byRefOnCopy = [k]() {
        auto inner = [&k]() { return k; };     // a reference to the outer copy
        return inner() * 2;
    };

    auto threeDeep = [=]() {
        auto second = [=]() {
            auto third = [=]() { return a; };
            return third() + 10;
        };
        return second() + 100;
    };

    auto twoNames = [=]() {
        auto inner = [=]() { return a * 10 + b; };
        return inner();
    };

    printf("%d %d %d %d %d\n",
           viaDefault(), named(), byRefOnCopy(), threeDeep(), twoNames());
    return 0;
}
