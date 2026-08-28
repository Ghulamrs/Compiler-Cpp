// Destructors and RAII - the last step of rung 3.
//
// The trace is the whole point: the order of these lines is the language's
// promise. Objects are destroyed last-first, an inner scope's objects go at
// its closing brace, and a `return` runs every destructor the function still
// owes AFTER computing the value it returns - which is why "returned 99" comes
// last rather than between "close 3" and "close 1".
//
// `delete p` runs the destructor and then frees, measured in that order from
// clang's own output.
extern "C" { int printf(const char *, ...); }

class Trace {
public:
    Trace(int id);
    ~Trace();
private:
    int id;
};
Trace::Trace(int n) { id = n; printf("open %d\n", id); }
Trace::~Trace() { printf("close %d\n", id); }

int scoped() {
    Trace a(1);
    {
        Trace b(2);
        printf("inner\n");
    }
    Trace c(3);
    return 99;
}

int main(void) {
    printf("returned %d\n", scoped());
    Trace *p = new Trace(4);
    delete p;
    return 0;
}
