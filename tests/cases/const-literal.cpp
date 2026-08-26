// In C++11 a string literal is an array of const char - [lex.string]/8 - so
// this is ill-formed, where C and C++03 let it through.
int main(void) {
    char *greeting = "hello";
    greeting[0] = 'H';
    return greeting[0];
}
