// The constructor the compiler would write is refused where the reader can fix
// it, rather than at the first use of the class. A base whose only constructor
// takes arguments has no way to be built by a default constructor nobody wrote,
// and the message says which constructor to write instead.
class Base {
public:
    Base(int n);
    int b;
};
Base::Base(int n) { b = n; }

class Derived : public Base {
public:
    int d;
};

int main() {
    Derived x;
    return x.d;
}
