// `reinterpret_cast<int>(p)` on a 64-bit target, refused.
//
// All three targets here have 8-byte pointers and 4-byte ints, so this would
// keep half of an address and quietly throw the rest away. Measured: clang
// refuses it rather than truncating. Note that `long` is not the portable
// answer either: it is 8 bytes on x86_64-linux and arm64-darwin and 4 on
// x86_64-windows, so named-casts.cpp goes through `long long`, which is 8 on
// all three. This case asks about `int`, which is too narrow everywhere.
int main(void) {
    int n = 3;
    int address = reinterpret_cast<int>(&n);
    return address != 0;
}
