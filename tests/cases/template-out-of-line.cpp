// Members of a class template defined outside it - rung 5.5.
//
// The plan called this "the qualified-name path with a template-id in it",
// and that is what it turned out to be: one place in the declarator had to
// learn that a name it just read may be a class template, in which case what
// follows is an argument list and the class it makes is the qualifier.
// Everything after the `::` is read by the loop a nested class already needed.
//
// A definition like this belongs to the *class* and not to a template of its
// own - the member was declared in the body, and this is only where its
// definition happens to be written. So it is kept on the class template and
// replayed when a specialization needs it, which means it may be written
// further down the file than the use that asked for the class. `late` here is
// defined after main and still reached.
//
// `unused` is defined and never called, and must produce no symbol: clang and
// cl instantiate a member of a class template only where something calls one.
extern "C" { int printf(const char *, ...); }

template <class T, int N> struct Box {
    T slot[N];
    int size();
    T get(int i);
    void put(int i, T v);
    T doubled(int i);          // calls another member, itself out of line
    int unused();
    int late();
};

template <class T, int N> int Box<T, N>::size() { return N; }
template <class T, int N> T Box<T, N>::get(int i) { return slot[i]; }
template <class T, int N> void Box<T, N>::put(int i, T v) { slot[i] = v; }
template <class T, int N> T Box<T, N>::doubled(int i) { return get(i) + get(i); }
template <class T, int N> int Box<T, N>::unused() { return 0; }

// A class template with one member written inside and one outside.
template <class T> struct Mixed {
    T item;
    T inside() { return item; }
    T outside();
};
template <class T> T Mixed<T>::outside() { return inside(); }

int main() {
    Box<int, 3> b;
    b.put(0, 7);
    printf("%d %d %d\n", b.size(), b.get(0), b.doubled(0));

    Box<double, 2> d;
    d.put(1, 1.5);
    printf("%d %.1f\n", d.size(), d.get(1));

    Mixed<int> m;
    m.item = 4;
    printf("%d %d\n", m.inside(), m.outside());

    printf("%d\n", b.late());
    return 0;
}

template <class T, int N> int Box<T, N>::late() { return 99; }
