// `[&]` and `[=]` capture whatever the body turns out to name, which is a
// second pass over the body this parser does not make. Refused separately from
// a named capture because the reason is different and the reader wrote
// something different.
int main(void) {
    int k = 1;
    auto f = [=]() { return k; };
    return f();
}
