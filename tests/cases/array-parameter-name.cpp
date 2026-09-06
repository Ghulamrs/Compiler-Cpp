// An array parameter's Microsoft name, which is `Q` where a pointer is `P`.
//
// [dcl.fct]/5 adjusts a parameter written as an array to a pointer and deletes
// its top-level cv, and the *type* is the adjusted one on both ABIs. **The
// Microsoft ABI mangles from before that adjustment**, so it keeps two facts
// Itanium throws away: that the parameter was written as an array, and that it
// was written const. Measured on cl, the oracle of record, and clang agrees:
//
//     int f(int a[], int n)         ?f@@YAHQEAHH@Z
//     int g(int *a, int n)          ?g@@YAHPEAHH@Z
//     int h(const int a[], int n)   ?h@@YAHQEBHH@Z
//     int k(int *const a, int n)    ?k@@YAHQEAHH@Z
//
// So `Q` there is "const pointer", and an array parameter is spelled as one.
// Itanium has no such distinction - `_Z1fPii` for every one of the four - and
// this case says so by being checked on all three targets.
//
// **Which spelling is mangled is decided by the first declaration**, not by the
// definition, and that was measured too: `decl_arr_def_ptr` below is declared
// with an array and defined with a pointer and is `Q`; `decl_ptr_def_arr` is
// the other way round and is `P`. cxx1 gets that for nothing, because a
// function's symbol is computed once, where it is first declared.
//
// The written list cannot be kept on the function *type*: a function type is
// interned on its parameters, so `f` and `g` above are one type, and hanging
// the spelling on it would give `g` a `Q` as well. That was the first attempt
// and names.sh caught it. The Microsoft mangler is handed a type of its own,
// made from the written list and used nowhere else.
//
// `volatile` is the piece that is still wrong, and it is older than this: cxx1
// discards the word in the declarator, so `int *volatile p` is `R` on cl and
// `P` here. Not a regression, and its own step - see CLAUDE.md.

extern "C" int printf(const char *, ...);

typedef int Three[3];

struct Holder { int v; };

int decl_arr_def_ptr(int a[], int n);
int decl_ptr_def_arr(int *a, int n);

int by_array(int a[], int n) { return a[n]; }
int by_pointer(int *a, int n) { return a[n]; }
int by_const_array(const int a[], int n) { return a[n]; }
int by_const_pointer(int *const a, int n) { return a[n]; }
int by_pointer_to_const(const int *a, int n) { return a[n]; }
int by_sized_array(int a[3], int n) { return a[n]; }
int by_typedef_array(Three a, int n) { return a[n]; }
int by_matrix(int a[2][3], int n) { return a[n][n]; }
int by_class_array(Holder a[], int n) { return a[n].v; }

// Two array parameters of one type: the second is a back-reference to the
// first, `QEAH0`, so the substitution table has to see the written spelling
// and not the adjusted one.
int by_two_arrays(int a[], int b[]) { return a[0] + b[0]; }

struct S {
    int member_by_array(int a[], int n);
    int member_by_pointer(int *a, int n);
};

int S::member_by_array(int a[], int n) { return a[n]; }
int S::member_by_pointer(int *a, int n) { return a[n]; }

// A constructor and a static member, because neither goes through the same
// door as an ordinary member: `declareConstructor` calls the Microsoft mangler
// itself, and a static member takes the memberSymbol path with an S rather
// than a Q for its access. Both were `P` until each was given the written
// list. Defined out of line, or clang emits only the C2 constructor and
// names.sh reports that as a mangling difference.
struct Made {
    int v;
    Made(int a[]);
    static int stat(int a[], int n);
};

Made::Made(int a[]) { v = a[0]; }
int Made::stat(int a[], int n) { return a[n]; }

int decl_arr_def_ptr(int *a, int n) { return a[n]; }
int decl_ptr_def_arr(int a[], int n) { return a[n]; }

int main() {
    int a[3] = { 4, 5, 6 };
    int m[2][3] = { { 1, 2, 3 }, { 7, 8, 9 } };
    Holder h[2];
    h[0].v = 11;
    h[1].v = 12;
    S s;

    printf("%d %d %d %d %d\n", by_array(a, 0), by_pointer(a, 1),
           by_const_array(a, 2), by_const_pointer(a, 0),
           by_pointer_to_const(a, 1));
    printf("%d %d %d %d\n", by_sized_array(a, 2), by_typedef_array(a, 0),
           by_matrix(m, 1), by_class_array(h, 1));
    printf("%d\n", by_two_arrays(a, a));
    printf("%d %d\n", s.member_by_array(a, 0), s.member_by_pointer(a, 1));
    printf("%d %d\n", decl_arr_def_ptr(a, 2), decl_ptr_def_arr(a, 0));

    Made made(a);
    printf("%d %d\n", made.v, Made::stat(a, 2));
    return 0;
}
