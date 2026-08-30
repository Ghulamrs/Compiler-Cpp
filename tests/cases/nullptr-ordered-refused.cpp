// `nullptr < nullptr`, refused.
//
// [expr.rel] orders two pointers, and std::nullptr_t is not a pointer - clang
// refuses `p < nullptr` for the same reason. Equality is a separate rule,
// [expr.eq], which takes a null pointer constant on either side; that is
// written in nullptr.cpp and works. There is one value of the type, so there
// was never anything to order.
int main(void) {
    return nullptr < nullptr ? 1 : 2;
}
