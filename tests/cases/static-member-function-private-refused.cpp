// A private static member function called from outside, refused.
//
// A static member obeys access like any other. The check is worth a case
// because this call does not go through the `.` or `->` path where the other
// members' access is checked - `S::f(1)` reaches the function by a route of
// its own, and the rule had to be written there a second time.
struct S {
private:
    static int f(int a) { return a; }
};

int main(void) {
    return S::f(1);
}
