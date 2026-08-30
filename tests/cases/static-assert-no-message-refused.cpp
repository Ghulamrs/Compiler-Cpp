// `static_assert(cond);` with no message, refused by name.
//
// C++17 made the message optional and C++11 did not, and this compiler targets
// C++11. Accepting it would make a file that builds here stop building on the
// compiler it was written for - the same reason `namespace N::M {}` is refused
// beside it. Note that clang accepts this under `-std=c++11` as an extension
// and only says so under `-pedantic-errors`, which is why this was measured
// rather than read off a default build.
int main(void) {
    static_assert(sizeof(int) == 4);
    return 0;
}
