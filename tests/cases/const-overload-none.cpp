// Where only a non-const member exists, a const object cannot reach it - and
// the message says that rather than "no function takes these arguments", which
// is what the ranking alone would have produced once the candidate stopped
// being viable.
class Counter {
public:
    Counter();
    int bump();
    int n;
};
Counter::Counter() { n = 0; }
int Counter::bump() { n = n + 1; return n; }

int main() {
    const Counter c;
    return c.bump();
}
