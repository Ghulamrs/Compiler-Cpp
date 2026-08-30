// [class.local]: a class defined in a function body belongs to that function.
//
// Four things here, and each was broken in its own way before:
//
//   * two functions may each define `struct L`, and they are two types. The
//     tag used to go into the one table every class shares, so the second
//     function was told its own class was "defined twice".
//   * a global class of the same name is shadowed inside the function and is
//     still itself everywhere else.
//   * the enclosing function keeps its parameters. A local class's member
//     bodies are replayed between the class closing and the next statement,
//     through the same path that sets a function up - so it emptied the table
//     `k` lived in, and `h` was told its own parameter was not declared.
//   * and the member functions are named by wrapping the enclosing function's
//     whole name, which is what keeps the two `L::get` apart in one object
//     file: `_ZZ1fvEN1L3getEv` against `_ZZ1gvEN1L3getEv`, measured.
extern "C" { int printf(const char *, ...); }

struct L { int g; };

int useGlobal(void) { L l; l.g = 1; return l.g; }

int f(void) {
    struct L { int x; int get() { return x * 2; } };
    L l;
    l.x = 5;
    return l.get();
}

int g(void) {
    struct L { int y; int get() { return y * 3; } };
    L l;
    l.y = 5;
    return l.get();
}

int h(int k) {
    struct M { int z; int get() { return z; } };
    M m;
    m.z = k;                 // the parameter, after the class
    return m.get();
}

int main(void) {
    printf("%d %d %d %d\n", useGlobal(), f(), g(), h(7));
    return 0;
}
