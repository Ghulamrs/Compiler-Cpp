// **A friend function defined inside the class body.** [class.friend]: the
// function belongs to the enclosing namespace and the class only grants it
// access - so the body is held with the class, where its tokens are, and
// replayed with *no* owner, through the ordinary free-function path rather than
// the member one. The replay begins after `friend`, the same rule `virtual`
// follows: the keyword is written on the declaration and nowhere else.
//
// [class.friend]/6 makes such a definition implicitly inline, which is what a
// replayed body already is. Reaching a private member is the point of writing
// it here, and `peek` does.
extern "C" int printf(const char *, ...);

class Box {
    friend int peek(const Box &b) { return b.x; }
    friend Box twice(const Box &b) { return Box(b.x * 2); }
    int x;
public:
    Box(int v) : x(v) {}
};

int main() {
    Box b(21);
    printf("%d %d\n", peek(b), peek(twice(b)));
    return 0;
}
