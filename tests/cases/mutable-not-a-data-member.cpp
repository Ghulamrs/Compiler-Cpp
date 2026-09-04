// [dcl.stc]/9 names what `mutable` may not be applied to, and each of the four
// would be a contradiction rather than a gap: a static member is not part of
// any object, a const one is what the keyword exists to undo, a reference
// cannot be rebound whatever is said about it, and a function is not a member
// that is written. clang refuses all four.
struct S {
    int z;
    mutable int &r;
};
