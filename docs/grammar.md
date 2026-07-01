# HolyC++ Grammar

An EBNF description of the HolyC++ surface syntax, matching exactly
what bootstrap/hcrun.c parses. Terminals are quoted, nonterminals are
lower case, alternatives are separated by a pipe, optional pieces are
wrapped in square brackets, and repetition is written with a star.

## Program

  program        = { import | class_decl | func_decl | global_decl } ;
  import         = "import" STRING ";" ;

## Types

  type           = "U0" | "U8" | "U16" | "U32" | "U64"
                 | "I8" | "I16" | "I32" | "I64"
                 | "F64" | "Bool" | "Str" | "Auto"
                 | IDENT
                 ;
                 [ "*" ]   (* accepted, purely cosmetic *)

## Classes

  class_decl     = "class" IDENT [ ":" IDENT ] "{" { class_member } "}" [ ";" ] ;
  class_member   = field_decl | method_decl ;
  field_decl     = type IDENT [ "[" INT "]" ] { "," IDENT [ "[" INT "]" ] } ";" ;
  method_decl    = type IDENT "(" [ param_list ] ")" block ;

## Functions

  func_decl      = type IDENT "(" [ param_list ] ")" block ;
  param_list     = param { "," param } ;
  param          = type IDENT [ "[" "]" ] ;

## Global and local variable declarations

  global_decl    = type decl_items ;
  var_decl       = type decl_items ;
  decl_items     = decl_item { "," decl_item } ";" ;
  decl_item      = IDENT [ "[" INT "]" ] [ "=" ( expr | array_lit ) ] ;
  array_lit      = "{" [ expr { "," expr } ] "}" ;

## Statements

  statement      = block
                 | if_stmt
                 | while_stmt
                 | do_while_stmt
                 | for_stmt
                 | switch_stmt
                 | "break" ";"
                 | "continue" ";"
                 | "return" [ expr ] ";"
                 | "goto" IDENT ";"
                 | IDENT ":"
                 | var_decl
                 | expr ";"
                 ;

  block          = "{" { statement } "}" ;
  if_stmt        = "if" "(" expr ")" statement [ "else" statement ] ;
  while_stmt     = "while" "(" expr ")" statement ;
  do_while_stmt  = "do" statement "while" "(" expr ")" ";" ;
  for_stmt       = "for" "(" ( var_decl | expr ";" | ";" ) [ expr ] ";" [ expr ] ")" statement ;
  switch_stmt    = "switch" "(" expr ")" "{" { switch_case } "}" ;
  switch_case    = ( "case" expr | "default" ) ":" { statement } ;

## Expressions, listed from lowest to highest precedence

  expr           = assignment ;
  assignment     = ternary [ ( "=" | "+=" | "-=" | "*=" | "/=" | "%=" ) assignment ] ;
  ternary        = logic_or [ "?" expr ":" ternary ] ;
  logic_or       = logic_and { "||" logic_and } ;
  logic_and      = bit_or { "&&" bit_or } ;
  bit_or         = bit_xor { "|" bit_xor } ;
  bit_xor        = bit_and { "^" bit_and } ;
  bit_and        = equality { "&" equality } ;
  equality       = comparison { ( "==" | "!=" ) comparison } ;
  comparison     = shift { ( "<" | "<=" | ">" | ">=" ) shift } ;
  shift          = additive { ( "<<" | ">>" ) additive } ;
  additive       = multiplicative { ( "+" | "-" ) multiplicative } ;
  multiplicative = unary { ( "*" | "/" | "%" ) unary } ;
  unary          = ( "!" | "~" | "-" | "+" | "++" | "--" ) unary | postfix ;
  postfix        = primary { call_suffix | index_suffix | member_suffix | "++" | "--" } ;
  call_suffix    = "(" [ arg_list ] ")" ;
  index_suffix   = "[" expr "]" ;
  member_suffix  = "." IDENT ;
  arg_list       = expr { "," expr } ;

  primary        = INT | FLOAT | STRING | CHAR
                 | "TRUE" | "FALSE" | "NULL" | "this"
                 | IDENT
                 | "(" expr ")"
                 | "new" IDENT "(" [ arg_list ] ")"
                 | array_lit
                 ;

## Notes on ambiguity

A call expression whose callee is a bare identifier, such as
Point(3, 4), is resolved at evaluation time rather than at parse
time: if the identifier names a class it constructs an instance, if
it names a builtin it calls the builtin, otherwise it calls a user
defined function with that name. This keeps the grammar itself small
and pushes the one place where HolyC++ is not context free into a
single, well documented spot in the evaluator.

## Lexical grammar

  IDENT   = ( letter | "_" ) { letter | digit | "_" } ;
  INT     = digit { digit } | "0" ( "x" | "X" ) hexdigit { hexdigit } ;
  FLOAT   = digit { digit } "." digit { digit } ;
  STRING  = '"' { any character except '"', or an escape } '"' ;
  CHAR    = "'" ( any character except "'" , or an escape ) "'" ;

Recognized escapes inside STRING and CHAR literals: \n \t \r \0 \\ \"
\' Any other backslash sequence is a lexical error.
