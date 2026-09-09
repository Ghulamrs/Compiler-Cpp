// A polymorphic class template emits a type-info name spelled from the tag
// rather than from the mangled template-id - `__ZTS6P<int>` - which the
// assembler refuses. Any virtual function in a class template reaches it.
extern "C" int printf(const char *, ...);
template <class T> struct P { virtual ~P() {} T v; };
int main() { P<int> p; p.v = 3; printf("%d\n", p.v); return 0; }
