// new and delete, which finish rung 2.
//
// The four operator functions this calls are the platform's own - their names
// were measured with clang for all three targets rather than read from a
// description, which is the same rule the mangler was built under. That is
// what makes a `new` here and a `delete` in a translation unit built by clang
// the same allocation.
extern "C" {
int printf(const char *, ...);
}

int main(void) {
    int *one = new int(41);
    int *zero = new int();
    int *many = new int[4];
    char *text = new char('A');
    double *d = new double(1.5);
    int i;
    int n = 3;
    int *runtime = new int[n];

    *one = *one + 1;
    for (i = 0; i < 4; i = i + 1) many[i] = i * i;
    for (i = 0; i < n; i = i + 1) runtime[i] = i + 10;

    printf("%d %d %d %d\n", *one, *zero, many[3], many[2]);
    printf("%d %d %d\n", (int)*text, (int)(*d * 2.0), runtime[2]);

    delete one;
    delete zero;
    delete text;
    delete d;
    delete[] many;
    delete[] runtime;
    return 0;
}
