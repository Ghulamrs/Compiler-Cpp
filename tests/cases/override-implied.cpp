// An override is virtual whether or not the keyword is written again.
//
// [class.virtual]: a function that overrides one is itself virtual, and the
// keyword on the derived declaration is optional. cxx1 used to require it,
// and a class that left it out was dispatched *statically* - the program
// compiled, ran, and quietly gave the base's answer. A wrong answer with no
// diagnostic is the outcome this compiler refuses loudest, so the rule is
// tested here rather than assumed.
//
// The destructor is the same rule and the more dangerous half: a base with a
// virtual destructor makes the derived one virtual too, and without that
// `delete` through a Base * runs ~Base alone and the derived class's own
// cleanup never happens at all.
extern "C" { int printf(const char *, ...); }
struct Base {
    virtual int value();
    virtual int twice();
    virtual ~Base();
};
struct Derived : Base {
    int value();                  // no keyword, and still an override
    ~Derived();                   // nor here
};
int Base::value() { return 1; }
int Base::twice() { return value() * 2; }
Base::~Base() { printf("~Base\n"); }
int Derived::value() { return 21; }
Derived::~Derived() { printf("~Derived\n"); }
int main() {
    Derived d;
    Base *p = &d;
    printf("%d\n", p->value());
    // Through the base's own non-virtual member, so the dispatch happens
    // inside code that was compiled knowing nothing about Derived.
    printf("%d\n", p->twice());
    Base *heap = new Derived;
    delete heap;
    return 0;
}
