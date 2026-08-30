// Friendship is granted to a *function*, not to a name.
//
// This is why the class records the linkage name of what it befriended rather
// than the source name: `peek(const Account &)` was offered access and
// `peek(const Account &, int)` was not, and an overload declared afterwards
// must not inherit a grant the class never saw. clang refuses this program in
// the same place.
class Account {
    friend int peek(const Account &a);
    int balance;
public:
    Account(int n);
};

Account::Account(int n) { balance = n; }
int peek(const Account &a) { return a.balance; }
int peek(const Account &a, int k) { return a.balance + k; }

int main(void) { Account a(1); return peek(a) + peek(a, 2); }
