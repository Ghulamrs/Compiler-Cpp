// **Two function templates of one name overload.** Each is a candidate; a call
// deduces against every one and overload resolution ranks the specializations
// with any ordinary functions. Two *class* templates of one name are still
// refused - that is partial specialization, a different path. Measured against
// clang -std=c++11 -pedantic-errors.
extern "C" int printf(const char *, ...);
template <class T> T pick(T x) { return x; }
template <class T> T pick(T x, T y) { return x + y; }
int main() {
    printf("%d %d\n", pick(7), pick(3, 4));
    return 0;
}
