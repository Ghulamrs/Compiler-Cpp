// The name is not visible outside the function that wrote it. Without this
// case "a local class works" could mean the tag was simply added to the file's
// types under a different spelling, which is what it must not be.
int f(void) { struct L { int x; }; L l; l.x = 1; return l.x; }
int g(void) { L l; l.x = 2; return l.x; }

int main(void) { return f() + g(); }
