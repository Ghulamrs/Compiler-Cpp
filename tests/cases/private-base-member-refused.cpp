// [class.access.base]/1: a member private in a base is inaccessible in a
// derived class, whichever of the three ways it is named. cxx1 accepted all
// three from inside the derived class, and the cause was in the layout rather
// than in the check: a base's data members are copied down into the derived
// class's list - that flattening *is* the layout - and they carried their
// access with them but no record of *whose* they were. So the check asked
// whether it was inside the class it was reached through, which it was.
//
// A Member records the class that declared it now, and the check asks about
// that one. There is no fallback to the class it was reached through: being
// inside Derived grants nothing over a private member of Base, and the
// protected case is already answered by insideAccessOf's derivesFrom clause.
class Base { int hidden; public: Base() : hidden(3) {} };

struct Derived : Base {
    int unqualified() const { return hidden; }
};

int main() { Derived d; return d.unqualified(); }
