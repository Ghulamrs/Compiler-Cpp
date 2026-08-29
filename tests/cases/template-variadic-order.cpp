// A pack takes every argument that is left, so a parameter written after one
// could never be given a value. [temp.param] says so; the message says why.
template <class... Ts, class T> int f(T x) { return (int)x; }
int main() { return f<int>(1); }
