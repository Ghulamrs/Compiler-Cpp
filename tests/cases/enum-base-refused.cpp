// **An enum-base fixes the underlying type**, which is what makes an
// enumeration's size and range something the program chose. Every enumeration
// is an int here, so a written base would be accepted and then ignored.
enum E : unsigned char { A, B };
int main() { return (int)A; }
