// **A class template's initializer_list constructor is emitted.** The held body
// is replayed only when its own overload is marked used, and PendingBody::which
// recorded `functions_.size()` taken *before* the declaration - which is not
// where the signature lands when a parameter names a class template: declaring
// `Box(std::initializer_list<T>)` instantiates initializer_list<int> first, and
// that appends its own members. So `which` pointed at one of those, the gate
// never saw the constructor used, and the body was never replayed: declared,
// referenced, and undefined at link. signatureAddedUnder asks where the
// signature actually went.
#include <initializer_list>
extern "C" int printf(const char *, ...);

template <class T> struct Box {
    T a[4];
    int n;
    Box() : n(0) {}
    Box(std::initializer_list<T> l) {
        n = 0;
        for (typename std::initializer_list<T>::const_iterator it = l.begin();
             it != l.end(); ++it)
            a[n++] = *it;
    }
    T sum() const { T s = 0; for (int i = 0; i < n; ++i) s += a[i]; return s; }
};

int main() {
    Box<int> b{7, 8, 9};
    Box<int> empty;
    printf("%d %d %d\n", b.n, (int)b.sum(), empty.n);
    return 0;
}
