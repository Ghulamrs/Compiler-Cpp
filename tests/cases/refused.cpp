// The refuse-by-name rule: a keyword this compiler recognises but has not
// implemented is refused where it stands, not twenty tokens later where the
// parse finally falls over.
//
// **This case names whichever keyword is still pending, and that changes as
// the ladder advances** - it said 'class' until rung 3 implemented it, which
// is how a green suite told the truth about the day before. Pick a keyword
// from a rung that is still ahead; typeid belongs to none of the ones written
// so far.
int main(void) { int x = typeid; return x; }
