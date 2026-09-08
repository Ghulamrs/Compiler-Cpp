// [class.temporary]/5: a temporary bound to a reference lives as long as the
// reference, not to the end of the full expression that made it. cxx1 built the
// temporary and destroyed it never - `+1|` where every compiler prints `+1|-1` -
// so a destructor with an observable effect did not run and anything the object
// owned leaked.
//
// The cases that matter are the ones either side of the rule: an object that
// already exists is *not* extended, because it belongs to whoever declared it;
// two references in one scope destroy in reverse order; a loop destroys each
// turn's own; and a temporary that is only an argument still dies at the
// semicolon, which is what tells extension apart from never destroying anything.
extern "C" int printf(const char *, ...);

struct T {
    int n;
    T(int v) : n(v) { printf("+%d", v); }
    ~T() { printf("-%d", n); }
};

T make(int v) { return T(v); }
int use(const T &a, int k) { return a.n + k; }

int main() {
    { const T &r = T(1); printf("|"); }               printf("\n");
    { T t(2); const T &r = t; printf("|%d", r.n); }   printf("\n");
    { const T &r = make(3); printf("|"); }            printf("\n");
    { printf("%d", use(T(4), 0)); }                   printf("\n");
    { const T &a = T(5); const T &b = T(6); printf("|"); } printf("\n");
    for (int i = 7; i < 9; i++) { const T &r = T(i); printf("|"); }
    printf("\n");
    return 0;
}
