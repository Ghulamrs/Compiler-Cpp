int printf(const char *, ...);

int main(void) {
    bool t = true;
    bool f = false;
    bool wide = 256;
    bool neg = -1;
    double zero = 0.0;
    bool fromDouble = zero;
    double small = 0.5;
    bool fromSmall = small;
    int back = t;

    printf("%d %d %d %d %d %d %d\n", (int)t, (int)f, (int)wide, (int)neg,
           (int)fromDouble, (int)fromSmall, back);
    printf("%d\n", (int)sizeof(bool));
    printf("%ld\n", (long)__cplusplus);

    if (wide && !f) printf("logic\n");
    printf("%d %d\n", (int)(t == true), (int)(f != true));
    return 0;
}
