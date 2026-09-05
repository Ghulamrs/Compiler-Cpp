// Copying a small class object out of a `const` reference.
//
// **This was a segfault, and the size of the class decided it.** `convert` fell
// through to a blanket `Cast` for a class differing only in const-ness, and a
// Cast reaches the backends' `genConversion`, which reads a class as a scalar
// and truncates its address to the object's *size*. So a class of 1, 2 or 4
// bytes lost the top of its own address and the copy wrote through a pointer
// that was never valid; a class of 8 bytes or more survived, because the
// truncation happened to keep the whole thing.
//
// [conv.qual] changes a type and not a value: the object keeps its address and
// its bytes, so there was never anything to convert. It is re-labelled now.
//
// The sizes below are the point of the case. It was found by
// `std::vector<Node>` where `struct Node { int id; };` - four bytes, and the
// most ordinary struct anyone writes - so a suite that tested one size would
// have had to test the wrong one to miss it.

extern "C" int printf(const char *, ...);

struct One   { char b[1]; };
struct Two   { char b[2]; };
struct Four  { char b[4]; };
struct Eight { char b[8]; };
struct Wide  { char b[24]; };

static void putOne(One *p, const One &v) { *p = v; }
static void putTwo(Two *p, const Two &v) { *p = v; }
static void putFour(Four *p, const Four &v) { *p = v; }
static void putEight(Eight *p, const Eight &v) { *p = v; }
static void putWide(Wide *p, const Wide &v) { *p = v; }

// The same conversion where the destination is a reference rather than a
// pointer, which is the shape a container's `items_[i] = v` takes.
static void intoRef(Four &d, const Four &s) { d = s; }

struct Holder {
    Four held;
    void keep(const Four &v) { held = v; }      // and through a member
};

int main(void) {
    One a; One as; as.b[0] = 'A'; putOne(&a, as);
    Two b; Two bs; bs.b[0] = 'B'; putTwo(&b, bs);
    Four c; Four cs; cs.b[0] = 'C'; putFour(&c, cs);
    Eight d; Eight ds; ds.b[0] = 'D'; putEight(&d, ds);
    Wide e; Wide es; es.b[0] = 'E'; putWide(&e, es);
    printf("%c%c%c%c%c\n", a.b[0], b.b[0], c.b[0], d.b[0], e.b[0]);

    Four f; Four fs; fs.b[0] = 'F'; intoRef(f, fs);
    Holder h; Four gs; gs.b[0] = 'G'; h.keep(gs);
    printf("%c%c\n", f.b[0], h.held.b[0]);
    return 0;
}
