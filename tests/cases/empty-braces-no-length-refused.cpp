// `{}` counts nothing, so an array with no length has nothing to take one
// from - and an array of no elements is not C++. clang refuses it too, as an
// extension it will not take under -pedantic-errors. Without this the length
// would be inferred as zero and the array laid out with no room at all.
int main() { int a[] = {}; return sizeof a; }
