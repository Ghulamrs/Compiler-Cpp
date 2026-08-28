// Two bases cannot both sit at offset 0, and every address in this compiler
// assumes a base does. Refused by name rather than laid out wrongly.
class A { public: int a; };
class B { public: int b; };
class Both : public A, public B { public: int c; };
int main(void) { Both x; x.a = 1; return x.a; }
