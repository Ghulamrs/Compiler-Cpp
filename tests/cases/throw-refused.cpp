// `throw` can name a fundamental type and nothing else yet.
//
// The Itanium ABI hands __cxa_throw a pointer to the type_info object that
// identifies the exception, and for a fundamental type the standard library
// already carries that object - naming it is all a compiler has to do. For a
// class, or a pointer, the compiler has to *emit* one, and cxx1 has no RTTI
// at all: the vtable's typeinfo slot is a plain zero and `typeid` is refused.
// So this is refused by name rather than thrown with a type nobody can catch.
//
// **Both ABIs refuse it and each says why in its own terms** - Itanium that
// the library carries no such type_info, Microsoft that it can name no such
// type descriptor - so what the case records is the half they share.
struct E { int v; };
void f() { E e; e.v = 1; throw e; }
int main() { f(); return 0; }
