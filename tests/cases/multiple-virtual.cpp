// A second base with virtual functions needs its own vtable at a non-zero
// offset, and thunks to adjust `this` on the way into an override. Refused by
// name rather than laid out with one vptr where two are needed.
class Plain { public: int p; };
class Poly { public: Poly(); virtual int f(); int q; };
Poly::Poly() { q = 1; }
int Poly::f() { return 1; }
class Both : public Plain, public Poly { public: Both(); int r; };
Both::Both() { r = 2; }
int main(void) { Both x; return x.r; }
