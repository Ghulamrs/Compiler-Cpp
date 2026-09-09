// [class.mem]/1: a member may not be declared twice in one class. cxx1 lays
// out both and reads whichever findMember reaches.
extern "C" int printf(const char *, ...);
struct S { int a; int a; };
int main() { S s; s.a = 1; printf("%d\n", s.a); return 0; }
