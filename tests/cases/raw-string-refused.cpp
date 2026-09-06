// A literal prefix is part of the literal, not a name in front of one. Without
// this the prefix lexes as an identifier and the reader is told it was never
// declared, which says nothing about the literal it belongs to.
int main() { const char *s = R"(ab)"; return s[0] == 'a' ? 0 : 1; }
