// The other side of "inside": a private member function is not callable from
// outside the class, exactly as a private data member is not readable.
class Safe {
public:
    int open();
private:
    int shut();
};
int Safe::open() { return shut(); }
int Safe::shut() { return 1; }

int main(void) {
    Safe s;
    return s.shut();
}
