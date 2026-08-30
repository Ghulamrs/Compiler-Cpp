// `int x = 5;` on the member itself - C++11's non-static data member
// initialiser.
//
// **It is kept as a place in the token stream**, the way a default argument is
// and for the same reason: it is evaluated once per *construction*, in a
// constructor that may not have been read yet, so one tree could not serve
// them all.
//
// **[class.base.init]/9 makes it a fallback and not an override**: a member the
// constructor named in its own list gets what the list says, and every other
// member gets what the class said. That is member by member and not all or
// nothing, which is what `S(int a) : x(a)` proves - x from a, y and g from the
// class.
//
// **A class with nothing but an initialiser still needs a constructor**, and
// that is where this could have gone silently wrong: the implicit default
// constructor is only declared when it has work to do, and an initialiser on a
// member is now one of the things that counts as work. Without it there is no
// function to put the store in and `Plain p;` leaves x holding whatever was
// there.
extern "C" { int printf(const char *, ...); }

int base = 40;

struct Plain {
    int x = 5;                 // no constructor at all
    int y = 7;
};

struct Written {
    int x = 1;
    int y = 2;
    double d = 0.5;
    int g = base + 2;          // an expression, not a literal
    Written();
    Written(int a);
};

Written::Written() { }
Written::Written(int a) : x(a) { }      // y, d and g still get theirs

struct Inner { int n = 3; };
struct Outer { Inner in; int k = 4; };  // through a member class

int main(void) {
    Plain p;
    Written w;
    Written v(9);
    Outer o;
    Plain many[2];             // the array path has to mark the ctor used
    printf("%d %d %d %d %d %d %d %d %d\n",
           p.x, p.y, w.x, w.y, v.x, v.y, v.g, o.in.n + o.k,
           many[0].x + many[1].x);
    return 0;
}
