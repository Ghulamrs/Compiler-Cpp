// The other side: naming a private nested type from outside the class is still
// refused. The look-ahead that lets `VM::Value VM::pop()` through asks whether
// the declarator defines a member of VM, and here it does not - this is an
// ordinary object at file scope. clang refuses it too.
struct VM {
private:
    union Value { int n; };
public:
    Value pop();
};
VM::Value VM::pop() { Value v; v.n = 1; return v; }
VM::Value stray;
int main() { return 0; }
