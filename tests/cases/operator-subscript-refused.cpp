// `operator[]` is still named and not reachable. The subscript path builds a
// pointer addition, which is the right rewrite for an array and the wrong one
// for a class, so until it learns to dispatch the declaration is refused where
// it is written rather than compiled into something nothing can call.
struct V {
    int x;
    int operator[](int i) const;
};

int main(void) { return 0; }
