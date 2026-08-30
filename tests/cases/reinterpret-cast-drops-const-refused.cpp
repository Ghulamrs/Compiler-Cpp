// `reinterpret_cast<int *>(p)` where p is a `const int *`, refused.
//
// **This is the line between the two casts.** reinterpret_cast changes what
// the bits are read as; const_cast changes const. Letting either do the
// other's job would make one of them redundant, and would let a program take a
// const off while looking like it was only changing the type - which is the
// thing the named casts exist to stop.
//
// Doing both means writing both, in either order.
int main(void) {
    const int n = 3;
    int *p = reinterpret_cast<int *>(&n);
    return *p;
}
