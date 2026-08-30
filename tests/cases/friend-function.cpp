// [class.friend]: a friend is not a member.
//
// The declaration is written inside the class and the function it declares
// belongs to the enclosing namespace - it has no `this`, it is not in the
// class's function table, it is not mangled into the class, and all the class
// gives it is access. `peek` below is the same `peek` `main` calls.
//
// Three things this case holds that are easy to get wrong:
//
//   * the access specifier a friend declaration sits under is ignored -
//     [class.friend]/9 - so `peek` under `public:` and `doubled` under
//     `private:` are granted exactly the same thing;
//   * a friend reaches a private member *function*, not only private data;
//   * and it is granted to that one function, never to its name. `snoop` and
//     the two-argument `peek` are in friend-not-granted and friend-overload,
//     which are the other half of this case.
extern "C" { int printf(const char *, ...); }

class Account {
public:
    Account(int n);
    friend int peek(const Account &a);
private:
    int balance;
    int twice() const;
    friend int doubled(const Account &a);
};

Account::Account(int n) { balance = n; }
int Account::twice() const { return balance * 2; }

int peek(const Account &a) { return a.balance; }
int doubled(const Account &a) { return a.twice(); }

int main(void) {
    Account a(42);
    printf("%d %d\n", peek(a), doubled(a));
    return 0;
}
