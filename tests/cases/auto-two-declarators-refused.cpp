// [dcl.spec.auto]/7: one `auto` declaration deduces one type, however many
// declarators are written after it. cxx1 deduced each on its own and took
// whatever each got, so `b` was an int holding 2 - the initialiser said 2.0
// and the object said 2, silently. What is compared is the deduced `auto`
// itself and not the declarator's type: `auto *p = &i, q = 5;` is legal, both
// deducing int, and refusals-that-must-not-fire.cpp holds that one.
extern "C" int printf(const char *, ...);
int main() { auto a = 1, b = 2.0; printf("%d\n", (int)(a + b)); return 0; }
