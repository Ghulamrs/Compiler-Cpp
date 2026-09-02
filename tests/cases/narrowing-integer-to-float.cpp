// An integer constant may go to a floating type only if it comes back as
// itself. 16777216 is 2^24 and survives a float; 16777217 rounds to it, so
// clang refuses this one and accepts the one below it by one. The check is
// the round trip and not the value's size, which is what the rule says.
int main() { float f = {16777217}; return (int)f; }
