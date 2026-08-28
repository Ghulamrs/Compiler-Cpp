// A class starts private, so a member written with no label before it is out
// of reach from outside. There is no inside yet - member functions are the
// next step - so this is the whole of what access control can currently mean,
// and it is the right whole: a class with private data and no member
// functions is closed.
class Account { int balance; };

int main(void) {
    Account a;
    a.balance = 1;
    return 0;
}
