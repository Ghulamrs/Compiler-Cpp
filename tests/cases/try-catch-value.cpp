// `try` and `catch` where the handler falls off its end, which is the shape
// both ABIs manage today.
//
// **On Windows a handler is a function of its own** - a funclet the runtime
// calls once it has chosen it from the tables - so leaving one early is not a
// jump but a *return* of the address to carry on at, in the register a return
// value would travel in. `return` inside a `catch` is refused by name on that
// target for exactly that reason, and this case does not use one: what a
// handler wants to say, it says by writing to a variable of the enclosing
// function, which works the same everywhere.
extern "C" int printf(const char *, ...);

void risky(int n) {
    if (n == 1) throw 7;
    if (n == 2) throw 'x';
}

int main() {
    for (int i = 0; i <= 2; i++) {
        int got = -1;
        char letter = '-';
        try {
            risky(i);
            got = 0;
        } catch (int e) {
            got = e;
        } catch (char c) {
            letter = c;
        }
        printf("%d: got=%d letter=%c\n", i, got, letter);
    }

    int fell = 0;
    try {
        risky(1);
        fell = 1;
    } catch (int e) {
        fell = e * 2;
    }
    printf("fell=%d\n", fell);
    return 0;
}
