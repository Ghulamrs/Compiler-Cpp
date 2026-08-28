// A class written inside another one.
//
// It takes no room in the enclosing object - `struct Inner { ... };` inside a
// class body declares a type and no member - and what it gains is a name:
// `Outer::Inner`, which is the tag every table in this compiler is keyed by,
// so a nested class cannot collide with a global of the same name.
//
// Both ABIs spell the whole scope. Itanium writes a nested-name and **each
// enclosing class is a substitution candidate of its own**, which is what
// makes a parameter of type Outer::Inner read `NS_5InnerE` inside a member of
// Outer rather than spelling Outer twice - measured, clang writes
// _ZN5Outer3useENS_5InnerE. Microsoft lists the scopes innermost first and
// back-references them the same way: `?use@Outer@@QEAAHUInner@1@@Z`, where the
// 1 is Outer, measured with cl.
//
// Unqualified, `Inner` means `Outer::Inner` only from inside Outer - the class
// body itself and the body of any member function of it. From outside it has
// to be written out.
extern "C" { int printf(const char *, ...); }

class Outer {
public:
    class Inner {
    public:
        Inner();
        int get();
        static int shared;
        int a;
    };

    // The nested type is usable unqualified from here on, inside the class.
    Inner one;
    int b;

    int use(Inner x);
    Inner make();

    // Two levels down, to show the scope is a list and not a pair.
    class Deep {
    public:
        class Deeper {
        public:
            int d;
            int twice();
        };
        Deeper down;
    };
};

Outer::Inner::Inner()  { a = 4; }
int Outer::Inner::get() { return a; }
int Outer::Inner::shared = 100;

int Outer::use(Inner x) { return x.a + b; }
Outer::Inner Outer::make() { Inner made; made.a = 9; return made; }

int Outer::Deep::Deeper::twice() { return d * 2; }

int main() {
    // The enclosing object holds one Inner, and nothing for the nested class
    // itself.
    Outer o;
    o.b = 1000;
    printf("%d %d\n", o.one.a, o.one.get());

    // Named from outside, it has to be written out in full.
    Outer::Inner x;
    x.a = 7;
    printf("%d\n", x.get());

    // Passed by value: the parameter type is what the substitutions above are
    // about.
    printf("%d\n", o.use(x));

    // Returned by value.
    Outer::Inner made = o.make();
    printf("%d\n", made.a);

    // A static member of a nested class, named with all three components.
    printf("%d\n", Outer::Inner::shared);
    Outer::Inner::shared = 101;
    printf("%d %d\n", Outer::Inner::shared, x.shared);

    // Two levels.
    Outer::Deep deep;
    deep.down.d = 21;
    printf("%d\n", deep.down.twice());
    return 0;
}
