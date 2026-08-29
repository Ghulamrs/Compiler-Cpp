// The same refusal reached the other way. Here the type reads fine and it is
// the *name* that is a keyword, so this ends in expectIdent rather than in
// specifiers - and "expected a name" would be just as unhelpful.
struct Vec {
    int x;
    int operator+(const Vec &o);
};

int main() { return 0; }
