// The same rule at file scope, where a local's constructor path does not run:
// this laid the object out as bytes and gave C++14's answer - 5 and 6 - until
// the check was put on the file-scope path as well.
struct S { int i = 1; int j = 2; };
S s = {5, 6};
int main() { return s.i - 5; }
