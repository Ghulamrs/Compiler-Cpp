// [expr.prim.lambda]/6 gives a capture-less closure a conversion function
// returning a function pointer that calls the body. The closure is a real
// class here and that function is not synthesised - so the refusal names the
// conversion rather than the closure type, which the program never wrote.
int main() { int (*p)(int) = [](int a) { return a; }; return p(0); }
