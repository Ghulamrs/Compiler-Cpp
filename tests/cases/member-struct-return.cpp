// **The Microsoft size rule for returning a class is about free functions,
// and a member function never uses it.** cl gives a class returned by value
// from a non-static member function the hidden pointer whatever its size -
// `Small { int }` from a member comes back through the pointer in rdx, with
// `this` in rcx and the first written parameter pushed along to r8 - where
// the same struct from a free function comes back in eax. Measured with
// clang for this ABI on both sides, and it is the documented rule: a UDT
// can be returned in rax from a global function only.
//
// cxx1 learned the member order for a *large* struct and kept the size test
// for a small one, so `sget` returned in eax with x in rdx - on both sides
// of every call it generated, which is why every case agreed with itself
// and disagreed with cl. The register question itself is pinned where a cl
// caller can ask it, in tools/windows/sret-check.cmd; what this case holds
// down is that both sizes of member return, the free-function counterpart,
// and a call through a member function pointer all still answer the same
// thing everywhere - the Itanium targets never had a member rule to learn.
extern "C" int printf(const char *, ...);

struct Small { int v; };
struct Pair { int a; int b; };
struct Big { long long a, b, c; };

struct W {
    long long k;
    Small sget(int x);
    Pair pget(int x, int y);
    Big bget(int x);
};
Small W::sget(int x) { Small r; r.v = (int)k + x; return r; }
Pair W::pget(int x, int y) { Pair r; r.a = (int)k * x; r.b = y; return r; }
Big W::bget(int x) { Big r; r.a = k; r.b = x; r.c = k + x; return r; }

Small freeSmall(int x) { Small r; r.v = x * 3; return r; }
Pair freePair(int x) { Pair r; r.a = x; r.b = x + 1; return r; }

int main() {
    W w; w.k = 10;
    Small s = w.sget(7);            // member, 4 bytes
    Pair p = w.pget(2, 9);          // member, 8 bytes
    Big b = w.bget(5);              // member, 24 bytes - the original
    Small fs = freeSmall(4);        // free, 4 bytes - stays in the register
    Pair fp = freePair(6);          // free, 8 bytes

    // Through a member function pointer, which is a member call however it
    // is spelled - same hidden pointer, same `this` first.
    Pair (W::*pm)(int, int) = &W::pget;
    Pair q = (w.*pm)(3, 8);

    printf("%d %d %d %lld %lld %lld %d %d %d %d %d\n",
           s.v, p.a, p.b, b.a, b.b, b.c, fs.v, fp.a, fp.b, q.a, q.b);
    return 0;
}
