// `try` and `catch` - rung 6.3, and the first time a cxx1 program catches its
// own exception rather than needing another compiler on the other end.
//
// **Almost all of it is ordinary statements.** The selector the personality
// routine chose is compared against 1, 2, 3 - the order the handlers are
// written in, which is the order their types go into the table - and each arm
// is __cxa_begin_catch, a copy into the caught variable, the handler's own
// body, and __cxa_end_catch. What nothing matches falls through to
// _Unwind_Resume, which is what "this frame does not want it after all"
// means, and is what lets `inner` below hand a double to `main`.
//
// The backend is told three things and no more: a label before the body and
// one after it, a label at the pad where the runtime arrives with two values
// in registers, and the type_info symbols in order.
//
// **Every call in a function has to be in the call-site table**, not only the
// ones inside a try - a return address the table does not mention is a
// program libc++abi stops. That is why the ranges between and around the try
// blocks are written out with no landing pad.
extern "C" { int printf(const char *, ...); }

void risky(int n) {
    if (n == 1) throw 7;
    if (n == 2) throw 2.5;
    if (n == 3) throw 'x';
}

// A try whose handlers do not match has to let the exception carry on.
int inner(int n) {
    try {
        risky(n);
        return 0;
    } catch (int e) {
        printf("inner int %d\n", e);
        return 1;
    }
}

int main() {
    for (int i = 0; i <= 3; i++) {
        try {
            risky(i);
            printf("%d: nothing thrown\n", i);
        } catch (int e) {
            printf("%d: int %d\n", i, e);
        } catch (double d) {
            printf("%d: double %.1f\n", i, d);
        } catch (...) {
            printf("%d: something else\n", i);
        }
    }

    printf("%d\n", inner(1));
    try {
        inner(2);
    } catch (double d) {
        printf("through inner: %.1f\n", d);
    }
    printf("done\n");
    return 0;
}
