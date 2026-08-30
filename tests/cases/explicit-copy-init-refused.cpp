// `S s = 3;` where the constructor is explicit, refused.
//
// This is the plain case and the reason the keyword exists. `S s(3);` on the
// line below would compile: the two call the same constructor with the same
// argument, and differ only in whether it may be chosen without being named.
//
// The check is made *after* overload resolution rather than by hiding the
// constructor from the candidate set, so the reader is told which constructor
// was found and why it could not be used - rather than that nothing matched,
// which sends them looking for a constructor that is there.
struct Seconds {
    int v;
    explicit Seconds(int a) { v = a; }
};

int main(void) {
    Seconds s = 30;
    return s.v;
}
