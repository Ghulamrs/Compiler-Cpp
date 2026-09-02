// The other neighbour: the same initialiser at file scope. A global is not
// stored by a run-time assignment but laid out as a static image, by a
// different function from the one a local goes through - so a rule written
// for locals alone would have left this compiling, and g holding 44.
char g = {300};
int main() { return g; }
