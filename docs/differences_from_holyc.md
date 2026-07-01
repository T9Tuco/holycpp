# HolyC++ Compared To Stock HolyC

HolyC++ starts from Terry A. Davis HolyC and adds exactly the pieces
needed for real object oriented programming, without touching the
parts of HolyC that already work well. This page lists every point
where the two languages disagree, and why.

## Runs on Linux instead of only inside TempleOS

Stock HolyC is inseparable from TempleOS, it compiles at ring 0 as
part of the operating system itself, with direct access to physical
memory and no process boundary at all. HolyC++ targets an ordinary
Linux process instead, through the bootstrap runtime described in
README.md. This is the single biggest practical difference: HolyC++
programs are ordinary user space programs, they cannot poke arbitrary
physical memory addresses or call into TempleOS kernel routines.

## Classes gain real methods, this, and constructors

In stock HolyC a class is a struct, nothing more. Functions that
operate on a class instance take the instance as an explicit first
parameter, there is no method call syntax, no implicit this, and no
automatic constructor.

HolyC++ classes can declare methods directly inside the class body.
Inside a method, this refers to the instance the method was called
on. A method whose name matches the class name runs automatically
when an instance is created, exactly like a constructor in mainstream
object oriented languages.

## Inheritance gains virtual dispatch

Stock HolyC supports extending one struct with another using a colon,
purely as a memory layout convenience, there is no notion of
overriding a function or of the call site figuring out which version
to run.

HolyC++ inheritance carries method dispatch with it: if a derived
class overrides a method, calling that method on an instance always
runs the derived version, no matter what static type the calling code
believes it is holding. This is what makes polymorphism actually
useful.

## The dot operator replaces the pointer arrow

Stock HolyC follows C's rule that a struct pointer needs arrow syntax
for field access, while a struct value uses dot. HolyC++ collapses
that distinction: class instances always behave like references
under the hood, so a plain dot works whether or not the variable was
declared with a trailing star. The star itself is still accepted for
readers coming from stock HolyC, it is simply optional and has no
separate effect. See docs/language_spec.md for the exact rule.

## new is optional sugar, not a different allocation path

HolyC++ accepts new ClassName(args) as an alternative spelling of
ClassName(args). Both produce the same kind of reference, there is no
separate stack allocated form. This keeps object creation predictable
while still feeling familiar to programmers coming from C plus plus
style languages.

## import replaces the raw text preprocessor

Stock HolyC has no header files and no #include, TempleOS instead
keeps every compiled routine in a single searchable namespace. HolyC++
adds an explicit import statement that pulls another file's
declarations into the current program, resolved once per distinct
file even if it is imported from more than one place. See
docs/language_spec.md for exactly how import resolution works.

## An explicit Main entry point

Inside TempleOS, HolyC statements typed at the shell run immediately,
top to bottom, there is no dedicated entry point function. A HolyC++
source file is instead a set of declarations, and execution begins at
a function named Main, the same convention most compiled languages
use for a standalone program. This is a deliberate simplification, it
makes a .hc++ file behave predictably when run as a batch program
instead of typed live into an interactive shell.

## Everything else stays the same on purpose

The full HolyC numeric type family, TRUE, FALSE and NULL as literal
keywords, fixed width integer wraparound, Print with a C style format
string, switch, goto and labels, // and /* */ comments, all of it
carries over unchanged. HolyC++ is meant to feel like HolyC with real
object orientation bolted on, not like a different language wearing
HolyC's name.
