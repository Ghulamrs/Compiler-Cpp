// Passing a class by value copies it, so the copy constructor has to be
// reachable from where the call is written. Refused at the call rather than
// inside the function that would have made the copy, because the call is the
// line the reader can change.
class Secret {
    Secret(const Secret &o);
public:
    Secret();
    int s;
};
Secret::Secret() { s = 1; }
Secret::Secret(const Secret &o) { s = o.s; }

int take(Secret x) { return x.s; }

int main() {
    Secret a;
    return take(a);
}
