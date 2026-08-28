// A jump can leave a scope without falling off its end, and destructors run at
// the end. Rather than skip one silently - which loses whatever the object was
// holding - the jump is refused while anything is alive.
//
// Conservative on purpose: this break would not actually cross the object.
// Making the rule precise means each jump knowing which scopes it leaves, and
// that is a change to how jumps are built rather than an addition to it.
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
