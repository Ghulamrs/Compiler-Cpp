// SFINAE - [temp.deduct]/8. The arguments deduce, but substituting them into
// the signature makes something ill-formed, and that removes the
// specialization from consideration rather than ending the compile.
//
// `Value<double>` has no member called `type`, so `take(1.5)` has no template
// candidate at all and the ordinary `take(double)` takes the call. This is
// the only failure in this compiler that recovers, and it recovers exactly
// this far: a failure inside a *body* is still an error, which is where the
// standard puts the line too.
//
// The signature has to be spelled from the pattern, because that is what
// Itanium encodes: `_Z4takeIiEN5ValueIT_E4typeES1_` says `Value<T>::type`
// where the substituted signature says int and has forgotten where it came
// from. Kind::DependentMember is what carries that, and it exists for no
// other reason.
extern "C" { int printf(const char *, ...); }

template <class T> struct Value { };
template <> struct Value<int> { typedef int type; };
template <> struct Value<char> { typedef int type; };

struct HasType { typedef int type; };

int take(double x) { (void)x; return 2; }
template <class T> typename Value<T>::type take(T x) { (void)x; return 1; }

// The other shape of a dependent member: the parameter itself is the class.
int through(double x) { (void)x; return 2; }
template <class T> typename T::type through(T x) { (void)x; return 1; }

int main() {
    printf("%d %d %d\n", take(1), take('a'), take(1.5));
    HasType h;
    printf("%d %d\n", through(h), through(1.5));
    return 0;
}
