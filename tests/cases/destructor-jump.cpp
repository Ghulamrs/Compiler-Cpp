// A jump can leave a scope without falling off its end, and destructors run at
// the end. This break does not cross the object at all - `g` was built before
// the loop - and used to be refused all the same, while anything was alive.
// A break now destroys exactly what was built since its loop was entered,
// which here is nothing, and the program compiles and prints nothing.
// jump-out-destroys.cpp holds the jumps that do cross an object.
class Guard {
public:
    Guard();
    ~Guard();
private:
    int held;
};
Guard::Guard() { held = 1; }
Guard::~Guard() { held = 0; }

int main(void) {
    Guard g;
    while (1) {
        break;
    }
    return 0;
}
