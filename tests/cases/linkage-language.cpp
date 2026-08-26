// The standard fixes two spellings and leaves the rest to the implementation,
// so a compiler that does not know one has to say which it does know.
extern "Fortran" {
int compute(int n);
}
int main(void) { return compute(1); }
