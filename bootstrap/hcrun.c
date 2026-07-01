/*
 * hcrun: the HolyC++ bootstrap runtime.
 *
 * Real HolyC by Terry A. Davis only runs inside TempleOS, where the
 * operating system itself is the compiler, the linker and the ring 0
 * JIT all at once. On a normal Linux machine none of that exists, so
 * this single file plays the part of that missing layer: it is a
 * complete lexer, parser and tree walking evaluator for the HolyC++
 * language, compiled once with a plain C compiler.
 *
 * Everything above this file, meaning the actual HolyC++ toolchain
 * (its own lexer, parser and interpreter, written in HolyC++ itself,
 * see selfhost/), is executed BY this program. hcrun never has to be
 * touched again once the self hosted compiler works: it is a bootstrap
 * shim, not the language implementation.
 *
 * Build: gcc -std=c11 -O2 -Wall -Wextra -o hcrun hcrun.c
 * Usage: ./hcrun program.hc++
 */

#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>
#include <math.h>
#include <time.h>
#include <limits.h>
#include <stdarg.h>
#include <unistd.h>

/* ======================================================================
 * Generic growable vector, used for token lists, AST child lists,
 * parameter lists, class tables and so on.
 * ==================================================================== */

typedef struct {
    void **items;
    int count;
    int cap;
} Vec;

static Vec *vec_new(void) {
    Vec *v = calloc(1, sizeof(Vec));
    v->cap = 4;
    v->items = calloc((size_t)v->cap, sizeof(void *));
    return v;
}

static void vec_push(Vec *v, void *item) {
    if (v->count == v->cap) {
        v->cap *= 2;
        v->items = realloc(v->items, (size_t)v->cap * sizeof(void *));
    }
    v->items[v->count++] = item;
}

static void *vec_get(Vec *v, int index) {
    return v->items[index];
}

/* ======================================================================
 * Fatal error reporting.
 * ==================================================================== */

static void fatal(const char *stage, int line, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "HolyC++ %s error", stage);
    if (line > 0) fprintf(stderr, " (line %d)", line);
    fprintf(stderr, ": ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
    exit(1);
}

static char *own_str(const char *s) {
    size_t n = strlen(s);
    char *r = malloc(n + 1);
    memcpy(r, s, n + 1);
    return r;
}

static char *own_str_n(const char *s, size_t n) {
    char *r = malloc(n + 1);
    memcpy(r, s, n);
    r[n] = '\0';
    return r;
}

/* ======================================================================
 * Token types and lexer.
 * ==================================================================== */

typedef enum {
    TK_INT, TK_FLOAT, TK_STRING, TK_CHAR, TK_IDENT,

    TK_U0, TK_U8, TK_U16, TK_U32, TK_U64,
    TK_I8, TK_I16, TK_I32, TK_I64, TK_F64, TK_BOOL, TK_STR, TK_AUTO,

    TK_CLASS, TK_IF, TK_ELSE, TK_WHILE, TK_DO, TK_FOR, TK_SWITCH,
    TK_CASE, TK_DEFAULT, TK_BREAK, TK_CONTINUE, TK_RETURN, TK_GOTO,
    TK_IMPORT, TK_NEW, TK_THIS,

    TK_TRUE, TK_FALSE, TK_NULL,

    TK_LPAREN, TK_RPAREN, TK_LBRACE, TK_RBRACE, TK_LBRACKET, TK_RBRACKET,
    TK_SEMI, TK_COMMA, TK_DOT, TK_COLON, TK_QUESTION,

    TK_PLUS, TK_MINUS, TK_STAR, TK_SLASH, TK_PERCENT,
    TK_ASSIGN, TK_PLUSEQ, TK_MINUSEQ, TK_STAREQ, TK_SLASHEQ, TK_PERCENTEQ,
    TK_EQ, TK_NEQ, TK_LT, TK_LE, TK_GT, TK_GE,
    TK_ANDAND, TK_OROR, TK_NOT, TK_AMP, TK_PIPE, TK_CARET, TK_TILDE,
    TK_SHL, TK_SHR, TK_INC, TK_DEC,

    TK_EOF
} TokKind;

typedef struct {
    TokKind kind;
    char *text;
    int64_t ival;
    double fval;
    int line;
} Token;

typedef struct {
    const char *name;
    TokKind kind;
} Keyword;

static Keyword KEYWORDS[] = {
    {"U0", TK_U0}, {"U8", TK_U8}, {"U16", TK_U16}, {"U32", TK_U32}, {"U64", TK_U64},
    {"I8", TK_I8}, {"I16", TK_I16}, {"I32", TK_I32}, {"I64", TK_I64},
    {"F64", TK_F64}, {"Bool", TK_BOOL}, {"Str", TK_STR}, {"Auto", TK_AUTO},
    {"class", TK_CLASS}, {"if", TK_IF}, {"else", TK_ELSE}, {"while", TK_WHILE},
    {"do", TK_DO}, {"for", TK_FOR}, {"switch", TK_SWITCH}, {"case", TK_CASE},
    {"default", TK_DEFAULT}, {"break", TK_BREAK}, {"continue", TK_CONTINUE},
    {"return", TK_RETURN}, {"goto", TK_GOTO}, {"import", TK_IMPORT},
    {"new", TK_NEW}, {"this", TK_THIS},
    {"TRUE", TK_TRUE}, {"FALSE", TK_FALSE}, {"NULL", TK_NULL},
    {NULL, TK_EOF}
};

typedef struct {
    const char *src;
    size_t pos;
    size_t len;
    int line;
    Vec *tokens;
} Lexer;

static Token *tok_new(TokKind kind, const char *text, int line) {
    Token *t = calloc(1, sizeof(Token));
    t->kind = kind;
    t->text = text ? own_str(text) : NULL;
    t->line = line;
    return t;
}

static char lx_peek(Lexer *lx, int off) {
    size_t idx = lx->pos + (size_t)off;
    if (idx >= lx->len) return '\0';
    return lx->src[idx];
}

static char lx_advance(Lexer *lx) {
    char c = lx->src[lx->pos++];
    if (c == '\n') lx->line++;
    return c;
}

static void lx_string(Lexer *lx) {
    int start_line = lx->line;
    char buf[65536];
    size_t n = 0;
    while (lx_peek(lx, 0) != '"' && lx_peek(lx, 0) != '\0') {
        char c = lx_advance(lx);
        if (c == '\\') {
            char e = lx_advance(lx);
            switch (e) {
                case 'n': buf[n++] = '\n'; break;
                case 't': buf[n++] = '\t'; break;
                case 'r': buf[n++] = '\r'; break;
                case '0': buf[n++] = '\0'; break;
                case '\\': buf[n++] = '\\'; break;
                case '"': buf[n++] = '"'; break;
                case '\'': buf[n++] = '\''; break;
                default: fatal("lexer", lx->line, "unknown escape sequence \\%c", e);
            }
        } else {
            buf[n++] = c;
        }
        if (n >= sizeof(buf) - 1) fatal("lexer", lx->line, "string literal too long");
    }
    if (lx_peek(lx, 0) == '\0') fatal("lexer", start_line, "unterminated string literal");
    lx_advance(lx);
    buf[n] = '\0';
    Token *t = tok_new(TK_STRING, buf, start_line);
    vec_push(lx->tokens, t);
}

static void lx_char(Lexer *lx) {
    int64_t value;
    if (lx_peek(lx, 0) == '\\') {
        lx_advance(lx);
        char e = lx_advance(lx);
        switch (e) {
            case 'n': value = '\n'; break;
            case 't': value = '\t'; break;
            case 'r': value = '\r'; break;
            case '0': value = '\0'; break;
            case '\\': value = '\\'; break;
            case '\'': value = '\''; break;
            default: fatal("lexer", lx->line, "unknown escape sequence \\%c", e); value = 0;
        }
    } else {
        value = lx_advance(lx);
    }
    if (lx_peek(lx, 0) != '\'') fatal("lexer", lx->line, "unterminated character literal");
    lx_advance(lx);
    Token *t = tok_new(TK_CHAR, NULL, lx->line);
    t->ival = value;
    vec_push(lx->tokens, t);
}

static void lx_number(Lexer *lx, char first) {
    char buf[256];
    size_t n = 0;
    buf[n++] = first;
    bool is_float = false;

    if (first == '0' && (lx_peek(lx, 0) == 'x' || lx_peek(lx, 0) == 'X')) {
        buf[n++] = lx_advance(lx);
        while (isxdigit((unsigned char)lx_peek(lx, 0))) buf[n++] = lx_advance(lx);
        buf[n] = '\0';
        Token *t = tok_new(TK_INT, NULL, lx->line);
        t->ival = strtoll(buf, NULL, 16);
        vec_push(lx->tokens, t);
        return;
    }

    while (isdigit((unsigned char)lx_peek(lx, 0))) buf[n++] = lx_advance(lx);
    if (lx_peek(lx, 0) == '.' && isdigit((unsigned char)lx_peek(lx, 1))) {
        is_float = true;
        buf[n++] = lx_advance(lx);
        while (isdigit((unsigned char)lx_peek(lx, 0))) buf[n++] = lx_advance(lx);
    }
    buf[n] = '\0';
    if (is_float) {
        Token *t = tok_new(TK_FLOAT, NULL, lx->line);
        t->fval = strtod(buf, NULL);
        vec_push(lx->tokens, t);
    } else {
        Token *t = tok_new(TK_INT, NULL, lx->line);
        t->ival = strtoll(buf, NULL, 10);
        vec_push(lx->tokens, t);
    }
}

static void lx_ident(Lexer *lx, char first) {
    char buf[1024];
    size_t n = 0;
    buf[n++] = first;
    while (isalnum((unsigned char)lx_peek(lx, 0)) || lx_peek(lx, 0) == '_') buf[n++] = lx_advance(lx);
    buf[n] = '\0';
    for (int i = 0; KEYWORDS[i].name != NULL; i++) {
        if (strcmp(KEYWORDS[i].name, buf) == 0) {
            vec_push(lx->tokens, tok_new(KEYWORDS[i].kind, buf, lx->line));
            return;
        }
    }
    vec_push(lx->tokens, tok_new(TK_IDENT, buf, lx->line));
}

