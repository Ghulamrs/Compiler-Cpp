// Floating to integer is narrowing whatever the value: [dcl.init.list]/7
// gives it no "unless the source is a constant" clause, so `{1.0}` is refused
// though 1.0 converts to 1 exactly. clang: "type 'double' cannot be narrowed
// to 'int' in initializer list".
int main() { int i = {1.0}; return i; }
