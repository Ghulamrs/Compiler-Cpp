// A keyword this compiler *has* is not "not supported yet" when it turns up
// somewhere it may not stand. `pending[]` named seven keywords that had since
// been implemented, so `int x = template;` was answered "'template' is not
// supported yet" - a claim about the compiler where the truth was about the
// place. The two answers are two different pieces of news for the reader: one
// says wait for a rung, the other says move the word.
//
// This is the expression door. keyword-as-a-name.cpp is the second of the
// three; the third, where a type was wanted, is the same list read there.
int main() { int x = template; return x; }