static Vec *lex(const char *src) {
    Lexer lx = { .src = src, .pos = 0, .len = strlen(src), .line = 1, .tokens = vec_new() };
    while (lx_peek(&lx, 0) != '\0') {
        char c = lx_advance(&lx);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;

        if (c == '/' && lx_peek(&lx, 0) == '/') {
            while (lx_peek(&lx, 0) != '\n' && lx_peek(&lx, 0) != '\0') lx_advance(&lx);
            continue;
        }
        if (c == '/' && lx_peek(&lx, 0) == '*') {
            lx_advance(&lx);
            while (!(lx_peek(&lx, 0) == '*' && lx_peek(&lx, 1) == '/')) {
                if (lx_peek(&lx, 0) == '\0') fatal("lexer", lx.line, "unterminated block comment");
                lx_advance(&lx);
            }
            lx_advance(&lx);
            lx_advance(&lx);
            continue;
        }
        if (c == '"') { lx_string(&lx); continue; }
        if (c == '\'') { lx_char(&lx); continue; }
        if (isdigit((unsigned char)c)) { lx_number(&lx, c); continue; }
        if (isalpha((unsigned char)c) || c == '_') { lx_ident(&lx, c); continue; }

        char two[3] = { c, lx_peek(&lx, 0), '\0' };
        struct { const char *op; TokKind kind; } twos[] = {
            {"==", TK_EQ}, {"!=", TK_NEQ}, {"<=", TK_LE}, {">=", TK_GE},
            {"&&", TK_ANDAND}, {"||", TK_OROR}, {"+=", TK_PLUSEQ},
            {"*=", TK_STAREQ}, {"/=", TK_SLASHEQ}, {"%=", TK_PERCENTEQ},
            {"<<", TK_SHL}, {">>", TK_SHR}, {"++", TK_INC},
        };
        bool matched = false;
        for (size_t i = 0; i < sizeof(twos) / sizeof(twos[0]); i++) {
            if (strcmp(two, twos[i].op) == 0) {
                lx_advance(&lx);
                vec_push(lx.tokens, tok_new(twos[i].kind, twos[i].op, lx.line));
                matched = true;
                break;
            }
        }
        if (matched) continue;

        if (c == '-') {
            if (lx_peek(&lx, 0) == '=') { lx_advance(&lx); vec_push(lx.tokens, tok_new(TK_MINUSEQ, "-=", lx.line)); continue; }
            if (lx_peek(&lx, 0) == '-') { lx_advance(&lx); vec_push(lx.tokens, tok_new(TK_DEC, "--", lx.line)); continue; }
            vec_push(lx.tokens, tok_new(TK_MINUS, "-", lx.line));
            continue;
        }

        struct { char ch; TokKind kind; } ones[] = {
            {'(', TK_LPAREN}, {')', TK_RPAREN}, {'{', TK_LBRACE}, {'}', TK_RBRACE},
            {'[', TK_LBRACKET}, {']', TK_RBRACKET}, {';', TK_SEMI}, {',', TK_COMMA},
            {'.', TK_DOT}, {':', TK_COLON}, {'?', TK_QUESTION},
            {'+', TK_PLUS}, {'*', TK_STAR}, {'/', TK_SLASH}, {'%', TK_PERCENT},
            {'=', TK_ASSIGN}, {'<', TK_LT}, {'>', TK_GT}, {'!', TK_NOT},
            {'&', TK_AMP}, {'|', TK_PIPE}, {'^', TK_CARET}, {'~', TK_TILDE},
        };
        matched = false;
        for (size_t i = 0; i < sizeof(ones) / sizeof(ones[0]); i++) {
            if (ones[i].ch == c) {
                char s[2] = { c, '\0' };
                vec_push(lx.tokens, tok_new(ones[i].kind, s, lx.line));
                matched = true;
                break;
            }
        }
        if (!matched) fatal("lexer", lx.line, "unexpected character %c", c);
    }
    vec_push(lx.tokens, tok_new(TK_EOF, "", lx.line));
    return lx.tokens;
}

/* ======================================================================
 * Import resolution: splice imported token streams directly into the
 * main token stream before parsing, the same way a preprocessor would,
 * but token based instead of text based so string and comment content
 * can never be mistaken for an import statement.
 * ==================================================================== */

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) fatal("io", 0, "cannot open file %s", path);
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)size + 1);
    size_t got = fread(buf, 1, (size_t)size, f);
    buf[got] = '\0';
    fclose(f);
    return buf;
}

static void dirname_of(const char *path, char *out, size_t outsz) {
    const char *slash = strrchr(path, '/');
    if (!slash) { snprintf(out, outsz, "."); return; }
    size_t n = (size_t)(slash - path);
    if (n >= outsz) n = outsz - 1;
    memcpy(out, path, n);
    out[n] = '\0';
}

typedef struct { char **items; int count; } StrList;

static bool strlist_has(StrList *list, const char *s) {
    for (int i = 0; i < list->count; i++) if (strcmp(list->items[i], s) == 0) return true;
    return false;
}

static Vec *resolve_imports(const char *path, StrList *visited) {
    char real[PATH_MAX];
    if (!realpath(path, real)) fatal("io", 0, "cannot resolve path %s", path);
    if (strlist_has(visited, real)) return vec_new();
    visited->items = realloc(visited->items, sizeof(char *) * (size_t)(visited->count + 1));
    visited->items[visited->count++] = own_str(real);

    char *src = read_file(path);
    Vec *tokens = lex(src);

    char base[PATH_MAX];
    dirname_of(path, base, sizeof(base));

    Vec *out = vec_new();
    for (int i = 0; i < tokens->count; i++) {
        Token *t = vec_get(tokens, i);
        if (t->kind == TK_IMPORT) {
            Token *pathTok = vec_get(tokens, i + 1);
            Token *semiTok = vec_get(tokens, i + 2);
            if (pathTok->kind != TK_STRING || semiTok->kind != TK_SEMI)
                fatal("parser", t->line, "expected import \"file\";");
            char *full = malloc(strlen(base) + strlen(pathTok->text) + 2);
            if (pathTok->text[0] == '/') strcpy(full, pathTok->text);
            else sprintf(full, "%s/%s", base, pathTok->text);
            Vec *inner = resolve_imports(full, visited);
            for (int k = 0; k < inner->count; k++) {
                Token *it = vec_get(inner, k);
                if (it->kind != TK_EOF) vec_push(out, it);
            }
            i += 2;
            continue;
        }
        if (t->kind == TK_EOF) continue;
        vec_push(out, t);
    }
    vec_push(out, tok_new(TK_EOF, "", 0));
    return out;
}

/* ======================================================================
 * Types.
 * ==================================================================== */

typedef enum {
    T_U0, T_U8, T_U16, T_U32, T_U64, T_I8, T_I16, T_I32, T_I64,
    T_F64, T_BOOL, T_STR, T_AUTO, T_CLASS
} TypeKind;

typedef struct {
    TypeKind kind;
    char *class_name; /* only meaningful when kind == T_CLASS */
} TypeInfo;

static TypeInfo type_simple(TypeKind k) {
    TypeInfo t; t.kind = k; t.class_name = NULL; return t;
}

/* ======================================================================
 * AST: expressions.
 * ==================================================================== */

typedef enum {
    E_INT, E_FLOAT, E_STRING, E_CHAR, E_BOOL, E_NULL, E_THIS,
    E_IDENT, E_ASSIGN, E_COMPOUND, E_BINARY, E_LOGICAL, E_UNARY,
    E_PREINC, E_PREDEC, E_POSTINC, E_POSTDEC,
    E_CALL, E_NEWCALL, E_INDEX, E_MEMBER, E_TERNARY, E_ARRAYLIT
} ExprKind;

typedef struct Expr {
    ExprKind kind;
    int line;
    int64_t ival;
    double fval;
    char *sval;
    bool bval;
    char *name;          /* identifier / member name / operator text */
    struct Expr *a, *b, *c;
    Vec *args;            /* Vec<Expr*> for calls and array literals */
} Expr;

static Expr *expr_new(ExprKind kind, int line) {
    Expr *e = calloc(1, sizeof(Expr));
    e->kind = kind;
    e->line = line;
    return e;
}

/* ======================================================================
 * AST: statements.
 * ==================================================================== */

typedef enum {
    S_EXPR, S_VARDECL, S_BLOCK, S_IF, S_WHILE, S_DOWHILE, S_FOR, S_SWITCH,
    S_BREAK, S_CONTINUE, S_RETURN, S_GOTO, S_LABEL
} StmtKind;

typedef struct {
    char *name;
    int array_size;   /* 0 when the variable is not an array */
    struct Expr *init;
    Vec *array_lit;    /* Vec<Expr*>, used when init is a {a, b, c} literal */
} VarDeclItem;

typedef struct {
    bool is_default;
    struct Expr *value; /* NULL for default */
    Vec *stmts;           /* Vec<Stmt*> */
} SwitchCase;

typedef struct Stmt {
    StmtKind kind;
    int line;

    struct Expr *expr;      /* EXPR value, IF/WHILE/DOWHILE condition, RETURN value, SWITCH subject */
    struct Stmt *a, *b;      /* IF then/else, WHILE/DOWHILE body */

    struct Stmt *for_init;
    struct Expr *for_cond;
    struct Expr *for_post;
    struct Stmt *for_body;

    Vec *stmts;              /* BLOCK statements */

    TypeInfo decl_type;
    Vec *decl_items;         /* Vec<VarDeclItem*> */

    Vec *cases;               /* Vec<SwitchCase*> */

    char *label;
} Stmt;

static Stmt *stmt_new(StmtKind kind, int line) {
    Stmt *s = calloc(1, sizeof(Stmt));
    s->kind = kind;
    s->line = line;
    return s;
}

/* ======================================================================
 * AST: functions and classes.
 * ==================================================================== */

typedef struct {
    TypeInfo type;
    char *name;
} Param;

typedef struct {
    char *name;
    TypeInfo ret_type;
    Vec *params;      /* Vec<Param*> */
    Stmt *body;         /* NULL for a forward declaration, unused here */
    char *owner_class;  /* NULL for a free function, class name for a method */
} FuncDef;

typedef struct {
    TypeInfo type;
    char *name;
    int array_size; /* 0 when the field is not an array */
} FieldDecl;

typedef struct ClassDef {
    char *name;
    char *base_name;    /* NULL when there is no base class */
    struct ClassDef *base; /* resolved after all classes are collected */
    Vec *fields;          /* Vec<FieldDecl*>, declared directly on this class */
    Vec *methods;          /* Vec<FuncDef*>, declared directly on this class */
} ClassDef;

typedef struct {
    Vec *classes;   /* Vec<ClassDef*> */
    Vec *funcs;     /* Vec<FuncDef*> */
    Vec *globals;   /* Vec<Stmt*> (S_VARDECL) */
} Program;

/* ======================================================================
 * Parser.
 * ==================================================================== */

typedef struct {
    Vec *tokens;
    int pos;
} Parser;

