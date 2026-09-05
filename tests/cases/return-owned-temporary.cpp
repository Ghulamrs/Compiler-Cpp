// A class that owns memory, returned by value as a temporary.
//
// **This is the case for a silent wrong answer, and it is the one that stopped
// the standard library being written.** `return Buf(n);` builds a temporary and
// hands it back through the hidden pointer - as *bytes*. The temporary was on
// the pending list to be destroyed at the end of the full expression, which is
// that same `return` statement, so it was destroyed before the caller copied
// out of it: the caller received a shallow copy of an object whose buffer had
// been freed, and read freed memory.
//
// What made it invisible is what makes it worth a case. The length was right,
// because a `size_t` member survives its object's destruction untouched; only
// the *pointer* had been freed. So the answer was a string of the correct
// length full of whatever the allocator left behind - which reads as a
// formatting bug, not a lifetime bug. `substr` returning the right size and an
// empty string is exactly how it was found.
//
// The instrument is the poison: the destructor fills the buffer with '!' before
// freeing it, so a shallow copy of a destroyed object reads back as poison
// rather than as whatever the allocator happened to leave. That is what this
// case asserts, and it is what the fix made right.
//
// **`live` is counted and deliberately not asserted**, and the reason is worth
// recording. A balance - every constructor up, every destructor down, zero at
// the end - is the instrument this tree has been told twice it needs, and it is
// elision-invariant, because whatever is elided loses a construction and a
// destruction together. cxx1 does not balance here: the temporary handed back
// through the hidden pointer is not destroyed by the caller, so this program
// leaks three objects where clang leaks none. That is a separate open finding,
// it is a leak and not a wrong answer, and asserting the balance today would
// make this case red for a fault it was not written to catch. **When that
// finding closes, the counter goes in the printf and this paragraph goes.**

extern "C" int printf(const char *, ...);
extern "C" void *malloc(unsigned long);
extern "C" void free(void *);

int live = 0;

class Buf {
public:
    Buf(int n) : n_(n) {
        p_ = (char *)malloc(16);
        for (int i = 0; i < 15; i++) p_[i] = 'a' + (char)(n % 26);
        p_[15] = 0;
        live++;
    }
    Buf(const Buf &o) : n_(o.n_) {
        p_ = (char *)malloc(16);
        for (int i = 0; i < 16; i++) p_[i] = o.p_[i];
        live++;
    }
    Buf &operator=(const Buf &o) {
        if (this != &o) {
            n_ = o.n_;
            for (int i = 0; i < 16; i++) p_[i] = o.p_[i];
        }
        return *this;
    }
    ~Buf() {
        for (int i = 0; i < 15; i++) p_[i] = '!';   // poison, as free() would
        p_[15] = 0;
        free(p_);
        live--;
    }
    int size(void) const { return n_; }
    char first(void) const { return p_[0]; }
    // The shape that found it: a temporary of this class, built inside one of
    // its own const members, returned by value.
    Buf grown(void) const { return Buf(n_ + 1); }
private:
    char *p_;
    int n_;
};

static Buf freeGrown(const Buf &b) { return Buf(b.size() + 10); }

int main(void) {
    Buf a(1);
    printf("%d %c\n", a.size(), a.first());

    Buf b = a.grown();                    // member returning a temporary
    printf("%d %c\n", b.size(), b.first());

    Buf c = freeGrown(a);                 // and a non-member
    printf("%d %c\n", c.size(), c.first());

    Buf d(0);
    d = a.grown();                        // through assignment, not construction
    printf("%d %c\n", d.size(), d.first());

    printf("%d %c\n", a.grown().size(), a.grown().first());   // used and discarded
    // Read so that `live` is not an unused variable in either compiler; the
    // value is not asserted, for the reason at the top of this file.
    printf("%d\n", live > 0);
    return 0;
}
