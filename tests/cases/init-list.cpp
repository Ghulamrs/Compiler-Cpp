// Mem-initializer lists - and the rule in them that surprises people.
//
// [class.base.init]/11: members initialise in DECLARATION order, whatever
// order the list writes them in. Account's list names fees first, and fees
// still runs second - after balance - which is why it reads 100 and not
// whatever was in the frame. An emitter that followed the list would compile a
// program the standard says means something else.
//
// The other half is a base with arguments: `: Base(v * 2)` is what chooses the
// base constructor, and it is also what lifted the old refusal of bases with
// no default constructor.
extern "C" { int printf(const char *, ...); }
class Account {
public:
    Account(int opening);
    int balance;
    int fees;
};
// The list out of declaration order on purpose: fees is declared second and
// initialised first in the list, and [class.base.init]/11 runs declaration
// order anyway - balance from the parameter, then fees from balance.
Account::Account(int opening) : fees(balance / 10), balance(opening) {}

class Base {
public:
    Base(int v);
    int b;
};
Base::Base(int v) : b(v) { printf("Base(%d)\n", v); }

class Derived : public Base {
public:
    Derived(int v);
    int d;
};
Derived::Derived(int v) : Base(v * 2), d(v) { printf("Derived(%d)\n", v); }

int main(void) {
    Account a(100);
    Derived x(5);
    printf("%d %d\n", a.balance, a.fees);
    printf("%d %d\n", x.b, x.d);
    return 0;
}
