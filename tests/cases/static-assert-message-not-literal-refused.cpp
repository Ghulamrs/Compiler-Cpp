// A static_assert whose message is a variable, refused.
//
// The message is printed by the compiler, so there is no program running to
// read a variable - even a `const char *` initialised right beside it. It has
// to be written out where the assertion is.
const char *kWhy = "the width is eight";

int main(void) {
    static_assert(sizeof(int) == 8, kWhy);
    return 0;
}
