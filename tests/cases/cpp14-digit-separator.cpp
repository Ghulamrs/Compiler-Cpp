// A `'` between digits is C++14's separator. Here the same character opens a
// character constant, so the diagnostic without this rule was an unterminated
// literal further down the line.
int main() { int n = 1'000; return n - 1000; }
