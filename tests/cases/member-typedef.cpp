// A typedef inside a class, which is what SFINAE turned out to need first.
//
// It is keyed "S::value" - the same qualified key a nested class already
// uses - so it is found from inside the class through classStack_, from a
// member's body through currentClass_, and from outside as `S::value` through
// the walk that reads `Outer::Inner`. Reaching one through a specialization,
// `Holder<int>::value`, is the same walk with a class that was made rather
// than named in front of it.
//
// `typename` is read and dropped. It exists to tell a C++ parser that a
// dependent qualified name is a type, which matters only where a template
// body is parsed before its arguments are known - and this compiler replays a
// body at instantiation, where the name is looked up like any other. Accepted
// rather than refused, so that a file written for clang compiles here too.
extern "C" { int printf(const char *, ...); }

struct S {
    typedef int value;
    typedef value *pointer;
    value v;
    value read() { return v; }
};

template <class T> struct Holder {
    typedef T value;
    value item;
    value take() { return item; }
};

int main() {
    S s;
    s.v = 7;
    S::value outside = 3;
    S::pointer p = &s.v;
    printf("%d %d %d\n", s.read(), outside, *p);

    Holder<int> h;
    h.item = 4;
    Holder<int>::value fromTemplate = 5;
    typename Holder<int>::value spelled = 6;
    printf("%d %d %d\n", h.take(), fromTemplate, spelled);
    return 0;
}
