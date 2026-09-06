// An anonymous union's members are members of the class around it and share
// its storage - [class.union]. Nothing here flattens one member list into
// another. C++98 rather than a C++11 gap, which is why the wider sweep found
// it and the C++11 one did not.
struct S { union { int a; float b; }; };
int main() { S s; s.a = 0; return s.a; }
