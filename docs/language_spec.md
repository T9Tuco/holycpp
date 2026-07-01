# HolyC++ Language Specification

## Overview

HolyC++ is an object oriented extension of HolyC, the language Terry
A. Davis designed for TempleOS. Stock HolyC already looks a lot like
C, but it treats every integer as a raw fixed width machine word, it
skips header files entirely, and its idea of a "class" is really just
a struct with an inheritance chain, no methods attached.

HolyC++ keeps everything that makes HolyC feel like HolyC (the type
names, the manual and honest approach to memory widths, TRUE, FALSE
and NULL as literal keywords, Print with a format string) and adds
the object oriented pieces stock HolyC never had: methods that belong
to a class, an implicit this inside them, constructors, and single
inheritance with real virtual dispatch, meaning the method that
actually runs is chosen by the object's real class at the moment of
the call, not by the type of the variable holding it.

A full comparison against stock HolyC lives in
docs/differences_from_holyc.md. A formal grammar lives in
docs/grammar.md. This document explains what each construct means.

## Where HolyC++ actually runs

Real HolyC only exists inside TempleOS, where the operating system
itself compiles and runs it at ring 0. There is no such environment on
an ordinary Linux machine, so HolyC++ ships with its own bootstrap
runtime, bootstrap/hcrun.c, a small program written in plain C that
plays the part of that missing layer. Once hcrun exists, the real
HolyC++ toolchain (its lexer, parser and evaluator) is written in
HolyC++ itself, inside selfhost/, and hcrun simply executes it. See
README.md for the exact build and run commands.

## Source files

A HolyC++ source file conventionally uses the extension .hc++. A file
can contain, at the top level, only:

  * class declarations
  * function declarations
  * global variable declarations
  * import statements

Every program needs exactly one entry point function:

  U0 Main() {
      ...
  }

hcrun looks up Main by name once every class, function and global
variable has been registered, and calls it with no arguments. There
is no implicit top to bottom script execution the way there is inside
the interactive TempleOS shell, a program is a set of declarations
plus one clearly marked starting point.

## Comments

Two forms, both borrowed unchanged from HolyC:

  // a line comment, runs to the end of the line
  /* a block comment, can span multiple lines */

## Types

HolyC++ keeps the full HolyC numeric type family. Every integer type
is a fixed width machine word. Assigning a value that does not fit
truncates it exactly the way it would on real hardware, this is not a
static type checker, it is a description of storage width.

  U0    void, used only as a function return type
  U8    unsigned  8 bit integer, wraps at 256
  U16   unsigned 16 bit integer, wraps at 65536
  U32   unsigned 32 bit integer
  U64   unsigned 64 bit integer
  I8    signed  8 bit integer
  I16   signed 16 bit integer
  I32   signed 32 bit integer
  I64   signed 64 bit integer, the default sized integer, used almost
        everywhere a plain number is needed
  F64   double precision floating point
  Bool  TRUE or FALSE
  Str   a string value (an alias for U8*, see below)
  Auto  infer the type from the initializer, no width coercion applied

A class name is also a valid type. Writing ClassName or ClassName*
both name the same type in HolyC++, the trailing star is accepted for
readers coming from stock HolyC but carries no separate meaning, see
docs/differences_from_holyc.md for why.

## Literals

  123          decimal integer
  0x7F         hexadecimal integer
  3.14         floating point
  "text\n"     string, escapes: \n \t \r \0 \\ \" \'
  'a'          character, evaluates to its integer code
  TRUE FALSE   boolean literals
  NULL         the empty reference, valid wherever a class type is expected

## Variables

  I64 count;
  I64 a, b, c;
  I64 total = 0;
  F64 ratio = 0.5;
  U8* name = "Terry";
  I64 scores[5];
  I64 primes[5] = {2, 3, 5, 7, 11};

An array declared without an initializer is zero filled to its
declared length. Len(x) returns the length of an array or a string.

## Operators, highest to lowest precedence

  postfix       () [] . ++ --
  unary         ! ~ - + ++ -- (prefix)
  multiplicative * / %
  additive      + -
  shift         << >>
  comparison    < <= > >=
  equality      == !=
  bitwise and   &
  bitwise xor   ^
  bitwise or    |
  logical and   &&
  logical or    ||
  ternary       ?:
  assignment    = += -= *= /= %=

The plus operator also concatenates when either side is a string, so
"score: " + ToStr(total) works directly.

## Control flow

  if (cond) stmt
  if (cond) stmt else stmt

  while (cond) stmt
  do stmt while (cond);

  for (init; cond; step) stmt

  switch (subject) {
      case value: stmt* break;
      default: stmt*
  }

  break;
  continue;
  return expr;
  goto label;
  label:

switch falls through by default, exactly like C, use break to stop.
goto can jump to any label inside the current function, forward or
backward, including out of a nested block into an enclosing one. It
cannot jump into a function it did not start in.

## Functions

  I64 Add(I64 a, I64 b) {
      return a + b;
  }

Parameters are typed and coerced to their declared type on every
call, the same fixed width rules that apply to variables apply here.

## Classes

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

A field can also be a fixed size array, using the same bracket syntax
as a local variable:

  class Bag {
      I64 items[8];
      I64 count;
  };

A method whose name matches the class name is the constructor, it
runs automatically when the class is instantiated. this refers to the
instance the method was called on, and is only valid inside a method
body.

Instantiating a class is a function call written with the class name:

  Point p = Point(3, 4);
  p.Show();

new is accepted as an optional, purely stylistic prefix for readers
coming from other object oriented languages:

  Point* p = new Point(3, 4);
  p.Show();

Both forms produce the exact same reference, HolyC++ has no separate
value versus pointer semantics for class instances, see
docs/differences_from_holyc.md.

## Inheritance

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

Circle inherits every field and method from Shape and may override
any method by declaring one with the same name. Dispatch is virtual:
calling Describe() always runs the version that belongs to the
object's real class, regardless of what type the calling code thinks
it is holding. Inheritance is single, a class names at most one base
class after a colon.

## Imports

  import "helpers.hc++";

Import statements are resolved before parsing begins: hcrun reads the
named file relative to the importing file's own directory, tokenizes
it, and splices its declarations directly into the token stream in
place of the import statement. Importing the same file twice, even
through different paths that resolve to the same file, only includes
it once. A leading slash means an absolute path instead of one
relative to the importing file.

## Standard library

The full reference lives in docs/stdlib.md. The short list:

  Print, StrPrint
  StrLen, StrCat, StrCmp, StrCopy, SubStr, ToUpper, ToLower, StrFind
  Abs, Min, Max, Sqrt, Pow, Floor, Ceil, Round
  ToI64, ToF64, ToStr, Len
  ReadLn, RandI64, RandF64, Now, Exit

## Error handling

HolyC++ does not have exceptions. Errors that cannot be handled by
the running program (division by zero, an out of bounds array index,
calling an unknown function, an unresolved goto target) stop the
program immediately with a message naming the offending line. This
matches the blunt, no safety net philosophy of the original language.
