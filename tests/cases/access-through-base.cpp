// [class.access.base]/5: a derived class may name a *protected* member of its
// base. cxx1 had one helper that knew this - insideAccessOf, with its
// derivesFrom clause - and seven access checks that did not use it, each
// written as `currentClass_ != cls` before the helper existed. Two of them
// refused perfectly ordinary code:
//
//   a protected static data member of a base, named from a derived member
//   a protected default constructor of a base, called by the constructor the
//   compiler writes for the derived class
//
// The second is the shape a class uses to say "derive from me, do not build me
// directly", and it could not be derived from at all. It needed a second
// helper rather than the first: the implicit special members are synthesised
// while a class is being *completed*, so `currentClass_` is not reliably the
// class they belong to, and accessibleFrom asks the question from a class
// named outright instead of from the one being parsed.
extern "C" int printf(const char *, ...);

class Base {
protected:
    static const int shared;
    int held;
    Base() : held(1) {}
    int twice() const { return held * 2; }
};
const int Base::shared = 9;

// No constructor of its own, so the one the compiler writes calls Base's
// protected default constructor - which is the whole point of writing it
// protected, and was refused.
struct Derived : Base {
    int viaName() const { return shared; }
    int viaQualified() const { return Base::shared; }
    int viaMember() const { return held; }
    int viaFunction() const { return twice(); }
};

// One more level down: protected reaches as far as the derivation does.
struct Deeper : Derived {
    int stillReaches() const { return shared + held; }
};

int main() {
    Derived d;
    Deeper e;
    printf("%d %d %d %d\n", d.viaName(), d.viaQualified(),
                            d.viaMember(), d.viaFunction());
    printf("%d\n", e.stillReaches());
    return 0;
}