static Token *p_peek(Parser *p) { return vec_get(p->tokens, p->pos); }
static Token *p_peek_at(Parser *p, int off) {
    int idx = p->pos + off;
    if (idx >= p->tokens->count) idx = p->tokens->count - 1;
    return vec_get(p->tokens, idx);
}
static Token *p_advance(Parser *p) {
    Token *t = p_peek(p);
    if (t->kind != TK_EOF) p->pos++;
    return t;
}
static bool p_check(Parser *p, TokKind k) { return p_peek(p)->kind == k; }
static bool p_match(Parser *p, TokKind k) {
    if (p_check(p, k)) { p_advance(p); return true; }
    return false;
}
static Token *p_expect(Parser *p, TokKind k, const char *what) {
    if (!p_check(p, k)) fatal("parser", p_peek(p)->line, "expected %s but found '%s'", what, p_peek(p)->text ? p_peek(p)->text : "?");
    return p_advance(p);
}

static bool is_type_start(TokKind k) {
    switch (k) {
        case TK_U0: case TK_U8: case TK_U16: case TK_U32: case TK_U64:
        case TK_I8: case TK_I16: case TK_I32: case TK_I64: case TK_F64:
        case TK_BOOL: case TK_STR: case TK_AUTO:
            return true;
        default:
            return false;
    }
}

static TypeInfo p_parse_type(Parser *p) {
    Token *t = p_advance(p);
    TypeInfo ty;
    switch (t->kind) {
        case TK_U0: ty = type_simple(T_U0); break;
        case TK_U8: ty = type_simple(T_U8); break;
        case TK_U16: ty = type_simple(T_U16); break;
        case TK_U32: ty = type_simple(T_U32); break;
        case TK_U64: ty = type_simple(T_U64); break;
        case TK_I8: ty = type_simple(T_I8); break;
        case TK_I16: ty = type_simple(T_I16); break;
        case TK_I32: ty = type_simple(T_I32); break;
        case TK_I64: ty = type_simple(T_I64); break;
        case TK_F64: ty = type_simple(T_F64); break;
        case TK_BOOL: ty = type_simple(T_BOOL); break;
        case TK_STR: ty = type_simple(T_STR); break;
        case TK_AUTO: ty = type_simple(T_AUTO); break;
        case TK_IDENT: ty.kind = T_CLASS; ty.class_name = own_str(t->text); break;
        default:
            fatal("parser", t->line, "expected a type but found '%s'", t->text ? t->text : "?");
            ty = type_simple(T_I64);
    }
    /* A trailing star is accepted for authenticity (U8*, ClassName*) but
       HolyC++ treats it as purely cosmetic: all class values and strings
       already behave like references, see docs/differences_from_holyc.md */
    p_match(p, TK_STAR);
    return ty;
}

static bool p_looks_like_type(Parser *p) {
    if (is_type_start(p_peek(p)->kind)) return true;
    if (p_peek(p)->kind == TK_IDENT && p_peek_at(p, 1)->kind == TK_IDENT) return true;
    return false;
}

static struct Expr *p_expr(Parser *p);
static Stmt *p_statement(Parser *p);
static Stmt *p_block(Parser *p);
static Vec *p_decl_items(Parser *p);

/* ---- expression grammar, lowest to highest precedence ---------------- */

static Expr *p_assignment(Parser *p);

static Expr *p_expr(Parser *p) { return p_assignment(p); }

static Vec *p_arg_list(Parser *p) {
    Vec *args = vec_new();
    if (!p_check(p, TK_RPAREN)) {
        vec_push(args, p_expr(p));
        while (p_match(p, TK_COMMA)) vec_push(args, p_expr(p));
    }
    p_expect(p, TK_RPAREN, ")");
    return args;
}

static Expr *p_primary(Parser *p) {
    Token *t = p_peek(p);

    if (p_match(p, TK_INT)) { Expr *e = expr_new(E_INT, t->line); e->ival = t->ival; return e; }
    if (p_match(p, TK_FLOAT)) { Expr *e = expr_new(E_FLOAT, t->line); e->fval = t->fval; return e; }
    if (p_match(p, TK_STRING)) { Expr *e = expr_new(E_STRING, t->line); e->sval = own_str(t->text); return e; }
    if (p_match(p, TK_CHAR)) { Expr *e = expr_new(E_CHAR, t->line); e->ival = t->ival; return e; }
    if (p_match(p, TK_TRUE)) { Expr *e = expr_new(E_BOOL, t->line); e->bval = true; return e; }
    if (p_match(p, TK_FALSE)) { Expr *e = expr_new(E_BOOL, t->line); e->bval = false; return e; }
    if (p_match(p, TK_NULL)) { return expr_new(E_NULL, t->line); }
    if (p_match(p, TK_THIS)) { return expr_new(E_THIS, t->line); }

    if (p_match(p, TK_NEW)) {
        Token *name = p_expect(p, TK_IDENT, "class name after new");
        p_expect(p, TK_LPAREN, "(");
        Expr *e = expr_new(E_NEWCALL, t->line);
        e->name = own_str(name->text);
        e->args = p_arg_list(p);
        return e;
    }

    if (p_match(p, TK_LBRACE)) {
        Expr *e = expr_new(E_ARRAYLIT, t->line);
        e->args = vec_new();
        if (!p_check(p, TK_RBRACE)) {
            vec_push(e->args, p_expr(p));
            while (p_match(p, TK_COMMA)) vec_push(e->args, p_expr(p));
        }
        p_expect(p, TK_RBRACE, "}");
        return e;
    }

    if (p_match(p, TK_LPAREN)) {
        Expr *inner = p_expr(p);
        p_expect(p, TK_RPAREN, ")");
        return inner;
    }

    if (p_match(p, TK_IDENT)) {
        Expr *e = expr_new(E_IDENT, t->line);
        e->name = own_str(t->text);
        return e;
    }

    fatal("parser", t->line, "unexpected token '%s'", t->text ? t->text : "?");
    return NULL;
}

static Expr *p_postfix(Parser *p) {
    Expr *e = p_primary(p);
    for (;;) {
        if (p_match(p, TK_LPAREN)) {
            Expr *call = expr_new(E_CALL, e->line);
            call->a = e;
            call->args = p_arg_list(p);
            e = call;
        } else if (p_match(p, TK_LBRACKET)) {
            Expr *idx = expr_new(E_INDEX, e->line);
            idx->a = e;
            idx->b = p_expr(p);
            p_expect(p, TK_RBRACKET, "]");
            e = idx;
        } else if (p_match(p, TK_DOT)) {
            Token *name = p_expect(p, TK_IDENT, "member name");
            Expr *m = expr_new(E_MEMBER, e->line);
            m->a = e;
            m->name = own_str(name->text);
            e = m;
        } else if (p_check(p, TK_INC)) {
            p_advance(p);
            Expr *inc = expr_new(E_POSTINC, e->line);
            inc->a = e;
            e = inc;
        } else if (p_check(p, TK_DEC)) {
            p_advance(p);
            Expr *dec = expr_new(E_POSTDEC, e->line);
            dec->a = e;
            e = dec;
        } else {
            break;
        }
    }
    return e;
}

static Expr *p_unary(Parser *p) {
    Token *t = p_peek(p);
    if (t->kind == TK_NOT || t->kind == TK_TILDE || t->kind == TK_MINUS || t->kind == TK_PLUS) {
        p_advance(p);
        Expr *e = expr_new(E_UNARY, t->line);
        e->name = own_str(t->text);
        e->a = p_unary(p);
        return e;
    }
    if (t->kind == TK_INC) {
        p_advance(p);
        Expr *e = expr_new(E_PREINC, t->line);
        e->a = p_unary(p);
        return e;
    }
    if (t->kind == TK_DEC) {
        p_advance(p);
        Expr *e = expr_new(E_PREDEC, t->line);
        e->a = p_unary(p);
        return e;
    }
    return p_postfix(p);
}

#define BIN_LEVEL(NAME, NEXT, ...) \
    static Expr *NAME(Parser *p) { \
        Expr *left = NEXT(p); \
        TokKind ops[] = { __VA_ARGS__, TK_EOF }; \
        for (;;) { \
            bool found = false; \
            for (int i = 0; ops[i] != TK_EOF; i++) { \
                if (p_check(p, ops[i])) { \
                    Token *op = p_advance(p); \
                    Expr *e = expr_new(E_BINARY, op->line); \
                    e->name = own_str(op->text); \
                    e->a = left; \
                    e->b = NEXT(p); \
                    left = e; \
                    found = true; \
                    break; \
                } \
            } \
            if (!found) break; \
        } \
        return left; \
    }

BIN_LEVEL(p_multiplicative, p_unary, TK_STAR, TK_SLASH, TK_PERCENT)
BIN_LEVEL(p_additive, p_multiplicative, TK_PLUS, TK_MINUS)
BIN_LEVEL(p_shift, p_additive, TK_SHL, TK_SHR)
BIN_LEVEL(p_comparison, p_shift, TK_LT, TK_LE, TK_GT, TK_GE)
BIN_LEVEL(p_equality, p_comparison, TK_EQ, TK_NEQ)
BIN_LEVEL(p_bitand, p_equality, TK_AMP)
BIN_LEVEL(p_bitxor, p_bitand, TK_CARET)
BIN_LEVEL(p_bitor, p_bitxor, TK_PIPE)

static Expr *p_logic_and(Parser *p) {
    Expr *left = p_bitor(p);
    while (p_check(p, TK_ANDAND)) {
        Token *op = p_advance(p);
        Expr *e = expr_new(E_LOGICAL, op->line);
        e->name = own_str(op->text);
        e->a = left;
        e->b = p_bitor(p);
        left = e;
    }
    return left;
}

static Expr *p_logic_or(Parser *p) {
    Expr *left = p_logic_and(p);
    while (p_check(p, TK_OROR)) {
        Token *op = p_advance(p);
        Expr *e = expr_new(E_LOGICAL, op->line);
        e->name = own_str(op->text);
        e->a = left;
        e->b = p_logic_and(p);
        left = e;
    }
    return left;
}

static Expr *p_ternary(Parser *p) {
    Expr *cond = p_logic_or(p);
    if (p_match(p, TK_QUESTION)) {
        Expr *e = expr_new(E_TERNARY, cond->line);
        e->a = cond;
        e->b = p_expr(p);
        p_expect(p, TK_COLON, ":");
        e->c = p_ternary(p);
        return e;
    }
    return cond;
}

