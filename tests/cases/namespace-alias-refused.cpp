// A namespace alias, refused by name.
//
// `namespace A = N;` makes A another way to write N, which is a second table
// on top of the one lookup already walks: every place that asks whether a
// name is a namespace, and every place that builds a qualified key, would
// have to resolve the alias first. Nothing is lost meanwhile - the namespace
// answers to its own name, and `using namespace N;` shortens what a long one
// costs to write.
namespace N {
    int f(void) { return 3; }
}

namespace A = N;

int main(void) {
    return A::f();
}
