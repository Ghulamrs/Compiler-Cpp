// `f(bool)` is not viable for `nullptr`, and that is not the same as being a
// worse match.
//
// [conv.bool] gives std::nullptr_t a conversion to bool for *direct*-
// initialization only, and an argument is copy-initialized - so bool is out of
// the candidate set entirely. Ranking it beside the pointer conversion would
// make this call ambiguous, which clang does not report. The pointer wins
// outright.
extern "C" { int printf(const char *, ...); }

int f(bool a)  { return 1 + 0 * a; }
int f(char *a) { return 2 + 0 * (a != 0); }

int main(void) {
    printf("%d\n", f(nullptr));
    return 0;
}
