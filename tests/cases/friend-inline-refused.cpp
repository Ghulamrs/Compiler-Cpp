// A friend function defined inside the class body. The declaration belongs to
// the enclosing namespace and so would the definition, which means the held-
// body replay that member functions use would have to put this one back
// outside the class it was written in. Refused by name until it does.
class Box {
    friend int peek(const Box &b) { return b.x; }
    int x;
};

int main(void) { return 0; }
