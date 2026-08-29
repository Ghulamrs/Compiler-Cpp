// An rvalue reference will not bind to an object that has an address. That is
// [dcl.init.ref] and it is the point of the type rather than a restriction on
// it: what makes taking a value apart safe is knowing nobody else can see it.
int main() { int n = 1; int &&r = n; return r; }
