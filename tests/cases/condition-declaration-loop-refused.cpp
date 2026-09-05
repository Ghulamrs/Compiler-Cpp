// [stmt.iter]/2 builds the loop variable afresh on every turn and destroys it
// at the end of each one. For a scalar that is exactly an assignment written
// where the test is, which is what the `while` form does. A class would need
// its constructor and its destructor run per turn, with the construction
// written where the test is - so it is refused here and not in an `if`, where
// the object is built once and the ordinary declaration path does all of it.
struct Guard {
    int v;
    Guard(int n) : v(n) {}
    ~Guard() {}
    operator bool() const { return v != 0; }
};
Guard make(int n) { return Guard(n); }
int main() {
    while (Guard g = make(1)) return g.v;
    return 0;
}
