// `friend` is a keyword the lexer knows and the parser has no rule for, and a
// class body is where it is written. Without the refusal that names it, the
// declaration reaches specifiers() and is reported as a missing type at the
// keyword - which points at the right token and says nothing about it.
struct Account {
    int balance;
    friend int peek(const Account &a);
};

int peek(const Account &a) { return a.balance; }

int main() {
    Account a;
    a.balance = 7;
    return peek(a);
}
