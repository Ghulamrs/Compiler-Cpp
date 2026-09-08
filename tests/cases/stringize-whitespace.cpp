// [cpp.stringize]/2: each run of white space *between* an argument's
// preprocessing tokens becomes one space in the string literal, and leading and
// trailing white space goes altogether. cxx1 deleted the ends and kept the
// middle, so `S(  a   b  )` came back "a   b" where the standard and every
// other compiler give "a b".
//
// White space inside a string or character literal is part of that token and
// survives untouched, which is the case that stops a blind collapse being right.
extern "C" int printf(const char *, ...);

#define S(x) #x
#define J(a, b) #a #b

int main() {
    printf("[%s]\n", S(  a   b  ));
    printf("[%s]\n", S("a   b"));
    printf("[%s]\n", S(x + 	 y));
    printf("[%s]\n", S(f(1,   2)));
    printf("[%s]\n", S('  '));
    printf("[%s]\n", S("q\"r"));
    printf("[%s]\n", J( p   q , r ));
    printf("[%s]\n", S());
    return 0;
}
