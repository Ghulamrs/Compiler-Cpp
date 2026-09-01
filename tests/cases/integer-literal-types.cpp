// **[lex.icon] table 6 holds two ladders, not one.** A decimal literal with
// no suffix climbs int, long, long long and never reaches an unsigned type; a
// hexadecimal or octal one has an unsigned rung above each signed one. cxx1
// ran the decimal ladder for every base, so 0x80000000 came out `long` where
// it is `unsigned int` - which changes `sizeof`, and changes the signedness
// of any comparison written against a mask.
//
// The sizes below agree on all three targets: `long` is 8 bytes on the two
// Itanium ones and 4 on Windows, so a decimal literal that overflows `int`
// lands on `long` there and `long long` here, and both are 8.
extern "C" int printf(const char *, ...);

static_assert(sizeof(0x80000000) == 4, "hex: unsigned int is the next rung");
static_assert(sizeof(0xFFFFFFFF) == 4, "still unsigned int");
static_assert(sizeof(2147483648) == 8, "decimal: no unsigned rung, so long");
static_assert(sizeof(0x7FFFFFFF) == 4, "int, on either ladder");
static_assert(sizeof(0x100000000) == 8, "past unsigned int");

int main() {
    printf("%d %d %d\n",
           (int)(0x80000000 > -1),    // 0: unsigned, so -1 converts up
           (int)(0xFFFFFFFF == -1),   // 1: same conversion, and they meet
           (int)(2147483648 > -1));   // 1: signed all the way
    return 0;
}
