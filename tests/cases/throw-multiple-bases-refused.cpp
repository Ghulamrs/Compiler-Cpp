// A class with more than one base wants `__vmi_class_type_info` - a shape
// carrying each base's offset and flags - where one base wants only
// `__si_class_type_info`. `emitClassTypeInfo` answers empty for it, which is
// what a class with no describable type_info has always meant here, and the
// throw path says so by name rather than throwing something nobody can catch.
//
// **Both ABIs refuse it and each says why in its own terms** - Itanium that
// this class has more than one base, Microsoft that it can name no class
// descriptor at all - so what the `.error` records is the half they share.
// The Windows box is what said so: the case first recorded Itanium's wording
// and failed there for a reason that was right.
struct A { int a; };
struct B { int b; };
struct C : A, B { int c; };
int main() { throw C(); }
