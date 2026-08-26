// There is no later assignment that would bind this: once a reference exists
// it refers to something, and assigning to it writes through it instead.
int main(void) {
    int &n;
    return n;
}
