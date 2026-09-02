// **`new P()` value-initialises, on the heap as on the stack.** [dcl.init]/8
// again, the rule class-temporary.cpp pins for `P()` in an expression - and
// the half the first fix missed: the new-expression's no-constructor branch
// converted its literal zero *to the class type*, which lowered to a copy
// whose source address was the zero itself, and `new P()` read address 0 and
// died. Every leaf must come back zero: array members, a nested class, the
// odd char after them, and a plain scalar and two-int class beside it.
extern "C" int printf(const char *, ...);

struct Q { int x; int y; };
struct P { int a[3]; Q q; char c; };
struct E { };

int main() {
    P *p = new P();
    Q *q = new Q();
    E *e = new E();
    int *i = new int();
    printf("%d %d %d %d %d %d %d %d %d %d\n", p->a[0], p->a[1], p->a[2],
           p->q.x, p->q.y, (int)p->c, q->x, q->y, *i, e != 0 ? 1 : 0);
    return 0;
}
