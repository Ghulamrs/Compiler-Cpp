// Member functions defined inside the class - the way nearly all C++ is
// written, and the reason it could not simply be parsed where it appears.
//
// `get()` calls `bonus()` and reads `n` and `step`, and every one of them is
// declared BELOW it. So the body cannot be read when it is met: it is held,
// stepped over, and replayed once the class is closed and every member exists.
// The replay re-reads the original tokens - return type, name, parameters,
// body - through the ordinary out-of-line definition path, so nothing about
// constructors, `this`, init lists, destructors or virtuals needed a second
// implementation.
//
// setStep() after a second `public:` is here because a held body must survive
// an access label between its declaration and the end of the class.
extern "C" { int printf(const char *, ...); }
class Counter {
public:
    Counter() { n = 0; }
    Counter(int start) : n(start) {}
    ~Counter() { printf("gone %d\n", n); }
    int get() const { return n + bonus(); }
    void bump() { n = n + step; }
private:
    int bonus() const { return 100; }
    int n;
    int step;
public:
    void setStep(int s) { step = s; }
};
int main(void) {
    Counter a;
    Counter b(5);
    a.setStep(3);
    a.bump();
    printf("%d %d\n", a.get(), b.get());
    return 0;
}
