// `0b101` is C++14 and `0x1f` is C++98, one character apart. Refused by name
// at the digits: without it the literal lexes as `0` followed by an
// identifier, and the error lands on whatever follows the number.
int main() { int n = 0b101; return n - 5; }
