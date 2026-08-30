// [dcl.fct.default]/4: the defaults have to be a suffix. A call fills them in
// from the right, so a parameter with no default sitting behind one that has
// it could never be reached - and saying so here beats letting the call site
// report that no function of that name takes these arguments.
int f(int a = 1, int b);

int main(void) { return f(1, 2); }
