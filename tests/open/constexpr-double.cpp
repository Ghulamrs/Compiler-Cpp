// [expr.const]: a floating constant expression. `fold` has no floating lane,
// so a constexpr double, a static_assert over one and an array length cast
// from one are all refused as "not a constant expression".
extern "C" int printf(const char *, ...);
constexpr double d = 1.5;
static_assert(d > 1.0, "d");
int main() { printf("ok\n"); return 0; }
