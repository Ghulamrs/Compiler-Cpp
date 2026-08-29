// A pack of values rather than of types. Its own step and refused by name:
// binding a pack is binding it to a list of *types*, and everything that
// reads one - the parameter list it expands into, the arguments a call gets -
// is written in terms of those. A pack of values needs a second list beside
// it and a second expansion, which is more than a flag.
template <int... Ns> int howMany() { return (int)sizeof...(Ns); }
int main() { return howMany<1, 2>(); }
