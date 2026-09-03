// A template inside a namespace, named with its namespace.
//
// **`std::vector<int> v;` did not compile, and `using namespace std;` did.**
// The template table is keyed by the bare name, so a template declared in a
// namespace was registered as `vector` - which the unqualified spelling found
// and the qualified one did not. Three places had to learn it: whether a
// qualified name *is* a type (a template is not one until its arguments are
// given, so the `<` is part of the question), how to read one where a type is
// expected, and how to call a function template written `std::copy(...)`.
//
// It went unseen because every template in the corpus was at global scope. A
// library puts all of them in one namespace, so the first header written was
// the first program to ask.

extern "C" int printf(const char *, ...);

namespace lib {

template <class T>
class box {
public:
    box() : v_() {}
    box(const T &v) : v_(v) {}
    T get(void) const { return v_; }
    void set(const T &v) { v_ = v; }
private:
    T v_;
};

template <class A, class B>
struct couple {
    A a;
    B b;
};

template <class T>
static T doubled(const T &v) { return v + v; }

}  // namespace lib

namespace deep {
namespace inside {
template <class T> struct held { T v; };
}
}

int main(void) {
    lib::box<int> a(7);                          // qualified, with an argument
    lib::box<char> b;
    b.set('q');
    printf("%d %c\n", a.get(), b.get());

    lib::couple<int, char> c;                    // two parameters
    c.a = 3;
    c.b = 'x';
    printf("%d %c\n", c.a, c.b);

    // Not here: a `lib::box<lib::box<int> >`. Nesting a template as its own
    // argument is covered by template-nested.cpp, and doing it *through* a
    // constructor taking `const T &` needs the inner instantiation's implicit
    // copy constructor, which an instantiated class template does not get -
    // a separate gap, and not this case's subject.

    printf("%d %d\n", lib::doubled(21), (int)lib::doubled('A'));

    deep::inside::held<int> d;                   // two namespaces deep
    d.v = 11;
    printf("%d\n", d.v);

    using namespace lib;                         // the spelling that always worked
    box<int> e(5);
    printf("%d\n", e.get());
    return 0;
}
