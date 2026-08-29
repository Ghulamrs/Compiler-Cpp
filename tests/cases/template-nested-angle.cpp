// `Box<Box<int>>` - the `>>` the lexer hands over as one token.
//
// The two `>` close two different argument lists, so the token has to split.
// It cannot split by inserting a second token: held bodies and templates
// record absolute token indices and an insert would move all of them. The
// first `>` is taken by leaving a marker at the index instead of advancing.
//
// This is a refusal case because 5.1 instantiates nothing - but which refusal
// is exactly the point. Without the split the inner list would swallow both
// `>` and the outer one would run to the end of the file looking for its own,
// and the message would be "this template argument list is never closed".
template <class T> struct Box { T slot; };
int main() {
    Box<Box<int>> b;
    return 0;
}
