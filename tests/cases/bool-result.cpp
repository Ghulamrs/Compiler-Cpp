// **[expr.rel]/1, [expr.eq]/1, [expr.log.and]/1, [expr.unary.op]/9: the
// result is `bool`.** cxx1 answered `int`, which is C's rule, and it showed
// up three ways: `sizeof` was 4 where it is 1, overloading on `bool` against
// `int` chose the wrong function, and `auto b = (x == y)` produced something
// that would hold 3.
//
// A bool promotes to int wherever a number is wanted, which is why nothing
// that used the old answer arithmetically had to change - the last line here
// is the proof of that half.
extern "C" int printf(const char *, ...);

int which(bool a) { return 1; }
int which(int a)  { return 2; }

// `sizeof` never evaluates its operand, so a global is enough to keep the
// operands from being constants - which clang warns about in `1 && 1` and
// which has nothing to do with the type of the result.
int probe = 1;

static_assert(sizeof(probe == 1) == 1, "a comparison is bool");
static_assert(sizeof(probe < 2)  == 1, "and so is a relation");
static_assert(sizeof(!probe)     == 1, "and the negation of anything");
static_assert(sizeof(probe && probe) == 1, "and both logical operators");
static_assert(sizeof(probe || probe) == 1, "");

int main() {
    int *p = 0;
    int n = 1;
    auto b = (1 == 2);
    b = true;                      // a bool, so this is what it can hold

    printf("%d %d %d %d %d\n",
           which(p != 0),           // 1: bool, not int
           which(n),                // 2: still int
           (int)b,                  // 1
           (1 < 2) + (3 < 4),       // 2: promotes where a number is wanted
           (int)(!!n));             // 1
    return 0;
}
