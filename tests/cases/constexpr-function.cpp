// A `constexpr` function is refused by name. Evaluating a call while
// compiling needs an interpreter over the AST - a second execution model
// beside the three backends - and accepting the keyword without one would
// compile and run correctly while quietly failing to be constant in the one
// place it was written for.
constexpr int square(int x) { return x * x; }
int main() { return square(4); }
