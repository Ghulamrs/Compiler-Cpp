// `P(1)` - a temporary of class type, and the gap that closed with it.
//
// It was reachable three ways and refused in all of them: as an expression,
// as `return P(1);`, and as `static_cast<T &&>` of a prvalue - which is what
// made it the most valuable thing left. One gap, three symptoms.
//
// **The object goes in a slot of this frame and the expression answers with
// its name**, the constructor sequenced in front by a comma. The shape is
// `*(P::P(&tmp, 1), &tmp)` and not `(P::P(&tmp, 1), tmp)`, which looks more
// natural and cannot work: a comma has its right operand's value category, so
// the parser would let anyone take its address, and taking the address of a
// comma is not something the three backends know. The address of `*p` is `p`,
// which all of them do know.
extern "C" { int printf(const char *, ...); }

struct P {
    int x;
    P(int a);
    P(int a, int b);
};

P::P(int a) { x = a; }
P::P(int a, int b) { x = a * 10 + b; }

struct M {
    int x;
    M(int a);
    M(const M &o);
    M(M &&o);
};

M::M(int a) { x = a; }
M::M(const M &o) { x = o.x; }
M::M(M &&o) { x = o.x + 100; }        // a move is visible in the answer

int take(P p) { return p.x; }
int taken(M m) { return m.x; }
P make(int n) { return P(n); }

int main(void) {
    printf("%d %d %d %d %d\n",
           P(1).x,                     // in an expression
           P(2, 3).x,                  // choosing between constructors
           take(P(4)),                 // passed by value
           make(5).x,                  // returned
           taken(static_cast<M &&>(M(6))));   // and moved from
    return 0;
}
