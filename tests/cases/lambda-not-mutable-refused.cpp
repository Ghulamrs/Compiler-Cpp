// Without `mutable` the call operator is const, so a by-value capture cannot be
// written through - the member is const inside the body like any other member
// of a const object. The other half of lambda-mutable, and the reason that one
// proves anything.
int main(void) {
    int k = 5;
    auto f = [k]() { k = k + 1; return k; };
    return f();
}
