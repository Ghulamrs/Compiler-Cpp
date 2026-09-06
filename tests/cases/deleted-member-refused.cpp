// The member-function door to the same refusal - see defaulted-deleted.
struct S { int f() = delete; };
int main() { (void)sizeof(S); return 0; }
