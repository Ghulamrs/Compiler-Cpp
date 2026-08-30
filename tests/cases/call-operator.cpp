// `operator()`, the one this rung was really after: a closure is a class with
// a call operator, so 7.6 could not begin until an object could be called.
//
// **[over.call] makes it a non-static member function and gives it no
// non-member form**, which is what makes it the simplest of the operators to
// dispatch: the whole candidate set is the class's own, so the ordinary member
// resolution is the resolution, arity and all. It is also the only overloadable
// operator with no fixed number of operands.
//
// The other half of making `f(1)` work is not in the dispatch at all: a name
// followed by '(' was being read as a call to a *function* of that name before
// anything looked at what the name held, so an object was reported undeclared.
// A class-typed name now falls through to the postfix parser the same way a
// function-pointer variable always has.
extern "C" { int printf(const char *, ...); }

class Adder {
public:
    int base;
    Adder(int n);
    int operator()() const;
    int operator()(int a) const;
    int operator()(int a, int b) const;
    int operator()(double a) const;
};

Adder::Adder(int n) { base = n; }
int Adder::operator()() const              { return 1 + 0 * base; }
int Adder::operator()(int a) const         { return 2 + 0 * (base + a); }
int Adder::operator()(int a, int b) const  { return 3 + 0 * (base + a + b); }
int Adder::operator()(double a) const      { return 4 + 0 * (base + (int)a); }

int main(void) {
    Adder add(10);
    char c;
    c = 'k';
    // the overload set is chosen by the arguments, exactly as a function's is
    printf("%d %d %d %d\n", add(), add(1), add(1, 2), add(1.5));
    // char promotes to int rather than converting to double
    printf("%d\n", add(c));
    return 0;
}
