// `operator int()` - a conversion function. Every operator this compiler can
// overload is punctuation; this one names a type, which is why it is caught
// where the operator's spelling is read rather than later.
struct V {
    int x;
    operator int() const;
};

int main(void) { return 0; }
