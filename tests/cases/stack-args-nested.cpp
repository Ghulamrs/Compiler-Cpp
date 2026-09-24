// Calls with arguments on the stack, nested in every place an argument can
// be evaluated from. On x86_64-windows a call made at the frame's floor
// stores its stack arguments straight into the outgoing area, which is safe
// only if nothing evaluated after the store makes a call that writes the
// same words - so the register arguments and the later stack ones below call
// functions that take stack arguments of their own.
extern "C" int printf(const char *, ...);

long six(long a, long b, long c, long d, long e, long f) {
    return a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * f;
}

double mixed(int a, double b, int c, double d, double e, int f, double g) {
    return a + b * 2 + c * 3 + d * 4 + e * 5 + f * 6 + g * 7;
}

struct Pair { long x, y; };
long pairs(Pair a, Pair b, Pair c, Pair d, Pair e) {
    return a.x + b.y * 2 + c.x * 3 + d.y * 4 + e.x * 5;
}

long one(long x) { return x + 1; }

// Each result goes to a local first: an argument of printf is evaluated with
// printf's own arguments pushed above it, which is not the floor.
int main() {
    Pair p = {1, 2}, q = {3, 4};
    long r0 = six(1, 2, 3, 4, 5, 6);
    printf("%ld\n", r0);
    long r1 = six(six(1, 1, 1, 1, 1, 1), 2, 3, 4, 5, 6);
    printf("%ld\n", r1);
    long r2 = six(1, 2, 3, 4, six(1, 2, 3, 4, 5, 6), 6);
    printf("%ld\n", r2);
    long r3 = six(1, 2, 3, 4, 5, six(6, 5, 4, 3, 2, 1));
    printf("%ld\n", r3);
    long r4 = six(1, 2, 3, 4, one(5), one(6));
    printf("%ld\n", r4);
    long r5 = six(1, 2, 3, 4, six(1, 1, 1, 1, 1, 1), six(2, 2, 2, 2, 2, 2));
    printf("%ld\n", r5);
    double r6 = mixed(1, 2.5, 3, 4.5, 5.5, six(1, 2, 3, 4, 5, 6), 7.5);
    printf("%.1f\n", r6);
    long r7 = pairs(p, q, p, q, p);
    printf("%ld\n", r7);
    long r8 = six(1, 2, 3, 4, 5, 6) + six(six(1, 2, 3, 4, 5, 6), 0, 0, 0, 0, 1);
    printf("%ld\n", r8);
    return 0;
}
