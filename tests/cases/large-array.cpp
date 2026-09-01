// Every size in this compiler is a signed 32-bit count - offsets, frame slots
// and the emitted `.zero` alike - and an array that overflowed one was laid
// out anyway: `static int a[600000000]` came out `.zero -1894967296`, written
// into the assembly by the shipped -O2 binary without a word. UBSan named the
// multiply; what it needed was a refusal where the array is written.
static int a[600000000];
int main() { return a[0]; }
