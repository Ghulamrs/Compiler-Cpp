// The second of the three doors the keyword lists are read at: a keyword where
// a *name* was wanted. `virtual` is implemented and cannot be a member's name,
// which is a different sentence from "not supported yet" and the one the reader
// needs. The message names what was wanted there, since expectIdent's callers
// each say it in their own words.
struct S { int virtual; };
int main() { return 0; }
