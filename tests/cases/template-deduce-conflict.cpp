// One parameter, two arguments, two different types. C++ does not pick one:
// [temp.deduct] makes the deduction fail, and with no other candidate of that
// name the call has nothing to go to. clang says the same thing in two lines;
// this says it in one, and names both types, because that is the whole of
// what the reader has to fix.
template <class T> T bigger(T a, T b) { return a > b ? a : b; }
int main() { return (int)bigger(1, 2.0); }
