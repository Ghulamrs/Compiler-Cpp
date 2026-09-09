// [class.mem]/1: one member of each name. cxx1 laid both out and read whichever
// `findMember` reached - the *last*, that walk going backwards so a derived
// member hides a base's - so the first was a hole in the object nothing could
// name. A member that hides a *base's* is ordinary C++ and is not this rule;
// refusals-that-must-not-fire.cpp holds that shape.
extern "C" int printf(const char *, ...);
struct S { int a; int a; };
int main() { S s; s.a = 1; printf("%d\n", s.a); return 0; }
