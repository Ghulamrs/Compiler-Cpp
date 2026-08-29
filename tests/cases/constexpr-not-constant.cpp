// The demand `constexpr` adds over `const`: this initialiser is a call, so
// its value is not known while this is compiled. A plain `const int` here
// would be accepted and simply not be a constant expression afterwards.
int measure();
int main() {
    constexpr int n = measure();
    return n;
}
