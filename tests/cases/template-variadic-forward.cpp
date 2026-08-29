// Expanding a pack into *another* template's argument list, `Tuple<Ts...>`.
// Refused by name. Everything a pack expands into here is a list of things
// the parser is about to read one by one - parameters, or a call's arguments.
// A template argument list is read against a parameter list that decides what
// each argument means, and a pack in the middle of that changes how many
// there are before that reading starts.
template <class... Ts> struct Tuple { };
template <class... Ts> int size(Tuple<Ts...> t) { (void)t; return 1; }
int main() { Tuple<int> t; return size(t); }
