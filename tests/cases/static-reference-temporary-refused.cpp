// A reference at file scope bound to a temporary. [class.temporary]/5 gives
// the temporary the reference's lifetime, which here is the program's - so it
// would need static storage of its own. Refused by name; a named object binds.
const int &r = 5;
int main() { return r - 5; }
