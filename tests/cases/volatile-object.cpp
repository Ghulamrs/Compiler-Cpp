// `volatile` on an object, which is the half of it this compiler is right about.
//
// There is no volatile in cxx1's type system: the word is read and dropped. The
// 2026-09-07 sweep asked what that costs, one program per shape, and the answer
// splits cleanly in two. **On an object it costs nothing**, and for a reason
// worth writing down: cxx1 optimises nothing, so every read of every object is
// already a load from its address and every write a store. `volatile`'s runtime
// guarantee holds here by construction rather than by being implemented - which
// is why these shapes are accepted rather than refused.
//
// The other half is a type a linkage name is made from, and there the qualifier
// is part of the name on both ABIs. Those are refused by name:
// volatile-pointer-refused, volatile-pointer-itself-refused,
// volatile-member-function-refused and volatile-typedef-refused are the four.
//
// **`volatile int g;` at namespace scope is the line between the two**, and it
// is a difference of ABI rather than of language: cl decorates a variable's name
// with its cv - `?g@@3HC` where a plain int is `?g@@3HA` - and neither Itanium
// target decorates a variable at all. So it is right on two targets and would be
// silently wrong on the third, and this case names that third in its .notarget.
//
// A function-local `static volatile` belongs in this list and is not in it: the
// sweep found that cxx1 names *any* function-local static `f.n` where Itanium
// writes `_ZZ1fvE1n`, which has nothing to do with volatile and is recorded on
// its own. Putting one here would have made this case red for that.
//
// The C corpus reads the same way: `qs_volatile.c`, `qs_qualifier_order.c`,
// `hd_setjmp.c` and `hd_standard_headers.c` all use volatile in exactly these
// shapes, and 365/58/1 was unchanged by the sweep.

extern "C" int printf(const char *, ...);

volatile int external = 5;
static volatile int internalG = 6;

struct Holder { volatile int v; };

static int byValue(volatile int n) { return n + 1; }

int main() {
    volatile int local = 7;
    // Three reads of one volatile object, which must be three loads. cxx1 emits
    // a load per read for every object, volatile or not - see the sweep.
    int a = local, b = local, c = local;

    volatile char pad[4];
    pad[0] = 3;

    Holder h;
    h.v = 9;

    const volatile int frozen = 11;

    printf("%d %d %d\n", external, internalG, a + b + c);
    printf("%d %d %d\n", byValue(1), pad[0], h.v);
    printf("%d\n", frozen);
    return 0;
}
