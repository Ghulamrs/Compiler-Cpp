// A class template used inside another class template.
//
// **Two faults, both invisible until a container was built out of a
// container**, and `map` holding a `vector` is exactly that.
//
// The first was a name. `inner<K> held_;` inside `outer<K>` instantiates
// `inner<int>` while `outer<int>`'s own body is being read, and the class
// context still in effect made the new class *nested*: it took the tag
// `outer<int>::inner<int>`, which nothing else forms. So its constructor was
// declared under one name and emitted under none, and the program did not
// link. A specialization is not a member of whatever class asked for it.
//
// The second was an order. A template's member body is replayed only once
// something uses it, and an implicit constructor is synthesised only once
// something needs one - and a synthesised constructor *is* a use. `outer<int>`
// has no constructor of its own, so the one that calls `inner<int>::inner()`
// was synthesised after the replay had finished. The two passes alternate now.
//
// What hid both: naming `inner<int>` anywhere at top level made the program
// link, because then the inner template was instantiated from a context with
// no enclosing class and its members were used before the replay. So a case
// that mentioned both templates would have passed.

extern "C" int printf(const char *, ...);

template <class T>
class inner {
public:
    inner() : n_(0) {}
    ~inner() { n_ = -1; }
    void put(const T &v) { last_ = v; n_++; }
    T last_;
    int n_;
};

template <class K>
class outer {
public:
    void add(const K &k) { held_.put(k); }
    int count(void) const { return held_.n_; }
    inner<K> held_;
};

// Three deep, which is what `map<string, vector<int> >` is.
template <class T>
class deeper {
public:
    void add(const T &v) { held_.add(v); }
    int count(void) const { return held_.count(); }
    outer<T> held_;
};

int main(void) {
    outer<int> o;
    o.add(5);
    o.add(6);
    printf("%d %d\n", o.count(), o.held_.last_);

    deeper<int> d;
    d.add(9);
    printf("%d %d\n", d.count(), d.held_.held_.last_);

    // A second instantiation of the same inner template, through a different
    // outer one - the specialization must be shared, not made twice.
    outer<char> c;
    c.add('z');
    printf("%d %c\n", c.count(), c.held_.last_);
    return 0;
}
