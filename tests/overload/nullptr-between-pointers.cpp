// Two pointer parameters, and `nullptr` converts to both at the same rank.
//
// Refused, and the refusal is the point: a null pointer constant says nothing
// about *which* pointer, so there is no best candidate. Making one of the two
// win - void * is a common guess - would be inventing a rule.
extern "C" { int printf(const char *, ...); }

int f(void *a) { return 1 + 0 * (a != 0); }
int f(char *a) { return 2 + 0 * (a != 0); }

int main(void) {
    printf("%d\n", f(nullptr));
    return 0;
}
