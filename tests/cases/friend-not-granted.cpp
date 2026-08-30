// The other half of friend-function: a function that was not befriended is
// refused exactly as it was before friendship existed. Without this case the
// feature could be "grant everybody access" and the suite would not notice.
class Account {
    friend int peek(const Account &a);
    int balance;
public:
    Account(int n);
};

Account::Account(int n) { balance = n; }
int peek(const Account &a) { return a.balance; }
int snoop(const Account &a) { return a.balance; }

int main(void) { Account a(1); return peek(a) + snoop(a); }
