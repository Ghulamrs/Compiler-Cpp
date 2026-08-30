// `bool b = nullptr;`, refused.
//
// [conv.bool] gives std::nullptr_t a conversion to bool for *direct*-
// initialization only, and this is copy-initialization - so it is not that the
// conversion is a poor one, it is that it is not offered here. The same
// conversion is offered where a *condition* asks for it, which is why
// `if (nullptr)` and `!nullptr` are written in nullptr.cpp and work.
//
// The distinction is load-bearing in overload resolution too: an argument is
// copy-initialized, so `f(bool)` is not a candidate for `nullptr` at all. Had
// bool been ranked beside the pointer conversion, `f(bool)` against
// `f(char *)` would have been reported ambiguous, which it is not - see
// tests/overload/nullptr-not-bool.cpp.
int main(void) {
    bool b = nullptr;
    return b ? 1 : 0;
}
