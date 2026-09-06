// A reference with static storage duration, in both of its shapes.
//
// [basic.start.init]/2 lets a reference be bound during *constant*
// initialisation when its initialiser is a constant expression, and clang
// then writes the address into the image - `int &r = g;` is a `.quad g` and
// nothing runs. Anything else is bound before main, in the init function.
// cxx1 draws the line at `&global`: that goes into the image, and every other
// initialiser is bound dynamically - which is a conforming choice, since a
// program cannot tell one from the other except through the order of
// initialisation across translation units.
//
// A static local reference is [stmt.dcl]/4's rule instead: bound the first
// time control passes through, under the same guard a static local with a
// constructor uses, and left alone on every later pass.
extern "C" int printf(const char *, ...);

int g = 5;
int arr[3] = { 10, 20, 30 };
struct S { int v; S(int a); };
S::S(int a) : v(a) {}
S obj(3);

int *pick() { return &arr[1]; }

int &r = g;                 // the address of a global: in the image
const int &cr = g;          // const, to the same object
S &sr = obj;                // a class object
int &dr = *pick();          // not a constant: bound before main
int &ar = arr[2];           // an element: bound before main too
extern int &er;             // declared here, defined below
int &er = g;

int &viaStatic(int *p) {
    static int &once = *p;  // bound on the first call, to that call's p
    return once;
}

int main() {
    printf("%d %d %d %d %d %d\n", r, cr, sr.v, dr, ar, er);
    r = 6;
    dr = 21;
    sr.v = 4;
    printf("%d %d %d %d %d\n", g, arr[1], obj.v, cr, er);
    int a = 1, b = 2;
    viaStatic(&a) = 100;
    viaStatic(&b) = 200;
    printf("%d %d %d\n", a, b, viaStatic(&b));
    return 0;
}
