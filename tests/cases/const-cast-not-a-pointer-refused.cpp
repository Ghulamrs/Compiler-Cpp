// `const_cast<int>(n)`, refused.
//
// const_cast is written on a pointer or a reference, because those are the
// things that carry a const somebody might want off. A value is copied when it
// is read, and a copy is a new object whose own constness nobody asked about -
// so there was never anything in the way. Writing the cast here is a sign the
// reader has misunderstood which const is the problem, which is why it is an
// error rather than a permitted no-op.
int main(void) {
    const int n = 3;
    return const_cast<int>(n);
}
