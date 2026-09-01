// No attribute is parsed yet. Named because C++11's own two and C++14's are
// spelled identically, and a reader who writes either is owed the version
// number rather than a complaint about a missing type.
[[noreturn]] void die();
int main() { return 0; }
