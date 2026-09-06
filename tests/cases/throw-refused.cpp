// **What `throw` still cannot name**, now that a class can be thrown.
//
// This case used to say "a fundamental type and nothing else", which was true
// when it was written and stopped being true when the class type_info the
// `dynamic_cast` work already emits was reached from the throw path. What is
// left is the shape below: a *pointer* wants `__pointer_type_info`, a third
// kind beside `__class_type_info` and `__si_class_type_info`, carrying the
// pointee's own object and the qualifiers it was written with. None of that
// is built, and the Microsoft side wants a descriptor of its own too - so
// both ABIs still refuse this and each says why in its own terms.
//
// throw-class.cpp is the half that works, and
// throw-multiple-bases-refused.cpp the other thing still refused.
struct E { int v; };
static E e;
void f() { e.v = 1; throw &e; }
int main() { f(); return 0; }
