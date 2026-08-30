// `[&]` is still refused, and the reason is about layout rather than about
// lambdas. Capturing by reference means the closure holds a reference member,
// and a reference member is refused throughout this compiler because `sizeof`
// a reference is the size of what it refers to while the slot it needs is a
// pointer - so laying one out inside a class needs a rule the type system does
// not have yet. `[=]` and a named capture copy, and both work.
int main(void) {
    int k = 5;
    auto f = [&]() { return k; };
    return f();
}
