// `::f()`, refused by name.
//
// The leading `::` says "the global scope and nothing nearer", and it earns
// its keep only where something nearer hides the global name - a member, a
// local, or a namespace member reached through a using-directive. Reaching
// past a hidden name is the whole feature, so it belongs with the lookup that
// does the hiding rather than with the token. Refused by name so that the
// reader is told that, rather than being pointed at a '::' and told an
// expression was expected.
int f(void) { return 3; }

int main(void) {
    return ::f();
}
