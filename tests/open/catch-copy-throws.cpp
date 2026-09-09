// [except.handle]/3: if copy-initialising the exception declaration exits by
// throwing, std::terminate is called - the handler is not entered and the new
// exception does not propagate. clang emits __clang_call_terminate as the
// cleanup around that copy; cxx1 has no region there, so the exception leaves
// and the enclosing handler catches it, printing what clang never reaches.
//
// The recorded answer is clang's stdout, which is empty: it aborts before the
// first printf. cxx1 prints `outer(9) done`.
extern "C" int printf(const char *, ...);
struct E { int v; E(int n) : v(n) {} E(const E &o) : v(o.v) { throw 9; } ~E() {} };
int main() {
    try { try { throw E(1); } catch (E e) { printf("body "); } }
    catch (int k) { printf("outer(%d) ", k); }
    printf("done\n");
    return 0;
}
