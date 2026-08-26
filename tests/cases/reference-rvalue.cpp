// A reference that can write has nothing to bind to here: 5 has no address,
// and a temporary made to hold it would be written to and then thrown away.
int main(void) {
    int &n = 5;
    n = 6;
    return n;
}
