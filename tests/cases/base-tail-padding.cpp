// **Tail padding is reused for a base that is not a POD, and not for one that
// is.** Itanium sets dsize == sizeof for a POD and only lets a derived class
// into the padding of a class that is not one; the Microsoft ABI never lets it
// in at all. cxx1 reused it for every base, so `struct TD : TP` came out 8
// bytes where all three oracles say 12 - and the consequence is not a number:
// assigning through a `TP *` wrote over the derived member, because the two
// objects overlapped.
//
// The empty base is here to hold the other half of the rule down. An empty
// class still contributes nothing, on every ABI, and that is the empty base
// optimisation rather than tail padding - fixing one must not break the other.
extern "C" int printf(const char *, ...);

struct TP { int a; char b; };                   struct TD : TP { char c; };
struct NP { int a; char b; NP() {} ~NP() {} };  struct ND : NP { char c; };
struct E1 { };                                  struct EB : E1 { int x; };
struct P1 { long long a; char b; };
struct P2 { long long c; char d; };
struct MI : P1, P2 { char e; };
struct V { virtual void f() {} int a; char b; };  struct VD : V { char c; };

static_assert(sizeof(TP) ==  8, "TP is int + char, padded to its int");
static_assert(sizeof(TD) == 12, "a POD base keeps its padding on every ABI");
static_assert(sizeof(EB) ==  4, "an empty base costs nothing on every ABI");
static_assert(sizeof(MI) == 40, "two POD bases, neither one's padding reused");
static_assert(sizeof(V)  == 16, "vptr, int, char, padded to the vptr");

#ifdef _WIN32
static_assert(sizeof(ND) == 12, "cl never reuses tail padding, POD or not");
static_assert(sizeof(VD) == 24, "nor a polymorphic base's");
#else
static_assert(sizeof(ND) ==  8, "a non-POD base's padding is reused on Itanium");
static_assert(sizeof(VD) == 16, "and a polymorphic base is not a POD");
#endif

int main() {
    TD d;
    d.c = 77;
    TP p;
    p.a = 1;
    p.b = 2;
    *(TP *)&d = p;          // legal: it names the base subobject
    printf("%d %d\n", d.c, d.a);
    return 0;
}
