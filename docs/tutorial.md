# HolyC++ Tutorial

A guided walkthrough for someone who has never written HolyC++ before.
Every snippet here is a complete, runnable program, save it to a file
ending in .hc++ and run it with:

    bootstrap/hcrun yourfile.hc++

## Your first program

    U0 Main() {
        Print("Hello, HolyC++ world\n");
    }

Every HolyC++ program needs exactly one Main function, that is where
execution begins. U0 is the HolyC void type, used whenever a function
returns nothing.

## Variables and types

    U0 Main() {
        I64 age = 30;
        F64 price = 19.99;
        Bool active = TRUE;
        U8* name = "Terry";

        Print("%s is %d years old, price %f, active %b\n", name, age, price, active);
    }

I64 is the everyday integer type. Narrower types exist too, and they
really do wrap at their declared width, exactly like real hardware
registers would:

    U0 Main() {
        U8 small = 250;
        small = small + 10;
        Print("%d\n", small); // 4, wrapped around 256
    }

See docs/language_spec.md for the full type list.

## Control flow

    U0 Main() {
        I64 n = 7;
        if (n % 2 == 0) Print("even\n");
        else Print("odd\n");

        I64 i;
        for (i = 0; i < 5; i++) Print("i = %d\n", i);

        I64 j = 0;
        while (j < 3) {
            Print("j = %d\n", j);
            j++;
        }
    }

switch, do while and goto with labels work too, see
docs/language_spec.md for the exact syntax of each.

## Functions

    I64 Square(I64 x) {
        return x * x;
    }

    U0 Main() {
        Print("%d\n", Square(6));
    }

Functions can call each other regardless of the order they are
declared in the file, the whole program is parsed before anything
runs.

## Arrays

    U0 Main() {
        I64 nums[5] = {10, 20, 30, 40, 50};
        I64 i;
        I64 total = 0;
        for (i = 0; i < Len(nums); i++) total += nums[i];
        Print("total = %d\n", total);
    }

Arrays are fixed size once declared. Len works on both arrays and
strings.

## Your first class

    class Counter {
        I64 value;

        U0 Counter() {
            this.value = 0;
        }

        U0 Increment() {
            this.value++;
        }

        I64 Value() {
            return this.value;
        }
    };

    U0 Main() {
        Counter c = Counter();
        c.Increment();
        c.Increment();
        c.Increment();
        Print("counter = %d\n", c.Value());
    }

A method whose name matches the class name is the constructor, it
runs automatically. Inside any method, this refers to the instance
the method was called on.

## Inheritance

    class Shape {
        U0 Describe() {
            Print("a shape\n");
        }
    };

    class Square : Shape {
        F64 side;

        U0 Square(F64 s) {
            this.side = s;
        }

        U0 Describe() {
            Print("a square with side %f\n", this.side);
        }
    };

    U0 Main() {
        Shape generic = Shape();
        Square sq = Square(4.0);
        generic.Describe();
        sq.Describe();
    }

Square inherits from Shape by naming it after a colon. Overriding
Describe replaces the base behavior entirely, and dispatch always
follows the object's real class, see docs/classes.md for the full
explanation and why that matters.

## Using the standard library

    U0 Main() {
        U8* name = "terry";
        Print("%s\n", ToUpper(name));
        Print("length = %d\n", StrLen(name));
        Print("sqrt(2) = %f\n", Sqrt(2.0));
        Print("max(3, 9) = %d\n", Max(3, 9));
    }

See docs/stdlib.md for every builtin function available.

## Splitting a program across files

    // helpers.hc++
    U0 Greet(U8* name) {
        Print("hello, %s\n", name);
    }

    // main.hc++
    import "helpers.hc++";

    U0 Main() {
        Greet("world");
    }

Run main.hc++ with hcrun directly, import resolution happens before
parsing even begins. Note that the self hosted compiler in selfhost/
does not resolve imports yet, use hcrun directly for multi file
programs until that lands.

## Where to go next

  docs/language_spec.md            every construct in the language
  docs/classes.md                     object orientation in depth
  docs/stdlib.md                       every builtin function
  docs/differences_from_holyc.md   what changed from stock HolyC, and why
  examples/                           more complete sample programs
