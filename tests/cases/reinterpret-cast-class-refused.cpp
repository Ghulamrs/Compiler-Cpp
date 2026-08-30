// `reinterpret_cast<int>(s)` where s is a class, refused.
//
// reinterpret_cast moves between pointers, and between a pointer and an
// integer wide enough to hold an address. A class object is neither. What the
// program probably meant - read this object's bytes as an int - is written by
// casting its *address*, which is what reinterpret_cast is for, and is in
// named-casts.cpp.
struct Pair {
    int a;
    int b;
};

int main(void) {
    Pair p;
    p.a = 1;
    p.b = 2;
    return reinterpret_cast<int>(p);
}
