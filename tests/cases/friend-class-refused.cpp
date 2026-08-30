// `friend class X;` grants every member function of another class access at
// once, where a friend function grants one named function. Refused by name.
class Box {
    friend class Peeker;
    int x;
};

int main(void) { return 0; }
