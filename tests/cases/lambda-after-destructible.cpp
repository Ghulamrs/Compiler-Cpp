// **A replayed body does not inherit the enclosing function's live objects.**
// A lambda's operator() is parsed by replaying its tokens through the ordinary
// member-definition path, and `alive_` - the objects a `return` still owes
// destructors to - belongs to the function being *parsed*, not to the one the
// replay interrupted. Left in place, the lambda's `return` emitted the
// enclosing function's destructors against its own frame: `h`'s ~Held ran
// inside the lambda with `this` pointing at the lambda's parameter, so the
// destructor read a member out of the lambda's arguments. With a destructor
// that only reads, that was a silent wrong answer; with one that frees, it was
// an abort. `pendingTemps_` is the same list for temporaries and goes with it.
//
// The case pins what the destructor is handed, not how many times anything is
// constructed: the object must be destroyed once, after the lambda has run, and
// with its own address.
extern "C" int printf(const char *, ...);

static int destroyed = 0;
static int sawWrongObject = 0;

struct Held {
    int tag;
    Held() : tag(0x5A) {}
    ~Held() {
        destroyed++;
        if (tag != 0x5A) sawWrongObject = 1;   // reached through a wrong `this`
    }
};

int main() {
    {
        Held h;
        auto cmp = [](int a, int b) { return a > b; };
        printf("cmp=%d\n", cmp(5, 2) ? 1 : 0);
        printf("alive tag=%d\n", h.tag == 0x5A ? 1 : 0);
    }
    printf("destroyed=%d wrongObject=%d\n", destroyed, sawWrongObject);
    return 0;
}
