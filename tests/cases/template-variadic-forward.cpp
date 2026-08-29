// Expanding a pack into another template's argument list *in a pattern*,
// where the pack stands for itself rather than for a list of types.
//
// `Tuple<Ts...>` as a function parameter is read for deduction: the compiler
// has to work Ts out backwards from the argument's type. Splicing is not
// available there, because at that moment Ts has no members - it is one
// opaque parameter - so what would be spliced is the parameter itself.
//
// Expanding a pack into an argument list where the members *are* known works
// and is template-variadic-into-args.cpp. The two are the same syntax and
// different problems, which is why one landed and one did not.
template <class... Ts> struct Tuple { int n; };
template <class... Ts> int size(Tuple<Ts...> t) { t.n = 0; return sizeof...(Ts); }
int main() { Tuple<int> t; return size(t); }
