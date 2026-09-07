// Two `try`s in one function catching the same type - and two catching
// everything - which is the shape that measures the type table being shared.
//
// **A selector is an index into the function's type table, and the parser
// predicts it.** The backend writes the index into the action record; the
// parser has already written `sel == n` into the handler chain. One entry per
// distinct type_info is what clang writes, so both sides here have to reuse
// the first entry's index for the second `catch (int)` - and if one side does
// and the other does not, the second handler compares against a number the
// runtime never returns, falls through to _Unwind_Resume, and the pad resumes
// into itself. That was measured as a hang, not a wrong answer: a backend
// that shared the entry while the parser still counted 1, 2 looped forever.
//
// `catch (...)` is the empty entry, and it is shared the same way - clang
// emits one `.long 0` for the two below - though nothing compares against it.
extern "C" int printf(const char *, ...);

void boom() { throw 7; }

int main() {
    try { boom(); } catch (int e) { printf("first %d ", e); }
    try { boom(); } catch (int e) { printf("second %d ", e); }
    try { boom(); } catch (...) { printf("any "); }
    try { boom(); } catch (...) { printf("again "); }
    printf("| end\n");
    return 0;
}
