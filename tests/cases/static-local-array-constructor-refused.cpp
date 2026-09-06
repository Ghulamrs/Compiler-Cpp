// A static local array of a class with a constructor: the same refusal as the
// file-scope array, in the block-scope spelling.
struct S { int v; S() : v(3) {} };
int f() { static S arr[2]; return arr[1].v; }
int main() { return f() - 3; }
