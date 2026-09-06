// **A variable template is C++14**, and this compiler is C++11 - the other
// half of alias-template-refused.cpp. Both begin `template <...>` and both
// reach an '=', so the pair is what holds the two messages apart.
template <class T> T zero = T();

int main() { return (int)zero<int>; }
