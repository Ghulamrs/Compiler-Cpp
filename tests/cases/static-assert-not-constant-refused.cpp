// A static_assert whose condition is not a constant, refused.
//
// The assertion is answered while compiling, so what it asks about has to be
// known then. A call to a function that is not constexpr is the ordinary way
// to get this wrong, and the message says which half is the problem rather
// than reporting the call.
int width(void);

int main(void) {
    static_assert(width() == 8, "the width is eight");
    return 0;
}
