// A condition has its own grammar - [stmt.select]/1 - and it takes `= expr` or
// braces, not parentheses. clang refuses this by name too: "variable
// declaration in condition cannot have a parenthesized initializer". Refused
// rather than accepted quietly, because `if (int a(5))` reads like a call to
// whoever comes next and is neither that nor allowed.
extern "C" int printf(const char *, ...);

int main() {
    if (int a(5)) printf("%d\n", a);
    return 0;
}
