// **An alias template is C++11, and was refused as C++14.** [temp.alias]:
// `template <class T> using X = ...;` is a C++11 alias template, and the token
// scan that recognises a C++14 variable template saw an '=' with no '(' in
// front of it and said so - telling the reader that a conforming C++11 program
// belongs to a later standard, which is worse than not naming the feature.
//
// The discriminator is the keyword: an alias template writes `using`
// immediately after the parameter list, and nothing else does.
// variable-template-refused.cpp is the other half, and the pair is what stops
// one message from claiming the other's ground.
template <class T> using Ptr = T*;

int main() {
    Ptr<int> p = 0;
    return p == 0 ? 0 : 1;
}
