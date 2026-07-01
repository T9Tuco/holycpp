# HolyC++ Standard Library Reference

Every function listed here is a builtin, implemented natively inside
bootstrap/hcrun.c (see call_builtin) and callable by name from any
HolyC++ program without an import. The self hosted compiler in
selfhost/ relies on exactly this same set, so it is guaranteed to
stay available once HolyC++ programs start running under their own,
self hosted interpreter too.

## Output

### Print(fmt, args...)

Writes formatted text to standard output. fmt is a string containing
ordinary text plus format specifiers:

  %d   an integer, in decimal
  %f   a floating point number
  %s   a string (or the display form of any other value)
  %c   a single character, from an integer code
  %x   an integer, in lowercase hexadecimal
  %b   a boolean, printed as TRUE or FALSE
  %%   a literal percent sign

Example:

  Print("%s scored %d points (%f average)\n", name, score, avg);

### StrPrint(fmt, args...)

Identical formatting rules to Print, but returns the formatted text as
a string instead of writing it out.

  U8* line = StrPrint("x = %d, y = %d", x, y);

## Strings

  StrLen(s)              length of s, in bytes
  StrCat(a, b)            a new string, a followed by b
  StrCmp(a, b)            negative, zero or positive, like C strcmp
  StrCopy(s)               a fresh copy of s
  SubStr(s, start, len)    len characters of s starting at start
  ToUpper(s)                a copy of s in upper case
  ToLower(s)                a copy of s in lower case
  StrFind(hay, needle)     index of the first match, or -1 if absent

Strings also support indexing, s[i] yields the integer character code
at position i, and the plus operator concatenates a string with any
other value by converting that value to text first.

## Math

  Abs(x)      absolute value, works on both integers and floats
  Min(a, b)   the smaller of the two
  Max(a, b)   the larger of the two
  Sqrt(x)     square root, always returns F64
  Pow(x, y)   x raised to the power y, always returns F64
  Floor(x)    largest integer not greater than x, returned as F64
  Ceil(x)     smallest integer not less than x, returned as F64
  Round(x)    x rounded to the nearest integer, returned as I64

## Conversion

  ToI64(x)   x converted to I64, parses a string if x is a string
  ToF64(x)   x converted to F64, parses a string if x is a string
  ToStr(x)   the display text of any value, as a string
  Chr(code)  a single character string built from an integer code

## Arrays and general purpose

  Len(x)     the length of an array, or of a string in bytes

## Input, randomness and time

  ReadLn()          reads a single line of text from standard input
  RandI64(lo, hi)    a random integer between lo and hi, inclusive
  RandF64()          a random float in the half open range 0 to 1
  Now()               the current Unix time, in whole seconds

## Files and command line arguments

  ReadFile(path)          the full contents of a file, as a string
  WriteFile(path, text)   writes text to a file, overwriting it
  ArgCount()                how many extra command line arguments were
                             passed after the script path
  Arg(i)                     the i'th extra command line argument, 0
                             indexed, or an empty string if there is none

Extra arguments are whatever follows the script path on the command
line, for example running hcrun compiler.hc++ target.hc++ gives the
running program ArgCount() equal to 1 and Arg(0) equal to target.hc++.
This is how the self hosted compiler in selfhost/ receives the path
of the program it should run, see README.md.

## Program control

  Exit(code)   stops the program immediately with the given exit code