static Expr *p_assignment(Parser *p) {
    Expr *left = p_ternary(p);
    TokKind k = p_peek(p)->kind;
    if (k == TK_ASSIGN || k == TK_PLUSEQ || k == TK_MINUSEQ || k == TK_STAREQ ||
        k == TK_SLASHEQ || k == TK_PERCENTEQ) {
        Token *op = p_advance(p);
        Expr *e = expr_new(k == TK_ASSIGN ? E_ASSIGN : E_COMPOUND, op->line);
        e->name = own_str(op->text);
        e->a = left;
        e->b = p_assignment(p);
        return e;
    }
    return left;
}

/* ---- statement grammar ------------------------------------------------ */

static Vec *p_decl_items(Parser *p) {
    Vec *items = vec_new();
    for (;;) {
        Token *name = p_expect(p, TK_IDENT, "variable name");
        VarDeclItem *item = calloc(1, sizeof(VarDeclItem));
        item->name = own_str(name->text);
        if (p_match(p, TK_LBRACKET)) {
            Token *size = p_expect(p, TK_INT, "array size");
            item->array_size = (int)size->ival;
            p_expect(p, TK_RBRACKET, "]");
        }
        if (p_match(p, TK_ASSIGN)) {
            if (p_check(p, TK_LBRACE)) {
                p_advance(p);
                item->array_lit = vec_new();
                if (!p_check(p, TK_RBRACE)) {
                    vec_push(item->array_lit, p_expr(p));
                    while (p_match(p, TK_COMMA)) vec_push(item->array_lit, p_expr(p));
                }
                p_expect(p, TK_RBRACE, "}");
            } else {
                item->init = p_expr(p);
            }
        }
        vec_push(items, item);
        if (!p_match(p, TK_COMMA)) break;
    }
    p_expect(p, TK_SEMI, ";");
    return items;
}

static Stmt *p_vardecl(Parser *p) {
    int line = p_peek(p)->line;
    TypeInfo ty = p_parse_type(p);
    Stmt *s = stmt_new(S_VARDECL, line);
    s->decl_type = ty;
    s->decl_items = p_decl_items(p);
    return s;
}

static Stmt *p_block(Parser *p) {
    int line = p_peek(p)->line;
    p_expect(p, TK_LBRACE, "{");
    Stmt *s = stmt_new(S_BLOCK, line);
    s->stmts = vec_new();
    while (!p_check(p, TK_RBRACE) && !p_check(p, TK_EOF)) vec_push(s->stmts, p_statement(p));
    p_expect(p, TK_RBRACE, "}");
    return s;
}

static Stmt *p_statement(Parser *p) {
    Token *t = p_peek(p);

    if (t->kind == TK_LBRACE) return p_block(p);

    if (t->kind == TK_IF) {
        p_advance(p);
        p_expect(p, TK_LPAREN, "(");
        Stmt *s = stmt_new(S_IF, t->line);
        s->expr = p_expr(p);
        p_expect(p, TK_RPAREN, ")");
        s->a = p_statement(p);
        if (p_match(p, TK_ELSE)) s->b = p_statement(p);
        return s;
    }

    if (t->kind == TK_WHILE) {
        p_advance(p);
        p_expect(p, TK_LPAREN, "(");
        Stmt *s = stmt_new(S_WHILE, t->line);
        s->expr = p_expr(p);
        p_expect(p, TK_RPAREN, ")");
        s->a = p_statement(p);
        return s;
    }

    if (t->kind == TK_DO) {
        p_advance(p);
        Stmt *s = stmt_new(S_DOWHILE, t->line);
        s->a = p_statement(p);
        p_expect(p, TK_WHILE, "while");
        p_expect(p, TK_LPAREN, "(");
        s->expr = p_expr(p);
        p_expect(p, TK_RPAREN, ")");
        p_expect(p, TK_SEMI, ";");
        return s;
    }

    if (t->kind == TK_FOR) {
        p_advance(p);
        p_expect(p, TK_LPAREN, "(");
        Stmt *s = stmt_new(S_FOR, t->line);
        if (p_check(p, TK_SEMI)) { p_advance(p); s->for_init = NULL; }
        else if (p_looks_like_type(p)) s->for_init = p_vardecl(p);
        else { Stmt *es = stmt_new(S_EXPR, p_peek(p)->line); es->expr = p_expr(p); p_expect(p, TK_SEMI, ";"); s->for_init = es; }

        if (!p_check(p, TK_SEMI)) s->for_cond = p_expr(p);
        p_expect(p, TK_SEMI, ";");

        if (!p_check(p, TK_RPAREN)) s->for_post = p_expr(p);
        p_expect(p, TK_RPAREN, ")");

        s->for_body = p_statement(p);
        return s;
    }

    if (t->kind == TK_SWITCH) {
        p_advance(p);
        p_expect(p, TK_LPAREN, "(");
        Stmt *s = stmt_new(S_SWITCH, t->line);
        s->expr = p_expr(p);
        p_expect(p, TK_RPAREN, ")");
        p_expect(p, TK_LBRACE, "{");
        s->cases = vec_new();
        while (!p_check(p, TK_RBRACE) && !p_check(p, TK_EOF)) {
            SwitchCase *c = calloc(1, sizeof(SwitchCase));
            if (p_match(p, TK_CASE)) {
                c->value = p_expr(p);
                p_expect(p, TK_COLON, ":");
            } else {
                p_expect(p, TK_DEFAULT, "default");
                p_expect(p, TK_COLON, ":");
                c->is_default = true;
            }
            c->stmts = vec_new();
            while (!p_check(p, TK_CASE) && !p_check(p, TK_DEFAULT) && !p_check(p, TK_RBRACE) && !p_check(p, TK_EOF))
                vec_push(c->stmts, p_statement(p));
            vec_push(s->cases, c);
        }
        p_expect(p, TK_RBRACE, "}");
        return s;
    }

    if (t->kind == TK_BREAK) { p_advance(p); p_expect(p, TK_SEMI, ";"); return stmt_new(S_BREAK, t->line); }
    if (t->kind == TK_CONTINUE) { p_advance(p); p_expect(p, TK_SEMI, ";"); return stmt_new(S_CONTINUE, t->line); }

    if (t->kind == TK_RETURN) {
        p_advance(p);
        Stmt *s = stmt_new(S_RETURN, t->line);
        if (!p_check(p, TK_SEMI)) s->expr = p_expr(p);
        p_expect(p, TK_SEMI, ";");
        return s;
    }

    if (t->kind == TK_GOTO) {
        p_advance(p);
        Token *name = p_expect(p, TK_IDENT, "label name");
        p_expect(p, TK_SEMI, ";");
        Stmt *s = stmt_new(S_GOTO, t->line);
        s->label = own_str(name->text);
        return s;
    }

    if (t->kind == TK_IDENT && p_peek_at(p, 1)->kind == TK_COLON) {
        p_advance(p);
        p_advance(p);
        Stmt *s = stmt_new(S_LABEL, t->line);
        s->label = own_str(t->text);
        return s;
    }

    if (p_looks_like_type(p)) return p_vardecl(p);

    Stmt *s = stmt_new(S_EXPR, t->line);
    s->expr = p_expr(p);
    p_expect(p, TK_SEMI, ";");
    return s;
}

/* ---- top level: classes, functions, globals --------------------------- */

static Vec *p_param_list(Parser *p) {
    Vec *params = vec_new();
    if (!p_check(p, TK_RPAREN)) {
        for (;;) {
            Param *param = calloc(1, sizeof(Param));
            param->type = p_parse_type(p);
            param->name = own_str(p_expect(p, TK_IDENT, "parameter name")->text);
            if (p_match(p, TK_LBRACKET)) p_expect(p, TK_RBRACKET, "]");
            vec_push(params, param);
            if (!p_match(p, TK_COMMA)) break;
        }
    }
    p_expect(p, TK_RPAREN, ")");
    return params;
}

static void p_class_decl(Parser *p, Program *prog) {
    p_expect(p, TK_CLASS, "class");
    Token *name = p_expect(p, TK_IDENT, "class name");
    ClassDef *cls = calloc(1, sizeof(ClassDef));
    cls->name = own_str(name->text);
    cls->fields = vec_new();
    cls->methods = vec_new();
    if (p_match(p, TK_COLON)) cls->base_name = own_str(p_expect(p, TK_IDENT, "base class name")->text);
    p_expect(p, TK_LBRACE, "{");
    while (!p_check(p, TK_RBRACE) && !p_check(p, TK_EOF)) {
        TypeInfo ty = p_parse_type(p);
        Token *member_name = p_expect(p, TK_IDENT, "member name");
        if (p_check(p, TK_LPAREN)) {
            p_advance(p);
            FuncDef *fn = calloc(1, sizeof(FuncDef));
            fn->name = own_str(member_name->text);
            fn->ret_type = ty;
            fn->owner_class = own_str(cls->name);
            fn->params = p_param_list(p);
            fn->body = p_block(p);
            vec_push(cls->methods, fn);
        } else {
            FieldDecl *field = calloc(1, sizeof(FieldDecl));
            field->type = ty;
            field->name = own_str(member_name->text);
            if (p_match(p, TK_LBRACKET)) {
                field->array_size = (int)p_expect(p, TK_INT, "array size")->ival;
                p_expect(p, TK_RBRACKET, "]");
            }
            vec_push(cls->fields, field);
            while (p_match(p, TK_COMMA)) {
                FieldDecl *more = calloc(1, sizeof(FieldDecl));
                more->type = ty;
                more->name = own_str(p_expect(p, TK_IDENT, "field name")->text);
                if (p_match(p, TK_LBRACKET)) {
                    more->array_size = (int)p_expect(p, TK_INT, "array size")->ival;
                    p_expect(p, TK_RBRACKET, "]");
                }
                vec_push(cls->fields, more);
            }
            p_expect(p, TK_SEMI, ";");
        }
    }
    p_expect(p, TK_RBRACE, "}");
    p_match(p, TK_SEMI);
    vec_push(prog->classes, cls);
}

static void p_top_level(Parser *p, Program *prog) {
    while (!p_check(p, TK_EOF)) {
        if (p_check(p, TK_CLASS)) { p_class_decl(p, prog); continue; }

        if (is_type_start(p_peek(p)->kind) ||
            (p_peek(p)->kind == TK_IDENT && p_peek_at(p, 1)->kind == TK_IDENT)) {
            TypeInfo ty = p_parse_type(p);
            Token *name = p_expect(p, TK_IDENT, "function or variable name");
            if (p_check(p, TK_LPAREN)) {
                p_advance(p);
                FuncDef *fn = calloc(1, sizeof(FuncDef));
                fn->name = own_str(name->text);
                fn->ret_type = ty;
                fn->params = p_param_list(p);
                fn->body = p_block(p);
                vec_push(prog->funcs, fn);
            } else {
                p->pos -= 1; /* rewind to the identifier, let p_decl_items reread it */
                Stmt *s = stmt_new(S_VARDECL, name->line);
                s->decl_type = ty;
                s->decl_items = p_decl_items(p);
                vec_push(prog->globals, s);
            }
            continue;
        }

        fatal("parser", p_peek(p)->line, "expected a class, function or variable declaration, found '%s'",
              p_peek(p)->text ? p_peek(p)->text : "?");
    }
}

