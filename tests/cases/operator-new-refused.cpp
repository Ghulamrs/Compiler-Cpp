// Replacing `operator new`. A new-expression here calls the platform's
// allocator by name - _Znwm on Itanium, ??2@YAPEAX_K@Z on Windows - so giving
// this class one would compile a function that the allocation never reaches.
struct V {
    int x;
};

void *operator new(unsigned long n);

int main(void) { return 0; }
