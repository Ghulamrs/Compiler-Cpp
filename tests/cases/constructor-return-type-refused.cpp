// [class.ctor]/3: a constructor has no return type. The constructor branch of
// the class parser never saw this one - it looks for the class's name at the
// *start* of a member declaration, and here a type was read first - so cxx1
// made a member function called `S` that nothing could ever call by that name,
// and `S s;` went on using the implicit default constructor. `void` is refused
// the same way, which is the spelling clang names in its own message.
extern "C" int printf(const char *, ...);
struct S { int S(); };
int main() { printf("x\n"); return 0; }