static Program *parse_program(Vec *tokens) {
    Parser p = { .tokens = tokens, .pos = 0 };
    Program *prog = calloc(1, sizeof(Program));
    prog->classes = vec_new();
    prog->funcs = vec_new();
    prog->globals = vec_new();
    p_top_level(&p, prog);
    return prog;
}

/* ======================================================================
 * Runtime values.
 * ==================================================================== */

typedef enum { V_I64, V_F64, V_BOOL, V_STR, V_ARRAY, V_OBJ, V_NULL, V_VOID } VKind;

typedef struct ArrayVal {
    TypeInfo elem_type;
    int length;
    struct Value *items;
} ArrayVal;

typedef struct ObjVal {
    ClassDef *cls;
    char **field_names;
    struct Value *field_values;
    int field_count;
} ObjVal;

typedef struct Value {
    VKind kind;
    int64_t i;
    double f;
    bool b;
    char *s;
    ArrayVal *arr;
    ObjVal *obj;
} Value;

static Value V_void(void) { Value v = {0}; v.kind = V_VOID; return v; }
static Value V_null(void) { Value v = {0}; v.kind = V_NULL; return v; }
static Value V_i64(int64_t x) { Value v = {0}; v.kind = V_I64; v.i = x; return v; }
static Value V_f64(double x) { Value v = {0}; v.kind = V_F64; v.f = x; return v; }
static Value V_bool(bool x) { Value v = {0}; v.kind = V_BOOL; v.b = x; return v; }
static Value V_str(const char *x) { Value v = {0}; v.kind = V_STR; v.s = own_str(x); return v; }

/* Environment: a chain of scopes, each a flat list of bindings.
   Global scope sits at the root of every chain. */

typedef struct Binding {
    char *name;
    TypeInfo type;
    Value value;
} Binding;

typedef struct Env {
    Vec *bindings; /* Vec<Binding*> */
    struct Env *parent;
    Value *this_val; /* points at the current 'this' Value, or NULL at top level */
} Env;

static Env *env_new(Env *parent) {
    Env *e = calloc(1, sizeof(Env));
    e->bindings = vec_new();
    e->parent = parent;
    e->this_val = parent ? parent->this_val : NULL;
    return e;
}

static Binding *env_find_local(Env *e, const char *name) {
    for (int i = 0; i < e->bindings->count; i++) {
        Binding *b = vec_get(e->bindings, i);
        if (strcmp(b->name, name) == 0) return b;
    }
    return NULL;
}

static Binding *env_find(Env *e, const char *name) {
    for (Env *cur = e; cur; cur = cur->parent) {
        Binding *b = env_find_local(cur, name);
        if (b) return b;
    }
    return NULL;
}

static Binding *env_define(Env *e, const char *name, TypeInfo type, Value value) {
    Binding *b = calloc(1, sizeof(Binding));
    b->name = own_str(name);
    b->type = type;
    b->value = value;
    vec_push(e->bindings, b);
    return b;
}

/* ======================================================================
 * Program tables: resolved class hierarchy, function lookup.
 * ==================================================================== */

static Program *G_PROG;
static Env *G_GLOBALS;
static int G_ARGC = 0;
static char **G_ARGV = NULL;

static ClassDef *find_class(const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < G_PROG->classes->count; i++) {
        ClassDef *c = vec_get(G_PROG->classes, i);
        if (strcmp(c->name, name) == 0) return c;
    }
    return NULL;
}

static FuncDef *find_free_func(const char *name) {
    for (int i = 0; i < G_PROG->funcs->count; i++) {
        FuncDef *f = vec_get(G_PROG->funcs, i);
        if (strcmp(f->name, name) == 0) return f;
    }
    return NULL;
}

static FuncDef *find_method(ClassDef *cls, const char *name) {
    for (ClassDef *cur = cls; cur; cur = cur->base) {
        for (int i = 0; i < cur->methods->count; i++) {
            FuncDef *f = vec_get(cur->methods, i);
            if (strcmp(f->name, name) == 0) return f;
        }
    }
    return NULL;
}

static void link_classes(void) {
    for (int i = 0; i < G_PROG->classes->count; i++) {
        ClassDef *c = vec_get(G_PROG->classes, i);
        if (c->base_name) {
            c->base = find_class(c->base_name);
            if (!c->base) fatal("link", 0, "class %s extends unknown class %s", c->name, c->base_name);
        }
    }
}

/* Flattened field list, base class fields first, used to build a fresh
   ObjVal whenever a class is instantiated. */

static void collect_fields(ClassDef *cls, Vec *out_fields) {
    if (!cls) return;
    collect_fields(cls->base, out_fields);
    for (int i = 0; i < cls->fields->count; i++) vec_push(out_fields, vec_get(cls->fields, i));
}

static Value default_value_for_type(TypeInfo ty);
static Value make_array_value(TypeInfo elem_type, int length);

static ObjVal *make_instance(ClassDef *cls) {
    Vec *fields = vec_new();
    collect_fields(cls, fields);
    ObjVal *obj = calloc(1, sizeof(ObjVal));
    obj->cls = cls;
    obj->field_count = fields->count;
    obj->field_names = calloc((size_t)fields->count, sizeof(char *));
    obj->field_values = calloc((size_t)fields->count, sizeof(Value));
    for (int i = 0; i < fields->count; i++) {
        FieldDecl *f = vec_get(fields, i);
        obj->field_names[i] = own_str(f->name);
        obj->field_values[i] = f->array_size > 0 ? make_array_value(f->type, f->array_size) : default_value_for_type(f->type);
    }
    return obj;
}

static int obj_field_index(ObjVal *obj, const char *name) {
    for (int i = 0; i < obj->field_count; i++)
        if (strcmp(obj->field_names[i], name) == 0) return i;
    return -1;
}

/* ======================================================================
 * Type coercion. HolyC keeps every integer as a fixed width machine
 * word, so assigning into a U8 wraps modulo 256 the same way it would
 * on real hardware. This table drives that behaviour.
 * ==================================================================== */

static int type_bits(TypeKind k) {
    switch (k) {
        case T_U8: case T_I8: return 8;
        case T_U16: case T_I16: return 16;
        case T_U32: case T_I32: return 32;
        default: return 64;
    }
}

static bool type_signed(TypeKind k) {
    return k == T_I8 || k == T_I16 || k == T_I32 || k == T_I64;
}

static int64_t mask_int(int64_t value, TypeKind k) {
    int bits = type_bits(k);
    if (bits >= 64) return value;
    uint64_t mask = ((uint64_t)1 << bits) - 1;
    uint64_t masked = (uint64_t)value & mask;
    if (type_signed(k) && (masked & ((uint64_t)1 << (bits - 1))))
        masked |= ~mask;
    return (int64_t)masked;
}

static Value default_value_for_type(TypeInfo ty) {
    switch (ty.kind) {
        case T_F64: return V_f64(0.0);
        case T_BOOL: return V_bool(false);
        case T_STR: return V_str("");
        case T_CLASS: return V_null();
        case T_U0: return V_void();
        default: return V_i64(0);
    }
}

static Value coerce_to_type(Value v, TypeInfo ty) {
    switch (ty.kind) {
        case T_U0: return v;
        case T_AUTO: return v;
        case T_STR: {
            if (v.kind == V_STR) return v;
            return v;
        }
        case T_F64:
            if (v.kind == V_I64) return V_f64((double)v.i);
            if (v.kind == V_BOOL) return V_f64(v.b ? 1.0 : 0.0);
            return v;
        case T_BOOL:
            if (v.kind == V_I64) return V_bool(v.i != 0);
            if (v.kind == V_F64) return V_bool(v.f != 0.0);
            return v;
        case T_CLASS:
            return v;
        default: { /* fixed width integer types */
            if (v.kind == V_F64) return V_i64(mask_int((int64_t)v.f, ty.kind));
            if (v.kind == V_BOOL) return V_i64(mask_int(v.b ? 1 : 0, ty.kind));
            if (v.kind == V_I64) return V_i64(mask_int(v.i, ty.kind));
            return v;
        }
    }
}

static bool value_truthy(Value v) {
    switch (v.kind) {
        case V_I64: return v.i != 0;
        case V_F64: return v.f != 0.0;
        case V_BOOL: return v.b;
        case V_STR: return v.s && v.s[0] != '\0';
        case V_NULL: return false;
        case V_OBJ: return true;
        case V_ARRAY: return true;
        default: return false;
    }
}

static double value_as_double(Value v) {
    switch (v.kind) {
        case V_I64: return (double)v.i;
        case V_F64: return v.f;
        case V_BOOL: return v.b ? 1.0 : 0.0;
        default: return 0.0;
    }
}

static int64_t value_as_int(Value v) {
    switch (v.kind) {
        case V_I64: return v.i;
        case V_F64: return (int64_t)v.f;
        case V_BOOL: return v.b ? 1 : 0;
        default: return 0;
    }
}

static char *value_to_display_string(Value v);

/* ======================================================================
 * Control flow signals, propagated up out of exec_stmt without using
 * setjmp: loops watch for BREAK/CONTINUE, function calls watch for
 * RETURN, blocks watch for GOTO and try to resolve it locally first.
 * ==================================================================== */

typedef enum { CF_NONE, CF_BREAK, CF_CONTINUE, CF_RETURN, CF_GOTO } CFKind;

typedef struct {
    CFKind kind;
    Value ret_value;
    char *goto_label;
} CF;

static CF CF_none(void) { CF c = {0}; c.kind = CF_NONE; return c; }

static Value eval_expr(Env *env, Expr *e);
static CF exec_stmt(Env *env, Stmt *s);
static Value call_function(FuncDef *fn, Vec *arg_values, Value *this_val);
static Value construct_object(ClassDef *cls, Vec *arg_values);

/* ---- lvalue assignment helpers ---------------------------------------- */

