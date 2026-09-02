// `new int[4](5)` is ill-formed - [expr.new]/17 allows only the empty pair
// after an array new-type-id - and clang refuses it too. Refused by name,
// and the name says what the empty pair would have meant.
int main() {
    int *a = new int[4](5);
    return a[0];
}
