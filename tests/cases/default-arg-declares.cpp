// **A default argument is re-parsed at the call**, and what it parses can
// declare a function: instantiating a function template appends to
// `functions_`, and so does building a closure. That vector is what overload
// resolution had just handed back a *reference* into - so the signature the
// caller was still reading, and that `applyDefaults` itself reads on every
// turn of its loop, was freed memory the moment it grew.
//
// It never crashed here; it read whatever the reallocation left behind. The
// symptom was a diagnostic naming a function with no name and a parameter
// that does not exist - "'' has no default for parameter 3" for a function of
// two - which is the freed Signature being printed.
//
// Both entry points hand back a copy now. The two shapes below are the two
// ways a default argument can declare something, and neither compiled before.
extern "C" int printf(const char *, ...);

template <class T> T id(T v) { return v; }

int viaTemplate(int a, int b = id(7)) { return a + b; }
int viaLambda(int a, int b = [](int v) { return v + 1; }(6)) { return a * b; }

int main() {
    printf("%d %d\n", viaTemplate(1), viaLambda(2));
    return 0;
}
