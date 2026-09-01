// **An override of a virtual belonging to a base that is not the first**,
// written without the keyword. [class.virtual]/2 makes it virtual whichever
// base declared the function it overrides, and the keyword changes nothing -
// but the slot search only ever looked at the first base's list, so this was
// declared non-virtual and a call through a `B *` reached B's own function.
// Writing `virtual` set the flag by hand and worked, which is why every case
// in the suite passed: they all write it.
//
// The two spellings emit identical objects now, thunk and all.
extern "C" int printf(const char *, ...);

struct A { virtual int fa() { return 1; } virtual ~A() {} };
struct B { virtual int fb() { return 2; } virtual ~B() {} };
struct D : A, B {
    int fa() { return 11; }         // first base, no keyword
    int fb() { return 22; }         // second base, no keyword - this is the bug
};

int main() {
    D d;
    A *pa = &d;
    B *pb = &d;
    printf("%d %d %d %d\n", pa->fa(), pb->fb(), d.fa(), d.fb());
    return 0;
}
