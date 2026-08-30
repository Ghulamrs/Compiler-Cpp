// A friend declaration declares an ordinary namespace-scope function, and
// this case is about that half of it rather than about access.
//
// `peek` is befriended by a struct, whose members are public anyway, so
// nothing here needs the grant. What it proves is what the grant is *not*:
// `peek` is not a member, so it takes its Account as a written parameter and
// has no `this`; it can be declared again at file scope and that is the same
// function, not a second one; and `main`, which was never befriended, calls
// it exactly as it calls anything else.
//
// This case used to hold the refusal - `friend` was a keyword the lexer knew
// and the parser had no rule for - and it holds the rule that replaced it.
extern "C" { int printf(const char *, ...); }

struct Account {
    int balance;
    friend int peek(const Account &a);
};

// The same function again, out here. A friend declaration is a declaration.
int peek(const Account &a);

int peek(const Account &a) { return a.balance; }

int main(void) {
    Account a;
    a.balance = 7;
    printf("%d\n", peek(a));
    return 0;
}
