// [dcl.init.list]/7: a braced initialiser does not narrow. This gave c the
// value 44 - 300 with its top bits cut off - without a word; clang refuses it
// under -std=c++11 -pedantic-errors: "constant expression evaluates to 300
// which cannot be narrowed to type 'char'".
int main() { char c = {300}; return c; }
