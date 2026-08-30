// `[this]` where there is no `this` to capture. Refused by name rather than
// left to fail inside the body, where the reader would be told about a member
// that has no object rather than about the capture list they wrote.
int main(void) {
    auto f = [this]() { return 1; };
    return f();
}
