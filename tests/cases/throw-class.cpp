// **A class-typed exception, thrown and caught.** Rung 6.2 emitted a
// `type_info` only for a fundamental type and said a class's "would have to be
// emitted here, and that is its own step". `dynamic_cast` had since made that
// step: `emitClassTypeInfo` builds `_ZTS` and `_ZTI` for any class this
// compiler can describe, and it needs no vtable - so a plain class is as
// throwable as a polymorphic one, and the refusal was all that stood there.
//
// **Catching by a base works because the object says so, not because the
// compiler does.** `__si_class_type_info` carries the base's own `_ZTI` as a
// third word and the runtime walks it, which is the same chain a
// `dynamic_cast` reads.
//
// Every handler here catches by reference or by value and none returns from
// inside a handler, and no local with a destructor shares a function with a
// `try` - the two limits rung 6 still has.
extern "C" int printf(const char *, ...);

// **The constructors are defined out of line on purpose.** Defined inside the
// class they are inline, and on x86_64-linux clang then emits only the C2
// variant while cxx1 emits C1 and C2 - a difference about emission that the
// names suite would report as though it were about mangling. CLAUDE.md
// records it; writing them out here keeps this case about exceptions.
struct Base { int v; Base(int n); };
struct Derived : Base { Derived(int n); };
struct Other { int w; Other(int n); };

Base::Base(int n) : v(n) {}
Derived::Derived(int n) : Base(n) {}
Other::Other(int n) : w(n) {}

int byBase()  { try { throw Derived(9); } catch (const Base &b) { return b.v; } return -1; }
int byValue() { try { throw Base(5); }    catch (Base b) { return b.v; } return -1; }

// The first handler does not match, so the second is reached - which is the
// selector being compared against this function's own type table in order.
int past()    { try { throw Other(3); }
                catch (const Base &) { return -2; }
                catch (const Other &o) { return o.w; } return -1; }

// Nothing matches, so `catch (...)` takes it.
int anything(){ try { throw Other(1); }
                catch (const Base &) { return -3; }
                catch (...) { return 8; } return -1; }

int main() {
    printf("%d %d %d %d\n", byBase(), byValue(), past(), anything());
    return 0;
}
