// A name with C linkage carries one symbol, so it can hold one function. The
// second declaration is refused where it stands rather than at the link,
// where the report would name a duplicate symbol in a file nobody wrote.
extern "C" {
int twice(int n);
int twice(double x);
}

int main(void) { return twice(1); }
