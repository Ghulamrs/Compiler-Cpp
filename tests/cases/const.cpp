// const is part of a type here, not a flag on the object that has it. This
// case is the part that must go on working: reading through it, passing it,
// and letting a writable thing be used where a const one is asked for.
int printf(const char *, ...);
int strcmp(const char *, const char *);

struct Point { int x; int y; };

const int limit = 10;

static int total(const int *values, int n) {
    int sum = 0;
    for (int i = 0; i < n; i++) sum += values[i];
    return sum;
}

static int firstX(const struct Point *p) { return p->x; }

int main(void) {
    int values[3];
    values[0] = 1; values[1] = 2; values[2] = 39;

    // A writable pointer converts to a pointer-to-const on its own; the other
    // direction is the one that needs a cast, and const-drop.cpp holds it.
    printf("%d\n", total(values, 3));

    const char *text = "const";
    printf("%d %d\n", strcmp(text, "const"), (int)*text);

    const struct Point origin = { 4, 5 };
    printf("%d %d\n", origin.x + origin.y, firstX(&origin));

    int n = 7;
    int *const fixed = &n;
    *fixed = 8;
    printf("%d %d\n", *fixed, limit);

    // The const at the top of a type is dropped by a copy, in both
    // directions - a struct is the case that has to be said out loud, since
    // the arithmetic rule covers 'const int' by itself.
    const struct Point copy = origin;
    struct Point back = copy;
    printf("%d %d\n", copy.x, back.y);

    const void *opaque = text;
    printf("%d\n", (int)(opaque != 0));
    return 0;
}
