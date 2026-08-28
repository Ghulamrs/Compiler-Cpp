// A const member function may be called on a const object and a non-const one
// may not - the whole point of writing const after the parameter list.
class Box {
public:
    int get() const;
    int set(int v);
private:
    int held;
};
int Box::get() const { return held; }
int Box::set(int v) { held = v; return held; }

int main(void) {
    Box b;
    const Box *p = &b;
    return p->set(1);
}
