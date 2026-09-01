// **An aggregate whose size is not a multiple of eight has a partial lane**,
// and every place one is moved used to write it with the single largest
// instruction that fit: two bytes for a three-byte tail, four for a six.
// The rest of the object was left as whatever the destination held, so
// `struct { char c[3]; }` passed by value arrived with its last byte missing.
// Silent, and data.
//
// Five places had it - the caller's push of a memory-class aggregate, the
// callee's spill of a stack argument and of a register lane, the caller's
// store of a returned struct, and the callee's load of the return lanes - and
// all five are on the two Itanium targets. x86_64-windows never had it:
// anything that is not a power of two goes by reference there.
//
// Sizes 1 to 15 cover every shape of tail: 3, 5, 6 and 7 are the ones that
// were wrong, and the powers of two are here so a fix cannot trade one for
// the other. The sums are triangular numbers, so a dropped byte cannot
// coincide with the right answer.
extern "C" int printf(const char *, ...);

struct T1  { char c[1];  };  struct T2  { char c[2];  };
struct T3  { char c[3];  };  struct T4  { char c[4];  };
struct T5  { char c[5];  };  struct T6  { char c[6];  };
struct T7  { char c[7];  };  struct T8  { char c[8];  };
struct T9  { char c[9];  };  struct T11 { char c[11]; };
struct T13 { char c[13]; };  struct T15 { char c[15]; };

static int sum(const char *c, int n) {
    int t = 0;
    for (int i = 0; i < n; i++) t += c[i];
    return t;
}

int s1(T1 v)   { return sum(v.c, 1); }
int s2(T2 v)   { return sum(v.c, 2); }
int s3(T3 v)   { return sum(v.c, 3); }
int s4(T4 v)   { return sum(v.c, 4); }
int s5(T5 v)   { return sum(v.c, 5); }
int s6(T6 v)   { return sum(v.c, 6); }
int s7(T7 v)   { return sum(v.c, 7); }
int s8(T8 v)   { return sum(v.c, 8); }
int s9(T9 v)   { return sum(v.c, 9); }
int s11(T11 v) { return sum(v.c, 11); }
int s13(T13 v) { return sum(v.c, 13); }
int s15(T15 v) { return sum(v.c, 15); }

// Returned by value as well as passed: a 5-byte struct comes back in a
// register lane the caller has to store, and the callee has to load.
T5 make5() { T5 v; for (int i = 0; i < 5; i++) v.c[i] = (char)(i + 1); return v; }
T7 make7() { T7 v; for (int i = 0; i < 7; i++) v.c[i] = (char)(i + 1); return v; }

int main() {
    T1 a1;   for (int i = 0; i < 1;  i++) a1.c[i]  = (char)(i + 1);
    T2 a2;   for (int i = 0; i < 2;  i++) a2.c[i]  = (char)(i + 1);
    T3 a3;   for (int i = 0; i < 3;  i++) a3.c[i]  = (char)(i + 1);
    T4 a4;   for (int i = 0; i < 4;  i++) a4.c[i]  = (char)(i + 1);
    T5 a5;   for (int i = 0; i < 5;  i++) a5.c[i]  = (char)(i + 1);
    T6 a6;   for (int i = 0; i < 6;  i++) a6.c[i]  = (char)(i + 1);
    T7 a7;   for (int i = 0; i < 7;  i++) a7.c[i]  = (char)(i + 1);
    T8 a8;   for (int i = 0; i < 8;  i++) a8.c[i]  = (char)(i + 1);
    T9 a9;   for (int i = 0; i < 9;  i++) a9.c[i]  = (char)(i + 1);
    T11 b11; for (int i = 0; i < 11; i++) b11.c[i] = (char)(i + 1);
    T13 b13; for (int i = 0; i < 13; i++) b13.c[i] = (char)(i + 1);
    T15 b15; for (int i = 0; i < 15; i++) b15.c[i] = (char)(i + 1);

    printf("%d %d %d %d %d %d %d %d %d %d %d %d\n",
           s1(a1), s2(a2), s3(a3), s4(a4), s5(a5), s6(a6),
           s7(a7), s8(a8), s9(a9), s11(b11), s13(b13), s15(b15));

    T5 r5 = make5();
    T7 r7 = make7();
    printf("%d %d\n", sum(r5.c, 5), sum(r7.c, 7));
    return 0;
}
