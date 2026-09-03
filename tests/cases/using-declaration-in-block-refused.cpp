// `using N::f;` inside a block, refused by name.
//
// At namespace scope the declaration is an alias that lasts as long as the
// namespace does. Here it would declare a name for the rest of the block and
// take part in overload resolution against the locals beside it, which is a
// scope this compiler's namespaces - a prefix and a search - do not have.
// `using namespace N;` works in a block, and is the directive, not this.
namespace N {
    int f(void) { return 3; }
}

int g(void) {
    using N::f;
    return f();
}

int main(void) {
    return g();
}
