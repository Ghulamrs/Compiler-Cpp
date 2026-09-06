// [except.handle]/1: a handler may not catch by rvalue reference. The
// exception object belongs to the runtime and outlives the handler, so there
// is nothing here to take apart - clang refuses this too.
int main() {
    try { throw 7; } catch (int &&e) { (void)e; }
    return 0;
}
