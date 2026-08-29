// `auto` with nothing to deduce from. Refused where it is written, naming the
// variable, rather than left to reach a type that has no size - which is what
// a stand-in does when nothing replaces it.
int main() { auto x; return 0; }
