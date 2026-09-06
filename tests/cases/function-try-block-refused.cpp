// A function try block's handler covers the mem-initialisers as well as the
// body - [except.pre] - so it can catch what a base or member constructor
// threw, which a `try` written inside the body cannot. C++98.
int f() try { return 0; } catch (...) { return 1; }
int main() { return f(); }
