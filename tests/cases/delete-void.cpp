// Deleting a void pointer is ill-formed: the operator would be handed an
// address with nothing to say how much was allocated or what is there.
extern "C" { void *malloc(unsigned long); }
int main(void) {
    void *p = malloc(4);
    delete p;
    return 0;
}
