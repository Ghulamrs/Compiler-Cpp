// Binding a writable reference to a const object would be the const-drop of
// const-drop.cpp, arrived at by another road.
int main(void) {
    const int fixed = 3;
    int &n = fixed;
    n = 4;
    return n;
}
