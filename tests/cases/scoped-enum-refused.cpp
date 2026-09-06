// **A scoped enumeration is C++11 and is a type of its own.** Its enumerators
// do not leak into the enclosing scope and do not convert to int; an
// enumeration here is an int that remembers its name, which is the opposite,
// so it is refused rather than read as the unscoped one it is not.
enum class Colour { Red, Green };
int main() { Colour c = Colour::Red; return (int)c; }
