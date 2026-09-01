// A shift count outside 0 to 63 is undefined rather than large, and this
// evaluator is inside a compiler - so it is named where it is written rather
// than left to whatever the host does with it.
#if (1 << 100)
#endif
int main() { return 0; }
