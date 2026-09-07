// <stdexcept>, and the hierarchy every C++ program's error handling is built on.
//
// **The header is three lines per class and that is the point.** Each holds a
// `std::string` and `what()` returns its buffer - [exception]/8 lets it, the
// pointer staying valid while the object does - so the string owns the bytes,
// its copy constructor copies them and its destructor frees them, and none of
// the nine classes writes any of the three.
//
// **What it leans on, all of which landed the same day:** an exception object
// copy-constructed from the thrown operand rather than block-copied, or the
// buffer would be shared with a temporary and freed under the handler; the
// operand's temporaries destroyed after that copy; `__cxa_throw` told the
// class's destructor, or the string leaks once per throw; and the implicit
// destructor of an *intermediate* class emitted, without which
// `domain_error : logic_error : exception` did not link at all.
//
// `std::exception` is in `<exception>` and not here, which is where the
// standard puts it - measured against libc++, where `<exception>` alone does
// not declare `runtime_error` and `<stdexcept>` alone does declare `exception`.
//
// The case catches by *base* at every depth, because that is what a real
// handler does and it is the whole reason the derived classes exist: they are
// distinguishable by type in a `catch` and by nothing else.

#include <stdexcept>
#include <string>
#include <cstdio>

// A user class under the standard's, which is what a program actually writes -
// and three deep, so it needs the intermediate's implicit destructor.
struct CustomError : public std::runtime_error {
    CustomError(const std::string &m);
};
CustomError::CustomError(const std::string &m) : std::runtime_error(m) {}

void raise(int which) {
    if (which == 0) throw std::logic_error("logic " + std::to_string(which));
    if (which == 1) throw std::runtime_error("runtime " + std::to_string(which));
    if (which == 2) throw std::out_of_range("range " + std::to_string(which));
    if (which == 3) throw std::invalid_argument("bad arg " + std::to_string(which));
    throw CustomError("custom " + std::to_string(which));
}

int main(void) {
    // Caught by the exact type.
    try { raise(0); } catch (const std::logic_error &e) { printf("exact: %s\n", e.what()); }

    // Caught one level up.
    try { raise(2); } catch (const std::logic_error &e) { printf("one up: %s\n", e.what()); }

    // Caught at the top, from each side of the hierarchy.
    try { raise(1); } catch (const std::exception &e) { printf("top: %s\n", e.what()); }
    try { raise(3); } catch (const std::exception &e) { printf("top: %s\n", e.what()); }

    // A user class three deep, caught at the top.
    try { raise(4); } catch (const std::exception &e) { printf("user: %s\n", e.what()); }

    // The order of handlers is the program's, and the first that matches wins -
    // so a derived one has to come before its base or it never runs.
    try { raise(4); }
    catch (const CustomError &e)     { printf("picked CustomError: %s\n", e.what()); }
    catch (const std::exception &e)  { printf("picked exception: %s\n", e.what()); }
    return 0;
}
