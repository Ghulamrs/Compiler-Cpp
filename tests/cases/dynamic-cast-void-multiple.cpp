// `dynamic_cast<void *>(p)` where the pointer does *not* point at the front of
// the object - which is the shape the feature exists for.
//
// A `Second *` into a `Both` points past the object's own front, so the offset
// the vtable carries is **negative** and the cast has to move backwards to land
// on the complete object. `second base is offset: 1` is what says the case is
// really testing that: if the class were laid out with one vptr the line would
// read 0 and everything after it would prove nothing.
//
// Separate from `dynamic-cast-void.cpp` because the Microsoft ABI refuses this
// layout for a reason older than this feature and unrelated to it - see the
// `.notarget`. The portable half of the feature is checked there, on all three.

extern "C" int printf(const char *, ...);

struct First  { virtual ~First(); int f; };
struct Second { virtual ~Second(); int s; };
struct Both : First, Second { int b; };

First::~First() {}
Second::~Second() {}

int main(void) {
    Both both;

    Second *second = &both;
    printf("second base is offset: %d\n", (void *)second != (void *)&both);
    printf("both through second: %d\n",
           dynamic_cast<void *>(second) == (void *)&both);

    First *first = &both;
    printf("both through first: %d\n",
           dynamic_cast<void *>(first) == (void *)&both);

    Second *none = 0;
    printf("null answers null: %d\n", dynamic_cast<void *>(none) == (void *)0);
    return 0;
}
