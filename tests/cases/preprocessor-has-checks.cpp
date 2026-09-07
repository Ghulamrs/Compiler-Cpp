// The `__has_*` predicates, and a comment on a directive line.
//
// **`__has_include` is answered truthfully and the rest answer 0**, which is
// not a dodge: `#if __has_builtin(X)` is written by a library precisely so it
// can be told no. Saying nothing left the whole family unrecognised, so the
// condition reached the expression parser as a bare identifier followed by a
// parenthesis and stopped there.
//
// The case asks about a builtin **neither** compiler has, so both answer 0 and
// the `.expected` is clang's, as every case here takes it. Asking about a real
// builtin would have clang answer 1 and cxx1 answer 0 - both right - and the
// file would then be checking cxx1 against itself.
//
// **`__is_identifier` is the one that answers 1.** It asks whether a token is
// an ordinary identifier rather than a keyword or a builtin, and here
// everything is. Answering 0 says "that name is special", and libstdc++'s
// `_GLIBCXX_HAS_BUILTIN(B)` - which is `__has_builtin(B) || ! __is_identifier(B)`
// - then reads `0 || !0` and concludes the builtin exists.
//
// **They answer to `#ifdef` as well as to `defined()`**, because libstdc++
// guards its whole builtin layer with `#ifdef __has_builtin`, a directive. That
// half was missed first time and left the macro undefined two thousand lines
// before it was used.
//
// **And a comment on a directive line is whitespace** - [lex.phases]/3 replaces
// it in phase 3, directives execute in phase 4. `#define`'s body already went
// through `stripComments` and a condition did not, so `#if EXPR // why` stopped
// at the '/'. Every real header writes them; it is what libstdc++'s
// `<exception>` line 111 stopped this compiler on.

extern "C" int printf(const char *, ...);

#if __has_include(<vector>)
#define HAVE_VECTOR 1        // a header this compiler ships
#else
#define HAVE_VECTOR 0
#endif

#if __has_include(<no_such_header_at_all>)
#define HAVE_JUNK 1
#else
#define HAVE_JUNK 0          /* and one written the other way */
#endif

#ifdef __has_builtin
#define SEES_IFDEF 1
#else
#define SEES_IFDEF 0
#endif

#if defined(__has_builtin)
#define SEES_DEFINED 1
#else
#define SEES_DEFINED 0
#endif

// **A builtin neither compiler has**, so both must answer 0 and the case stays
// differential. Asking about a real one - __builtin_is_constant_evaluated - is
// what a library does, and there clang answers 1 and cxx1 answers 0, both
// correctly; a case cannot compare those without recording one compiler's
// answer as the other's expectation.
#if __has_builtin(__builtin_no_such_thing_exists_anywhere)
#define HAVE_BUILTIN 1
#else
#define HAVE_BUILTIN 0
#endif

#if __is_identifier(ordinary_name)
#define IS_IDENT 1
#else
#define IS_IDENT 0
#endif

// The libstdc++ shape itself, spelled out.
#ifdef __has_builtin
# ifdef __is_identifier
#  define HAS_BUILTIN(B) __has_builtin(B) || ! __is_identifier(B)
# else
#  define HAS_BUILTIN(B) __has_builtin(B)
# endif
#endif
#if HAS_BUILTIN(__builtin_no_such_thing_exists_anywhere)
#define GLIBCXX_SHAPE 1
#else
#define GLIBCXX_SHAPE 0
#endif

#define N 41 // the answer, less one
#if N + 1 == 42 /* a block comment in a condition */
#define ARITHMETIC_OK 1
#else
#define ARITHMETIC_OK 0
#endif

int main(void) {
    printf("vector=%d junk=%d\n", HAVE_VECTOR, HAVE_JUNK);
    printf("ifdef=%d defined=%d\n", SEES_IFDEF, SEES_DEFINED);
    printf("builtin=%d ident=%d\n", HAVE_BUILTIN, IS_IDENT);
    printf("glibcxx_shape=%d arithmetic=%d n=%d\n", GLIBCXX_SHAPE, ARITHMETIC_OK, N);
    return 0;
}
