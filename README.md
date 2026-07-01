# HolyC++

HolyC++ is an object oriented extension of HolyC, the language Terry
A. Davis wrote for TempleOS. It keeps everything that makes HolyC feel
like HolyC (U8 through I64 as real fixed width machine words, TRUE,
FALSE and NULL as literal keywords, Print with a C style format
string) and adds what stock HolyC never had: methods that belong to a
class, an implicit this inside them, constructors, and single
inheritance with real virtual dispatch.

Full details live under docs/, start with
docs/language_spec.md for the language itself and
docs/differences_from_holyc.md for exactly what changed from stock
HolyC and why.

## Where this actually runs

Real HolyC only exists inside TempleOS, where the operating system
itself is the compiler and the JIT, running at ring 0. There is no
such environment on an ordinary Linux machine, so this project ships
its own small bootstrap runtime, bootstrap/hcrun.c, a lexer, parser
and tree walking evaluator for HolyC++ written once in plain C. It
plays the part of the missing layer, nothing more.

Everything on top of that bootstrap, the real HolyC++ toolchain
(lexer, parser and evaluator), lives under selfhost/ and is written in
HolyC++ itself. hcrun executes it, and from that point on hcrun never
has to be touched again to add a language feature, that work happens
in selfhost/ instead. Running any program through the self hosted
compiler produces exactly the same output as running it through hcrun
directly, the test suite checks this for every example.

## Building

Requires only a C compiler and make.

    make build

This produces bootstrap/hcrun.

## Running a program

Directly through the bootstrap runtime:

    bootstrap/hcrun examples/hello.hc++

Or through the self hosted toolchain, passing the target program as
an extra argument:

    bootstrap/hcrun selfhost/compiler.hc++ examples/hello.hc++

The self hosted compiler does not yet resolve import statements
inside the program it is running, use hcrun directly for programs
that span multiple files until that lands, see
docs/differences_from_holyc.md.

## Testing

    make test

This builds hcrun, then for every program under examples/ checks that
hcrun's output matches the recorded golden output in tests/golden/,
and that the self hosted compiler produces the exact same output too.

## Layout

    bootstrap/hcrun.c       the C bootstrap runtime
    selfhost/                the real HolyC++ toolchain, written in HolyC++
        common.hc++            shared token, AST and program data structures
        lexer.hc++              tokenizer
        parser.hc++              recursive descent parser
        interpreter.hc++         tree walking evaluator and stdlib forwarding
        compiler.hc++             entry point, ties the above together
    examples/                sample HolyC++ programs
    tests/                     the test runner and its golden output files
    docs/                      the full documentation set

## A short example

    class Animal {
        U8* name;

        U0 Animal(U8* n) {
            this.name = n;
        }

        U0 Speak() {
            Print("%s makes a sound\n", this.name);
        }
    };

    class Dog : Animal {
        U0 Speak() {
            Print("%s says Woof\n", this.name);
        }
    };

    U0 Main() {
        Animal a = Dog("Rex");
        a.Speak();
    }

Running this prints "Rex says Woof", not "Rex makes a sound", even
though a is declared as a plain Animal: method dispatch always follows
the object's real class. See docs/classes.md.

## Documentation

    docs/language_spec.md            the full language reference
    docs/grammar.md                    the formal EBNF grammar
    docs/classes.md                     object orientation in depth
    docs/stdlib.md                       every builtin function
    docs/tutorial.md                     a guided walkthrough
    docs/differences_from_holyc.md   exactly what changed from stock HolyC

## License

See LICENSE.
