// A nested class is a member, and `private:` reaches it. The name is refused
// where it is written rather than at the first use of the object, because the
// line the reader has to change is the one that names the type.
class Outer {
    class Inner {
    public:
        int a;
    };
public:
    int b;
};

int main() {
    Outer::Inner x;
    x.a = 1;
    return x.a;
}
