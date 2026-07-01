# Object Orientation In HolyC++

This page goes deeper than the short summary in
docs/language_spec.md, walking through what a class actually is at
runtime, how construction works, how inheritance and dispatch are
implemented, and where the design deliberately diverges from other
object oriented languages.

## A class is a named set of fields and methods

    class Point {
        I64 x;
        I64 y;

        U0 Point(I64 ax, I64 ay) {
            this.x = ax;
            this.y = ay;
        }

        U0 Show() {
            Print("Point(%d, %d)\n", this.x, this.y);
        }
    };

Fields are declared exactly like variables, including support for
fixed size array fields:

    class Bag {
        I64 items[8];
        I64 count;
    };

Methods are declared exactly like functions, just written inside the
class body. Every method implicitly receives the instance it was
called on, available as this.

## Construction

A method whose name matches the class name is the constructor. It
runs automatically, once, whenever the class is instantiated:

    Point p = Point(3, 4);

If a class declares no constructor, every field simply takes its
default value: zero for numeric types, an empty string for Str and
U8*, FALSE for Bool, and NULL for a class typed field.

new is accepted as an optional prefix that means exactly the same
thing:

    Point* p = new Point(3, 4);

Both p variables above hold the same kind of reference. There is no
separate stack allocated form of a class instance in HolyC++, see
docs/differences_from_holyc.md for the reasoning.

## Every class instance is a reference

Assigning a class instance to a variable, passing it as an argument,
or storing it in a field never copies the underlying object, it
copies a reference to it, the same way an object reference behaves in
Java or a Python object behaves. This matters in two ways:

Mutating a field through one reference is visible through every other
reference to the same object:

    U0 Rename(Animal a, U8* newName) {
        a.name = newName;
    }

    Animal x = Animal("Old Name");
    Rename(x, "New Name");
    x.Speak(); // sees "New Name", the call above mutated the shared object

Comparing two class typed values with == checks whether they refer to
the same object, not whether their fields happen to match:

    Animal a = Animal("Rex");
    Animal b = Animal("Rex");
    Animal c = a;
    a == b; // FALSE, two different objects
    a == c; // TRUE, the same object

## Inheritance is single, and dispatch is virtual

    class Shape {
        U0 Describe() {
            Print("a shape\n");
        }
    };

    class Circle : Shape {
        F64 radius;

        U0 Circle(F64 r) {
            this.radius = r;
        }

        U0 Describe() {
            Print("a circle with radius %f\n", this.radius);
        }
    };

Circle inherits every field and method Shape declares. A field or
method Circle does not redeclare works exactly as if it were written
directly on Circle. A method Circle does redeclare overrides the base
version completely, there is no way to call the base version from the
override in the current language, that is a known limitation, not a
deliberate design choice.

Dispatch is virtual: whichever method actually runs is chosen by
looking at the object's real, dynamic class, walking up the
inheritance chain from there until a matching method name is found.
The declared type of the variable holding the reference never affects
which method runs:

    U0 Announce(Shape s) {
        s.Describe();
    }

    Announce(Shape());        // prints "a shape"
    Announce(Circle(2.0));    // prints "a circle with radius 2"

A class can only extend one base class. There is no interface or trait
mechanism, and there is no way to call a method on a specific ancestor
class, only on the object's actual runtime type.

## Fields are resolved by name, not by declared type

Calling a.Field or a.Method() looks the field or method up by name on
a's actual object at the moment of the call. Nothing about HolyC++
enforces that the variable's declared type actually matches the
object it holds, the same way stock HolyC never enforced strict
static typing either. This is a deliberate consequence of keeping the
language close to HolyC's own loose, storage focused type system, see
docs/differences_from_holyc.md.

## What HolyC++ classes do not have

  * Multiple inheritance or interfaces, only a single base class.
  * Access modifiers such as public or private, every field and
    method is reachable from anywhere, matching stock HolyC's total
    absence of visibility control.
  * Operator overloading, the built in operators only work on the
    built in numeric, string and boolean types.
  * Generics or templates, a class is always a fixed, concrete type.
  * A way to call an overridden base method from inside an override.

These are honest gaps, not hidden behavior, and are tracked as
directions the language could grow into rather than things it quietly
pretends to support.
