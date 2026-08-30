// A unary operator, which the mangler can name and no expression can reach.
//
// The refusal is worth as much as the feature. `operator-` with a parameter
// is subtraction and works; with none it is negation and there is no path
// from `-v` to it, so the arity is what is being refused rather than the
// name. Accepting it would leave a function in the object file, under the
// name clang gives it, that nothing in the language can call.
struct V {
    int x;
    V operator-() const;
};

int main(void) { return 0; }
