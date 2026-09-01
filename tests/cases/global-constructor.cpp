// A class with a constructor at file scope. The local path refuses a `static`
// one by name; this path had no test at all, so the object was laid out as
// bytes and the constructor never ran - `s.i` read 0 where the constructor
// had written 7, and it compiled, linked and ran.
struct S { int i; S() { i = 7; } };
S s;
int main() { return s.i - 7; }
