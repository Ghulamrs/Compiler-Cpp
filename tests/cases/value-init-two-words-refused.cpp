// `unsigned long()` is ill-formed: [expr.type.conv] takes one
// simple-type-specifier, and clang stops at the second word - "expected '('
// for function-style cast". Refused by name, with the two spellings that
// are legal.
int main() {
    unsigned long n = unsigned long();
    return (int)n;
}
