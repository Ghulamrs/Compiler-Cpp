// [class.static.data]/2: `static` belongs to the declaration inside the class.
// The definition at namespace scope is written with the class in front and
// without the keyword - `double S::x = 0;` - because at namespace scope
// `static` would give the object internal linkage, which the member it
// defines cannot have. clang refuses this and so does cxx1, which used to
// accept it: the object was defined, the keyword was quietly dropped, and a
// second translation unit naming S::x would have found it anyway.
struct S { static double x; };
static double S::x = 0;

int main() { return 0; }
