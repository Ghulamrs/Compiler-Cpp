// A source that is not a constant is narrowing whenever the target cannot
// hold every value of its type - here `char` from `int` - even though this
// particular int holds 65. The value is not the question; the type is. clang:
// "non-constant-expression cannot be narrowed from type 'int' to 'char'".
int main() { int i = 65; char c = {i}; return c; }
