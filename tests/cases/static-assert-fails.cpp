// A static_assert that fails, which is the whole point of having one.
//
// The message is the program's own words, printed back. Nothing else about the
// file matters - it is otherwise a valid program, and it does not compile.
extern "C" int printf(const char *, ...);

struct Header {
    int tag;
    char flag;
};

// Padding: a char after an int is rounded up to the int's alignment, so this
// is 8 and not 5. Written the wrong way round on purpose.
static_assert(sizeof(Header) == 5, "a Header is meant to be five bytes");

int main(void) {
    printf("%d\n", (int) sizeof(Header));
    return 0;
}
