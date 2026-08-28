// Placement new needs a constructor to be worth having, and that is rung 3.
// A parenthesised type-id after 'new' reads the same way to the parser, so
// both are refused with one message that says how to write the other.
int main(void) {
    int *p = new (int);
    return p == 0;
}
