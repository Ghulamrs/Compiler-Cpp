// A pointer may gain const on the way in and may never lose it.
int main(void) {
    char buffer[4];
    const char *reading = buffer;
    char *writing = reading;
    return *writing;
}
