// Befriending one member function of another class. `Other::look` has to be
// found and named before Box is complete, which is a lookup this compiler
// does not do from here. Refused by name.
class Other {
public:
    int look(int n);
};

class Box {
    friend int Other::look(int n);
    int x;
};

int main(void) { return 0; }