static void assign_to_expr(Env *env, Expr *target, Value value) {
    if (target->kind == E_IDENT) {
        Binding *b = env_find(env, target->name);
        if (!b) fatal("runtime", target->line, "assignment to undeclared variable %s", target->name);
        b->value = coerce_to_type(value, b->type);
        return;
    }
    if (target->kind == E_MEMBER) {
        Value base = eval_expr(env, target->a);
        if (base.kind != V_OBJ) fatal("runtime", target->line, "member assignment on a non object value");
        int idx = obj_field_index(base.obj, target->name);
        if (idx < 0) fatal("runtime", target->line, "unknown field %s", target->name);
        TypeInfo ty = { .kind = T_AUTO, .class_name = NULL };
        /* Recover the declared field type by walking the class chain so
           fixed width integers still wrap correctly on assignment. */
        for (ClassDef *cur = base.obj->cls; cur; cur = cur->base) {
            for (int i = 0; i < cur->fields->count; i++) {
                FieldDecl *f = vec_get(cur->fields, i);
                if (strcmp(f->name, target->name) == 0) { ty = f->type; goto found; }
            }
        }
        found:
        base.obj->field_values[idx] = coerce_to_type(value, ty);
        return;
    }
    if (target->kind == E_INDEX) {
        Value base = eval_expr(env, target->a);
        if (base.kind != V_ARRAY) fatal("runtime", target->line, "indexing a non array value");
        int64_t idx = value_as_int(eval_expr(env, target->b));
        if (idx < 0 || idx >= base.arr->length) fatal("runtime", target->line, "array index out of bounds");
        base.arr->items[idx] = coerce_to_type(value, base.arr->elem_type);
        return;
    }
    fatal("runtime", target->line, "invalid assignment target");
}

/* ======================================================================
 * Expression evaluation.
 * ==================================================================== */

static Value binary_numeric(const char *op, Value a, Value b, int line) {
    bool use_float = a.kind == V_F64 || b.kind == V_F64;
    if (strcmp(op, "+") == 0) {
        if (a.kind == V_STR || b.kind == V_STR) {
            char *sa = value_to_display_string(a);
            char *sb = value_to_display_string(b);
            char *cat = malloc(strlen(sa) + strlen(sb) + 1);
            strcpy(cat, sa);
            strcat(cat, sb);
            Value v = V_str(cat);
            free(cat);
            return v;
        }
        return use_float ? V_f64(value_as_double(a) + value_as_double(b)) : V_i64(value_as_int(a) + value_as_int(b));
    }
    if (strcmp(op, "-") == 0) return use_float ? V_f64(value_as_double(a) - value_as_double(b)) : V_i64(value_as_int(a) - value_as_int(b));
    if (strcmp(op, "*") == 0) return use_float ? V_f64(value_as_double(a) * value_as_double(b)) : V_i64(value_as_int(a) * value_as_int(b));
    if (strcmp(op, "/") == 0) {
        if (use_float) return V_f64(value_as_double(a) / value_as_double(b));
        int64_t divisor = value_as_int(b);
        if (divisor == 0) fatal("runtime", line, "division by zero");
        return V_i64(value_as_int(a) / divisor);
    }
    if (strcmp(op, "%") == 0) {
        int64_t divisor = value_as_int(b);
        if (divisor == 0) fatal("runtime", line, "division by zero");
        return V_i64(value_as_int(a) % divisor);
    }
    if (strcmp(op, "&") == 0) return V_i64(value_as_int(a) & value_as_int(b));
    if (strcmp(op, "|") == 0) return V_i64(value_as_int(a) | value_as_int(b));
    if (strcmp(op, "^") == 0) return V_i64(value_as_int(a) ^ value_as_int(b));
    if (strcmp(op, "<<") == 0) return V_i64(value_as_int(a) << value_as_int(b));
    if (strcmp(op, ">>") == 0) return V_i64(value_as_int(a) >> value_as_int(b));
    fatal("runtime", line, "unknown binary operator %s", op);
    return V_void();
}

static bool values_equal(Value a, Value b) {
    if (a.kind == V_STR && b.kind == V_STR) return strcmp(a.s, b.s) == 0;
    if (a.kind == V_NULL || b.kind == V_NULL) {
        if (a.kind == V_NULL && b.kind == V_NULL) return true;
        if (a.kind == V_OBJ) return false;
        if (b.kind == V_OBJ) return false;
        return a.kind == b.kind;
    }
    if (a.kind == V_OBJ && b.kind == V_OBJ) return a.obj == b.obj;
    return value_as_double(a) == value_as_double(b);
}

static Value binary_compare(const char *op, Value a, Value b, int line) {
    if (strcmp(op, "==") == 0) return V_bool(values_equal(a, b));
    if (strcmp(op, "!=") == 0) return V_bool(!values_equal(a, b));
    double x = value_as_double(a), y = value_as_double(b);
    if (a.kind == V_STR && b.kind == V_STR) {
        int c = strcmp(a.s, b.s);
        if (strcmp(op, "<") == 0) return V_bool(c < 0);
        if (strcmp(op, "<=") == 0) return V_bool(c <= 0);
        if (strcmp(op, ">") == 0) return V_bool(c > 0);
        if (strcmp(op, ">=") == 0) return V_bool(c >= 0);
    }
    if (strcmp(op, "<") == 0) return V_bool(x < y);
    if (strcmp(op, "<=") == 0) return V_bool(x <= y);
    if (strcmp(op, ">") == 0) return V_bool(x > y);
    if (strcmp(op, ">=") == 0) return V_bool(x >= y);
    fatal("runtime", line, "unknown comparison operator %s", op);
    return V_void();
}

static Vec *eval_args(Env *env, Vec *arg_exprs) {
    Vec *out = vec_new();
    for (int i = 0; i < arg_exprs->count; i++) {
        Value *v = malloc(sizeof(Value));
        *v = eval_expr(env, vec_get(arg_exprs, i));
        vec_push(out, v);
    }
    return out;
}

static Value call_builtin(const char *name, Vec *args, int line, bool *handled);

static Value eval_expr(Env *env, Expr *e) {
    switch (e->kind) {
        case E_INT: return V_i64(e->ival);
        case E_FLOAT: return V_f64(e->fval);
        case E_STRING: return V_str(e->sval);
        case E_CHAR: return V_i64(e->ival);
        case E_BOOL: return V_bool(e->bval);
        case E_NULL: return V_null();
        case E_THIS:
            if (!env->this_val) fatal("runtime", e->line, "this used outside of a method");
            return *env->this_val;

        case E_IDENT: {
            Binding *b = env_find(env, e->name);
            if (!b) fatal("runtime", e->line, "undefined variable %s", e->name);
            return b->value;
        }

        case E_ASSIGN: {
            Value v = eval_expr(env, e->b);
            assign_to_expr(env, e->a, v);
            return v;
        }

        case E_COMPOUND: {
            Value cur = eval_expr(env, e->a);
            Value rhs = eval_expr(env, e->b);
            const char *base_op = strcmp(e->name, "+=") == 0 ? "+" :
                                   strcmp(e->name, "-=") == 0 ? "-" :
                                   strcmp(e->name, "*=") == 0 ? "*" :
                                   strcmp(e->name, "/=") == 0 ? "/" : "%";
            Value result = binary_numeric(base_op, cur, rhs, e->line);
            assign_to_expr(env, e->a, result);
            return result;
        }

        case E_BINARY: {
            Value a = eval_expr(env, e->a);
            Value b = eval_expr(env, e->b);
            if (strcmp(e->name, "==") == 0 || strcmp(e->name, "!=") == 0 || strcmp(e->name, "<") == 0 ||
                strcmp(e->name, "<=") == 0 || strcmp(e->name, ">") == 0 || strcmp(e->name, ">=") == 0)
                return binary_compare(e->name, a, b, e->line);
            return binary_numeric(e->name, a, b, e->line);
        }

        case E_LOGICAL: {
            Value a = eval_expr(env, e->a);
            if (strcmp(e->name, "&&") == 0) {
                if (!value_truthy(a)) return V_bool(false);
                return V_bool(value_truthy(eval_expr(env, e->b)));
            }
            if (value_truthy(a)) return V_bool(true);
            return V_bool(value_truthy(eval_expr(env, e->b)));
        }

        case E_UNARY: {
            Value a = eval_expr(env, e->a);
            if (strcmp(e->name, "-") == 0) return a.kind == V_F64 ? V_f64(-a.f) : V_i64(-value_as_int(a));
            if (strcmp(e->name, "+") == 0) return a;
            if (strcmp(e->name, "!") == 0) return V_bool(!value_truthy(a));
            if (strcmp(e->name, "~") == 0) return V_i64(~value_as_int(a));
            fatal("runtime", e->line, "unknown unary operator %s", e->name);
            return V_void();
        }

        case E_PREINC: case E_PREDEC: case E_POSTINC: case E_POSTDEC: {
            Value old = eval_expr(env, e->a);
            int64_t delta = (e->kind == E_PREINC || e->kind == E_POSTINC) ? 1 : -1;
            Value updated = old.kind == V_F64 ? V_f64(old.f + (double)delta) : V_i64(value_as_int(old) + delta);
            assign_to_expr(env, e->a, updated);
            return (e->kind == E_PREINC || e->kind == E_PREDEC) ? updated : old;
        }

        case E_TERNARY:
            return value_truthy(eval_expr(env, e->a)) ? eval_expr(env, e->b) : eval_expr(env, e->c);

        case E_ARRAYLIT: {
            ArrayVal *arr = calloc(1, sizeof(ArrayVal));
            arr->elem_type = type_simple(T_I64);
            arr->length = e->args->count;
            arr->items = calloc((size_t)arr->length, sizeof(Value));
            for (int i = 0; i < arr->length; i++) arr->items[i] = eval_expr(env, vec_get(e->args, i));
            Value v = {0};
            v.kind = V_ARRAY;
            v.arr = arr;
            return v;
        }

        case E_INDEX: {
            Value base = eval_expr(env, e->a);
            if (base.kind == V_STR) {
                int64_t idx = value_as_int(eval_expr(env, e->b));
                if (idx < 0 || (size_t)idx >= strlen(base.s)) fatal("runtime", e->line, "string index out of bounds");
                return V_i64((unsigned char)base.s[idx]);
            }
            if (base.kind != V_ARRAY) fatal("runtime", e->line, "indexing a non array value");
            int64_t idx = value_as_int(eval_expr(env, e->b));
            if (idx < 0 || idx >= base.arr->length) fatal("runtime", e->line, "array index out of bounds");
            return base.arr->items[idx];
        }

        case E_MEMBER: {
            Value base = eval_expr(env, e->a);
            if (base.kind != V_OBJ) fatal("runtime", e->line, "member access on a non object value");
            int idx = obj_field_index(base.obj, e->name);
            if (idx < 0) fatal("runtime", e->line, "unknown field %s on class %s", e->name, base.obj->cls->name);
            return base.obj->field_values[idx];
        }

        case E_NEWCALL: {
            ClassDef *cls = find_class(e->name);
            if (!cls) fatal("runtime", e->line, "unknown class %s", e->name);
            Vec *args = eval_args(env, e->args);
            return construct_object(cls, args);
        }

        case E_CALL: {
            if (e->a->kind == E_MEMBER) {
                Value base = eval_expr(env, e->a->a);
                if (base.kind != V_OBJ) fatal("runtime", e->line, "method call on a non object value");
                FuncDef *fn = find_method(base.obj->cls, e->a->name);
                if (!fn) fatal("runtime", e->line, "unknown method %s on class %s", e->a->name, base.obj->cls->name);
                Vec *args = eval_args(env, e->args);
                return call_function(fn, args, &base);
            }
            if (e->a->kind == E_IDENT) {
                ClassDef *cls = find_class(e->a->name);
                if (cls) {
                    Vec *args = eval_args(env, e->args);
                    return construct_object(cls, args);
                }
                bool handled = false;
                Vec *args = eval_args(env, e->args);
                Value r = call_builtin(e->a->name, args, e->line, &handled);
                if (handled) return r;
                FuncDef *fn = find_free_func(e->a->name);
                if (!fn) fatal("runtime", e->line, "unknown function %s", e->a->name);
                return call_function(fn, args, env->this_val);
            }
            fatal("runtime", e->line, "expression is not callable");
            return V_void();
        }
    }
    fatal("runtime", e->line, "unhandled expression kind");
    return V_void();
}

