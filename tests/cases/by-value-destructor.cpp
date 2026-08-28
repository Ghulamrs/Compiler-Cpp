// A class with a destructor and nothing else non-trivial, passed by value.
//
// **This is where the two ABIs genuinely part company**, and both were
// measured. Itanium passes such a class *by address* whatever its size - the
// caller makes the copy, and the **caller** destroys it. Microsoft passes it by
// the ordinary size rules, in a register if it fits, and the **callee**
// destroys its own parameter: cl's `?useSmall@@YAHUSmall@@@Z` calls
// `??1Small@@QEAA@XZ` on its parameter before returning, and the caller emits
// no destructor for it at all.
//
// Both return such a class through a hidden pointer.
//
// So the copy is destroyed at a different *moment* on the two platforms - at
// the end of the full expression on Itanium, and inside the callee on Windows.
// Nothing below puts anything observable between those two points: the result
// of a call is taken into a variable before it is printed, so the full
// expression ends before the printf either way. A case that printed inside the
// same expression would have two right answers and no single recorded output.
//
// The other half is that a returned local is *not* destroyed on the way out.
// Its bytes go straight to the caller's storage and the caller destroys it
// there - one construction and one destruction. Destroying it here as well
// would destroy the same object twice, which for a class that owns anything is
// a double free.
extern "C" { int printf(const char *, ...); }

struct D {
    int n;
    D(int v);
    ~D();
};
D::D(int v) { n = v; }
D::~D()     { printf("  ~D %d\n", n); }

struct Wide {
    int a[8];
    ~Wide();
};
Wide::~Wide() { printf("  ~Wide %d\n", a[0]); }

struct Plain { int n; };            // trivial: no copy, no destruction

int takeD(D d)         { return d.n; }
int takeWide(Wide w)   { return w.a[0]; }
int takePlain(Plain p) { return p.n; }
D   giveD()            { D d(9); return d; }
void voidTake(D d)     { printf("  inside %d\n", d.n); }

int main() {
    {
        D d(1);
        int v = takeD(d);            // the copy is gone by the end of this
        printf("takeD %d\n", v);
    }
    printf("---\n");
    {
        Wide w;
        w.a[0] = 2;
        int v = takeWide(w);
        printf("takeWide %d\n", v);
    }
    printf("---\n");
    {
        Plain p;
        p.n = 3;
        printf("takePlain %d\n", takePlain(p));
    }
    printf("---\n");
    {
        D r = giveD();               // one construction, one destruction
        printf("gave %d\n", r.n);
    }
    printf("---\n");
    {
        D d(4);
        voidTake(d);                 // the copy goes before this returns
        printf("after\n");
    }
    printf("end\n");
    return 0;
}
