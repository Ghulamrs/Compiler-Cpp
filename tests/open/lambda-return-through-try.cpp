// A lambda whose only `return` is inside a `try` deduces `void`, so the return
// itself is then refused: "this function's return type is 'void' and this is
// 'int'". [expr.prim.lambda]/4 deduces from the return statements wherever
// they are; cxx1 scans for the first `return` at the body's own level, which
// a try/catch puts one level down. Writing `-> int` works.
extern "C" int printf(const char *, ...);
int main() {
    auto f = [](int k) { try { throw 4; } catch (int b) { return k + b; } };
    printf("%d\n", f(10));
    return 0;
}
