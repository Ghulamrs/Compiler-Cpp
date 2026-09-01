// Each member fits the signed 32-bit count on its own - the array declarator
// checks that, and large-array.cpp pins it - but a class *sums* its members,
// `alignTo` takes an `int`, and the total was truncated in that call and laid
// out anyway: these two arrays made a `.zerofill` of -294967296, written by
// the shipped binary without a word, and `sizeof(Two)` answered the same
// negative number. The rule is large-array.cpp's, one level up: an object
// this compiler cannot measure is refused where it is written. No sanitizer
// finds this one - the truncation is a defined conversion - which is why it
// has to be a case.
struct Two { char a[2000000000]; char b[2000000000]; };
int main() { return 0; }