/* ======================================================================
 * Statement execution.
 * ==================================================================== */

static Value make_array_value(TypeInfo elem_type, int length) {
    ArrayVal *arr = calloc(1, sizeof(ArrayVal));
    arr->elem_type = elem_type;
    arr->length = length;
    arr->items = calloc((size_t)length, sizeof(Value));
    for (int i = 0; i < length; i++) arr->items[i] = default_value_for_type(elem_type);
    Value v = {0};
    v.kind = V_ARRAY;
    v.arr = arr;
    return v;
}

static void exec_vardecl(Env *env, Stmt *s) {
    for (int i = 0; i < s->decl_items->count; i++) {
        VarDeclItem *item = vec_get(s->decl_items, i);
        Value value;
        if (item->array_lit) {
            int n = item->array_size > 0 ? item->array_size : item->array_lit->count;
            value = make_array_value(s->decl_type, n);
            for (int k = 0; k < item->array_lit->count && k < n; k++)
                value.arr->items[k] = coerce_to_type(eval_expr(env, vec_get(item->array_lit, k)), s->decl_type);
        } else if (item->array_size > 0) {
            value = make_array_value(s->decl_type, item->array_size);
        } else if (item->init) {
            value = coerce_to_type(eval_expr(env, item->init), s->decl_type);
        } else {
            value = default_value_for_type(s->decl_type);
        }
        env_define(env, item->name, s->decl_type, value);
    }
}

/* Looks for a label anywhere in a statement list, used to resolve goto
   targets that jump forward or backward within the same block. */
static int find_label_index(Vec *stmts, const char *label) {
    for (int i = 0; i < stmts->count; i++) {
        Stmt *s = vec_get(stmts, i);
        if (s->kind == S_LABEL && strcmp(s->label, label) == 0) return i;
    }
    return -1;
}

static CF exec_block_stmts(Env *env, Vec *stmts) {
    int i = 0;
    while (i < stmts->count) {
        Stmt *s = vec_get(stmts, i);
        CF cf = exec_stmt(env, s);
        if (cf.kind == CF_GOTO) {
            int target = find_label_index(stmts, cf.goto_label);
            if (target >= 0) { i = target + 1; continue; }
            return cf;
        }
        if (cf.kind != CF_NONE) return cf;
        i++;
    }
    return CF_none();
}

static CF exec_stmt(Env *env, Stmt *s) {
    switch (s->kind) {
        case S_EXPR:
            eval_expr(env, s->expr);
            return CF_none();

        case S_VARDECL:
            exec_vardecl(env, s);
            return CF_none();

        case S_BLOCK: {
            Env *inner = env_new(env);
            return exec_block_stmts(inner, s->stmts);
        }

        case S_IF:
            if (value_truthy(eval_expr(env, s->expr))) return exec_stmt(env, s->a);
            if (s->b) return exec_stmt(env, s->b);
            return CF_none();

        case S_WHILE:
            while (value_truthy(eval_expr(env, s->expr))) {
                CF cf = exec_stmt(env, s->a);
                if (cf.kind == CF_BREAK) break;
                if (cf.kind == CF_CONTINUE) continue;
                if (cf.kind != CF_NONE) return cf;
            }
            return CF_none();

        case S_DOWHILE:
            do {
                CF cf = exec_stmt(env, s->a);
                if (cf.kind == CF_BREAK) break;
                if (cf.kind == CF_CONTINUE) continue;
                if (cf.kind != CF_NONE) return cf;
            } while (value_truthy(eval_expr(env, s->expr)));
            return CF_none();

        case S_FOR: {
            Env *loop_env = env_new(env);
            if (s->for_init) exec_stmt(loop_env, s->for_init);
            while (!s->for_cond || value_truthy(eval_expr(loop_env, s->for_cond))) {
                CF cf = exec_stmt(loop_env, s->for_body);
                if (cf.kind == CF_BREAK) break;
                if (cf.kind != CF_NONE && cf.kind != CF_CONTINUE) return cf;
                if (s->for_post) eval_expr(loop_env, s->for_post);
            }
            return CF_none();
        }

        case S_SWITCH: {
            Value subject = eval_expr(env, s->expr);
            int start = -1;
            for (int i = 0; i < s->cases->count && start < 0; i++) {
                SwitchCase *c = vec_get(s->cases, i);
                if (!c->is_default && values_equal(subject, eval_expr(env, c->value))) start = i;
            }
            if (start < 0) {
                for (int i = 0; i < s->cases->count && start < 0; i++) {
                    SwitchCase *c = vec_get(s->cases, i);
                    if (c->is_default) start = i;
                }
            }
            if (start < 0) return CF_none();
            Env *inner = env_new(env);
            for (int i = start; i < s->cases->count; i++) {
                SwitchCase *c = vec_get(s->cases, i);
                CF cf = exec_block_stmts(inner, c->stmts);
                if (cf.kind == CF_BREAK) return CF_none();
                if (cf.kind != CF_NONE) return cf;
            }
            return CF_none();
        }

        case S_BREAK: { CF cf = {0}; cf.kind = CF_BREAK; return cf; }
        case S_CONTINUE: { CF cf = {0}; cf.kind = CF_CONTINUE; return cf; }

        case S_RETURN: {
            CF cf = {0};
            cf.kind = CF_RETURN;
            cf.ret_value = s->expr ? eval_expr(env, s->expr) : V_void();
            return cf;
        }

        case S_GOTO: {
            CF cf = {0};
            cf.kind = CF_GOTO;
            cf.goto_label = s->label;
            return cf;
        }

        case S_LABEL:
            return CF_none();
    }
    fatal("runtime", s->line, "unhandled statement kind");
    return CF_none();
}

/* ======================================================================
 * Function and constructor calls.
 * ==================================================================== */

static Value call_function(FuncDef *fn, Vec *arg_values, Value *this_val) {
    Env *call_env = env_new(G_GLOBALS);
    call_env->this_val = fn->owner_class ? this_val : NULL;
    int n = fn->params->count;
    for (int i = 0; i < n; i++) {
        Param *param = vec_get(fn->params, i);
        Value arg = i < arg_values->count ? *(Value *)vec_get(arg_values, i) : default_value_for_type(param->type);
        env_define(call_env, param->name, param->type, coerce_to_type(arg, param->type));
    }
    CF cf = exec_block_stmts(call_env, fn->body->stmts);
    if (cf.kind == CF_RETURN) return coerce_to_type(cf.ret_value, fn->ret_type);
    if (cf.kind == CF_GOTO) fatal("runtime", 0, "label %s not found in function %s", cf.goto_label, fn->name);
    if (cf.kind == CF_BREAK) fatal("runtime", 0, "break used outside of a loop or switch in function %s", fn->name);
    if (cf.kind == CF_CONTINUE) fatal("runtime", 0, "continue used outside of a loop in function %s", fn->name);
    return V_void();
}

static Value construct_object(ClassDef *cls, Vec *arg_values) {
    ObjVal *obj = make_instance(cls);
    Value v = {0};
    v.kind = V_OBJ;
    v.obj = obj;
    FuncDef *ctor = find_method(cls, cls->name);
    if (ctor) call_function(ctor, arg_values, &v);
    return v;
}

/* ======================================================================
 * Value to text conversion, shared by Print, ToStr and string
 * concatenation through the plus operator.
 * ==================================================================== */

static char *value_to_display_string(Value v) {
    char buf[512];
    switch (v.kind) {
        case V_I64: snprintf(buf, sizeof(buf), "%lld", (long long)v.i); return own_str(buf);
        case V_F64: snprintf(buf, sizeof(buf), "%g", v.f); return own_str(buf);
        case V_BOOL: return own_str(v.b ? "TRUE" : "FALSE");
        case V_STR: return own_str(v.s);
        case V_NULL: return own_str("NULL");
        case V_VOID: return own_str("");
        case V_OBJ: snprintf(buf, sizeof(buf), "%s@%p", v.obj->cls->name, (void *)v.obj); return own_str(buf);
        case V_ARRAY: snprintf(buf, sizeof(buf), "array[%d]", v.arr->length); return own_str(buf);
    }
    return own_str("");
}

/* ======================================================================
 * Standard library, implemented natively here and callable by name
 * from any HolyC++ source file, including the self hosted compiler.
 * ==================================================================== */

static Value arg_at(Vec *args, int i, int line, const char *fn) {
    if (i >= args->count) fatal("runtime", line, "%s expects more arguments", fn);
    return *(Value *)vec_get(args, i);
}

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} StrBuf;

static void sb_init(StrBuf *sb) { sb->cap = 256; sb->len = 0; sb->data = malloc(sb->cap); sb->data[0] = '\0'; }

static void sb_append(StrBuf *sb, const char *text) {
    size_t add = strlen(text);
    while (sb->len + add + 1 > sb->cap) sb->cap *= 2;
    sb->data = realloc(sb->data, sb->cap);
    memcpy(sb->data + sb->len, text, add + 1);
    sb->len += add;
}

