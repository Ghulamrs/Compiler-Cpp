// The other side of value-init-empty-braces.cpp. `{}` is value-initialisation
// and is read; braces with a value in them are list-initialisation, which this
// compiler does not do - and for an aggregate `= {...}` says the same thing,
// so the refusal names it. What is pinned here is that the empty pair is told
// from the rest at the point the braces are seen, rather than by a parse error
// two tokens later saying `expected ';'`.
struct P { int a; int b; };
int main() { P p{1, 2}; return p.a; }
