// class-too-large.cpp is the member list; this is the base list, which sums
// the same way and was checked one step too late. The member-list check sees
// the cursor the bases left, but each base's offset is an `int` and the
// cursor is derived from it - so two 2000000000-byte bases with no members of
// their own slipped past as a `.zerofill` of -294967295, and a third base
// wrapped its offset to a negative number before the member-list check could
// see the true sum at all. The base loop refuses where the sum is still a
// sum. The empty derived classes are the point: every member of Two is
// inherited, so nothing but the bases decides its size.
struct One { char a[2000000000]; };
struct A : One {};
struct B : One {};
struct Two : A, B {};
int main() { return 0; }
