// A frame larger than a page needs a stack probe on x86_64-windows.
//
// Windows commits a thread's stack one page at a time, through a guard page
// just below what has been touched. A prologue that moves rsp past that page
// in one step and then writes at the bottom of its frame touches memory the
// OS never committed, and the write is an access violation - 0xC0000005, not
// a stack overflow, because the guard page was never hit. cl probes every
// frame of 4096 bytes or more with __chkstk (measured: a 4056-byte frame is a
// plain sub, a 4120-byte one calls __chkstk first), and cxx1 now does the
// same. Before it did, this program died on the Windows box every run, and
// matrix_main.cpp of the Vector Exercise - whose main has a 24 KB frame -
// died on most runs and not all, depending on where the guard page sat when
// main was entered.
//
// The frame here is 256 KB so that skipping the guard page is certain: no
// start-up path commits that much stack. It is written from its lowest
// address upwards, which is where the fault appears. The two Itanium targets
// need no probe - their kernels extend a stack on any access inside its
// limit - and there this is an ordinary program.
extern "C" int printf(const char *, ...);

static int fill(char *p, int n) {
    int sum = 0;
    for (int i = 0; i < n; i++) { p[i] = (char)(i * 7); sum += p[i]; }
    return sum;
}

int big() {
    char buf[262144];
    buf[0] = 1;            // the lowest address of the frame, first
    return fill(buf, 262144);
}

int page() {
    char buf[5000];        // a page and a bit: the smallest shape cl probes
    buf[0] = 2;
    return fill(buf, 5000);
}

int main() {
    printf("%d %d\n", big(), page());
    return 0;
}
