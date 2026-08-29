// `Box<Box<int>>` - the `>>` the lexer hands over as one token.
//
// The two `>` close two different argument lists, so the token has to split.
// It cannot split by inserting a second token: held bodies and templates
// record absolute token indices and an insert would move all of them. The
// first `>` is taken by leaving a marker at the index instead of advancing.
//
// Written both ways here, spaced and tight, and they must be the same type -
// which is what the two assignments through one `Wrap` prove. Until class
// templates this was a refusal case, and the refusal it wanted was the
// class-template one rather than "this argument list is never closed", which
// is what a broken split says.
extern "C" { int printf(const char *, ...); }

template <class T> struct Box {
    T slot;
    T get() { return slot; }
};
template <class T> struct Wrap {
    Box<T> inner;
    T peel() { return inner.get(); }
};

int main() {
    Box<Box<int> > spaced;
    Box<Box<int>> tight;
    spaced.slot.slot = 3;
    tight.slot.slot = 4;
    printf("%d %d\n", spaced.get().slot, tight.get().slot);

    Wrap<int> w;
    w.inner.slot = 5;
    printf("%d\n", w.peel());
    return 0;
}
