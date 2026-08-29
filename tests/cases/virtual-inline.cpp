// A virtual function written inside its own class.
//
// The body of a member defined in the class is held and replayed once the
// class is closed, so that it can see members declared after it. The replay
// re-reads the tokens through the ordinary out-of-line path - and out of line
// `virtual` is not written, because C++ puts the keyword on the declaration
// inside the class and nowhere else. So the replay has to begin at the return
// type. Beginning it at the keyword made every one of these a parse error,
// which is what this case is here to stop happening again.
//
// The destructor is written the same way for the same reason, and printing
// from it shows that the derived one was found through the base pointer.
extern "C" { int printf(const char *, ...); }
struct Animal {
    virtual int legs() { return 4; }
    virtual const char *says() { return "..."; }
    virtual ~Animal() { printf("~Animal\n"); }
};
struct Bird : Animal {
    virtual int legs() { return 2; }
    virtual const char *says() { return "tweet"; }
    virtual ~Bird() { printf("~Bird\n"); }
};
int main() {
    Animal a;
    Bird b;
    Animal *p = &a;
    printf("%d %s\n", p->legs(), p->says());
    p = &b;
    printf("%d %s\n", p->legs(), p->says());
    Animal *heap = new Bird;
    delete heap;
    return 0;
}
