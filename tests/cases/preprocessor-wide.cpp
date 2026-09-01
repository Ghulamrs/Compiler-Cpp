// **[cpp.cond] evaluates a condition in the widest signed integer there is.**
// Five levels of this evaluator worked in `long`, which is 64 bits where gcc
// and clang build this compiler and 32 where cl does - so a condition holding
// a value wider than 32 bits answered differently depending on which of the
// three boxes had built the preprocessor reading it. The same source, two
// answers, and no test could see it from one machine.
//
// The arithmetic wraps rather than overflowing, too. This evaluator runs
// inside a compiler, so "undefined behaviour on overflow" would mean the
// compiler itself: negation of the most negative value, the one division that
// overflows, and a shift count outside 0 to 63 are each defined or refused by
// name now.
extern "C" int printf(const char *, ...);

#if 0x300000002 & 0xFFFFFFFF
#define WIDE_AND 1
#else
#define WIDE_AND 0
#endif

#if (0x300000002 / 0x100000000) == 3
#define WIDE_DIV 1
#else
#define WIDE_DIV 0
#endif

#if (1 << 40) > 1000000000
#define WIDE_SHIFT 1
#else
#define WIDE_SHIFT 0
#endif

#if (0x7FFFFFFFFFFFFFFF | 0) == 9223372036854775807
#define WIDE_OR 1
#else
#define WIDE_OR 0
#endif

#if (-9223372036854775807 - 1) < 0
#define LEAST_NEGATIVE 1
#else
#define LEAST_NEGATIVE 0
#endif

int main() {
    printf("%d %d %d %d %d\n", WIDE_AND, WIDE_DIV, WIDE_SHIFT, WIDE_OR,
           LEAST_NEGATIVE);
    return 0;
}
