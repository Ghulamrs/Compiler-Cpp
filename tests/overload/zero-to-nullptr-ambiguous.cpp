// The conversion runs the other way too, and it costs the same.
//
// The literal 0 is a null pointer constant, so it reaches std::nullptr_t as
// well as char * - both pointer conversions, so neither wins. Refused, which
// is the mirror of nullptr-between-pointers and is why the rank had to be the
// same in both directions rather than nullptr_t being made a special case.
extern "C" { int printf(const char *, ...); }

int f(decltype(nullptr) a) { return 1 + 0 * (a != 0); }
int f(char *a)             { return 2 + 0 * (a != 0); }

int main(void) {
    printf("%d\n", f(0));
    return 0;
}