static void sb_append_char(StrBuf *sb, char c) { char s[2] = { c, '\0' }; sb_append(sb, s); }

static void format_into(StrBuf *sb, const char *fmt, Vec *args, int start_index, int line) {
    char num[64];
    int argi = start_index;
    for (size_t i = 0; fmt[i]; i++) {
        if (fmt[i] != '%') { sb_append_char(sb, fmt[i]); continue; }
        i++;
        char spec = fmt[i];
        switch (spec) {
            case 'd': { Value v = arg_at(args, argi++, line, "Print"); snprintf(num, sizeof(num), "%lld", (long long)value_as_int(v)); sb_append(sb, num); break; }
            case 'f': { Value v = arg_at(args, argi++, line, "Print"); snprintf(num, sizeof(num), "%g", value_as_double(v)); sb_append(sb, num); break; }
            case 's': { Value v = arg_at(args, argi++, line, "Print"); char *s = value_to_display_string(v); sb_append(sb, s); free(s); break; }
            case 'c': { Value v = arg_at(args, argi++, line, "Print"); sb_append_char(sb, (char)value_as_int(v)); break; }
            case 'x': { Value v = arg_at(args, argi++, line, "Print"); snprintf(num, sizeof(num), "%llx", (long long)value_as_int(v)); sb_append(sb, num); break; }
            case 'b': { Value v = arg_at(args, argi++, line, "Print"); sb_append(sb, value_truthy(v) ? "TRUE" : "FALSE"); break; }
            case '%': sb_append_char(sb, '%'); break;
            case '\0': sb_append_char(sb, '%'); i--; break;
            default: sb_append_char(sb, '%'); sb_append_char(sb, spec); break;
        }
    }
}

static void print_formatted(const char *fmt, Vec *args, int start_index, int line) {
    StrBuf sb;
    sb_init(&sb);
    format_into(&sb, fmt, args, start_index, line);
    fputs(sb.data, stdout);
    free(sb.data);
}

static Value call_builtin(const char *name, Vec *args, int line, bool *handled) {
    *handled = true;

    if (strcmp(name, "Print") == 0) {
        Value fmt = arg_at(args, 0, line, "Print");
        if (fmt.kind != V_STR) fatal("runtime", line, "Print expects a string format as its first argument");
        print_formatted(fmt.s, args, 1, line);
        return V_void();
    }
    if (strcmp(name, "StrPrint") == 0) {
        Value fmt = arg_at(args, 0, line, "StrPrint");
        StrBuf sb;
        sb_init(&sb);
        format_into(&sb, fmt.s, args, 1, line);
        Value r = V_str(sb.data);
        free(sb.data);
        return r;
    }

    if (strcmp(name, "StrLen") == 0) return V_i64((int64_t)strlen(arg_at(args, 0, line, name).s));
    if (strcmp(name, "StrCat") == 0) {
        Value a = arg_at(args, 0, line, name), b = arg_at(args, 1, line, name);
        char *cat = malloc(strlen(a.s) + strlen(b.s) + 1);
        strcpy(cat, a.s); strcat(cat, b.s);
        Value r = V_str(cat); free(cat); return r;
    }
    if (strcmp(name, "StrCmp") == 0) return V_i64(strcmp(arg_at(args, 0, line, name).s, arg_at(args, 1, line, name).s));
    if (strcmp(name, "StrCopy") == 0) return V_str(arg_at(args, 0, line, name).s);
    if (strcmp(name, "SubStr") == 0) {
        char *s = arg_at(args, 0, line, name).s;
        int64_t start = value_as_int(arg_at(args, 1, line, name));
        int64_t len = value_as_int(arg_at(args, 2, line, name));
        int64_t total = (int64_t)strlen(s);
        if (start < 0) start = 0;
        if (start > total) start = total;
        if (start + len > total) len = total - start;
        if (len < 0) len = 0;
        char *r = own_str_n(s + start, (size_t)len);
        Value v = V_str(r); free(r); return v;
    }
    if (strcmp(name, "ToUpper") == 0) {
        char *s = own_str(arg_at(args, 0, line, name).s);
        for (char *p = s; *p; p++) *p = (char)toupper((unsigned char)*p);
        Value v = V_str(s); free(s); return v;
    }
    if (strcmp(name, "ToLower") == 0) {
        char *s = own_str(arg_at(args, 0, line, name).s);
        for (char *p = s; *p; p++) *p = (char)tolower((unsigned char)*p);
        Value v = V_str(s); free(s); return v;
    }
    if (strcmp(name, "StrFind") == 0) {
        char *hay = arg_at(args, 0, line, name).s;
        char *needle = arg_at(args, 1, line, name).s;
        char *found = strstr(hay, needle);
        return V_i64(found ? (int64_t)(found - hay) : -1);
    }

    if (strcmp(name, "Abs") == 0) {
        Value v = arg_at(args, 0, line, name);
        return v.kind == V_F64 ? V_f64(fabs(v.f)) : V_i64(llabs((long long)value_as_int(v)));
    }
    if (strcmp(name, "Min") == 0) {
        Value a = arg_at(args, 0, line, name), b = arg_at(args, 1, line, name);
        return value_as_double(a) <= value_as_double(b) ? a : b;
    }
    if (strcmp(name, "Max") == 0) {
        Value a = arg_at(args, 0, line, name), b = arg_at(args, 1, line, name);
        return value_as_double(a) >= value_as_double(b) ? a : b;
    }
    if (strcmp(name, "Sqrt") == 0) return V_f64(sqrt(value_as_double(arg_at(args, 0, line, name))));
    if (strcmp(name, "Pow") == 0) return V_f64(pow(value_as_double(arg_at(args, 0, line, name)), value_as_double(arg_at(args, 1, line, name))));
    if (strcmp(name, "Floor") == 0) return V_f64(floor(value_as_double(arg_at(args, 0, line, name))));
    if (strcmp(name, "Ceil") == 0) return V_f64(ceil(value_as_double(arg_at(args, 0, line, name))));
    if (strcmp(name, "Round") == 0) return V_i64((int64_t)llround(value_as_double(arg_at(args, 0, line, name))));

    if (strcmp(name, "ToI64") == 0) {
        Value v = arg_at(args, 0, line, name);
        if (v.kind == V_STR) return V_i64(strtoll(v.s, NULL, 10));
        return V_i64(value_as_int(v));
    }
    if (strcmp(name, "ToF64") == 0) {
        Value v = arg_at(args, 0, line, name);
        if (v.kind == V_STR) return V_f64(strtod(v.s, NULL));
        return V_f64(value_as_double(v));
    }
    if (strcmp(name, "ToStr") == 0) {
        char *s = value_to_display_string(arg_at(args, 0, line, name));
        Value v = V_str(s); free(s); return v;
    }
    if (strcmp(name, "Chr") == 0) {
        char s[2] = { (char)value_as_int(arg_at(args, 0, line, name)), '\0' };
        return V_str(s);
    }

    if (strcmp(name, "Len") == 0) {
        Value v = arg_at(args, 0, line, name);
        if (v.kind == V_ARRAY) return V_i64(v.arr->length);
        if (v.kind == V_STR) return V_i64((int64_t)strlen(v.s));
        fatal("runtime", line, "Len expects an array or a string");
        return V_void();
    }

    if (strcmp(name, "ReadLn") == 0) {
        char buf[65536];
        if (!fgets(buf, sizeof(buf), stdin)) return V_str("");
        size_t n = strlen(buf);
        if (n > 0 && buf[n - 1] == '\n') buf[n - 1] = '\0';
        return V_str(buf);
    }

    if (strcmp(name, "RandI64") == 0) {
        int64_t lo = value_as_int(arg_at(args, 0, line, name));
        int64_t hi = value_as_int(arg_at(args, 1, line, name));
        if (hi < lo) { int64_t t = lo; lo = hi; hi = t; }
        int64_t span = hi - lo + 1;
        return V_i64(lo + (span > 0 ? (rand() % span) : 0));
    }
    if (strcmp(name, "RandF64") == 0) return V_f64((double)rand() / ((double)RAND_MAX + 1.0));

    if (strcmp(name, "Now") == 0) return V_i64((int64_t)time(NULL));

    if (strcmp(name, "Exit") == 0) {
        int64_t code = args->count > 0 ? value_as_int(arg_at(args, 0, line, name)) : 0;
        exit((int)code);
    }

    if (strcmp(name, "ReadFile") == 0) {
        Value path = arg_at(args, 0, line, name);
        FILE *f = fopen(path.s, "rb");
        if (!f) fatal("runtime", line, "ReadFile: cannot open %s", path.s);
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        char *buf = malloc((size_t)size + 1);
        size_t got = fread(buf, 1, (size_t)size, f);
        buf[got] = '\0';
        fclose(f);
        Value r = V_str(buf);
        free(buf);
        return r;
    }
    if (strcmp(name, "WriteFile") == 0) {
        Value path = arg_at(args, 0, line, name);
        Value content = arg_at(args, 1, line, name);
        FILE *f = fopen(path.s, "wb");
        if (!f) fatal("runtime", line, "WriteFile: cannot open %s", path.s);
        fputs(content.s, f);
        fclose(f);
        return V_void();
    }
    if (strcmp(name, "ArgCount") == 0) return V_i64(G_ARGC > 2 ? G_ARGC - 2 : 0);
    if (strcmp(name, "Arg") == 0) {
        int64_t i = value_as_int(arg_at(args, 0, line, name));
        int real_index = 2 + (int)i;
        if (i < 0 || real_index >= G_ARGC) return V_str("");
        return V_str(G_ARGV[real_index]);
    }

    *handled = false;
    return V_void();
}

/* ======================================================================
 * Program entry point.
 * ==================================================================== */

static void init_globals(Program *prog) {
    G_GLOBALS = env_new(NULL);
    for (int i = 0; i < prog->globals->count; i++) {
        Stmt *decl = vec_get(prog->globals, i);
        exec_vardecl(G_GLOBALS, decl);
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: hcrun program.hc++ [args...]\n");
        return 1;
    }
    srand((unsigned)time(NULL));
    G_ARGC = argc;
    G_ARGV = argv;

    StrList visited = {0};
    Vec *tokens = resolve_imports(argv[1], &visited);

    Program *prog = parse_program(tokens);
    G_PROG = prog;
    link_classes();
    init_globals(prog);

    FuncDef *entry = find_free_func("Main");
    if (!entry) {
        fprintf(stderr, "HolyC++ link error: no entry point found, define U0 Main() in %s\n", argv[1]);
        return 1;
    }
    Vec *no_args = vec_new();
    call_function(entry, no_args, NULL);
    return 0;
}
