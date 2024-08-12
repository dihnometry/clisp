#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mpc.h"

#ifdef _WIN32

static char buffer[2048];

char* readline(char* msg) {
    fputs(msg, stdout);
    fgets(buffer, 2048, stdin);
    char* s = malloc(strlen(buffer) + 1);
    strcpy(s, buffer);
    s[strlen(s) - 1] = '\0';
    return s;
}

void add_history(char* unused) {}

#else
#include <editline/readline.h>
#endif

#define LASSERT(args, cond, fmt, ...) \
    if (!(cond)) { \
        lval* err = lval_err(fmt, ##__VA_ARGS__); \
        lval_del(args); \
        return err; \
    }

#define LTYPE_ERR(name, got, exp) \
    "Error: Function '%s' passed incorrect type. Got %s, expected %s.", name, ltype_name(got), ltype_name(exp)

#define LARG_ERR(name, got, exp) \
    "Error: Function '%s' passed incorrect number of arguments. Got %i, expected %i.", name, got, exp

#define LEMP_ERR(name) \
    "Error: Function '%s' passed empty list '{}'", name

struct lval;
struct lenv;
typedef struct lval lval;
typedef struct lenv lenv;

// Create an enum for possible lval types
enum { LVAL_NUM, LVAL_ERR, LVAL_SYM, LVAL_FUN, LVAL_SFUN, LVAL_SEXPR, LVAL_QEXPR };

char* ltype_name(int t) {
    switch (t) {
        case LVAL_SFUN:
        case LVAL_FUN: return "Function"; break;
        case LVAL_NUM: return "Number"; break;
        case LVAL_ERR: return "Error"; break;
        case LVAL_SYM: return "Symbol"; break;
        case LVAL_SEXPR: return "S-Expression"; break;
        case LVAL_QEXPR: return "Q-Expression"; break;
        default: return "Unknown"; break;
    }
}

typedef lval*(*lbuiltin)(lenv*, lval*);

struct lval {
    int type;
    long num;

    char* err;
    char* sym;

    lbuiltin builtin;
    lenv* env;
    lval* formals;
    lval* body;

    int count;
    struct lval** cell;
};

struct lenv {
    lenv* par;
    int count;
    char** syms;
    lval** vals;
};

void lval_print(lval* v);
lval* lval_eval(lenv* e, lval* v);
lenv* lenv_copy(lenv* e);
void lenv_del(lenv* e);


lval* lval_num(long x) {
    lval* v = malloc(sizeof(lval));
    v->type = LVAL_NUM;
    v->num = x;
    return v;
}

lval* lval_err(char* fmt, ...) {
    lval* v = malloc(sizeof(lval));
    v->type = LVAL_ERR;

    va_list va;
    va_start(va, fmt);

    // Allocate 512 bytes of space
    v->err = malloc(512);

    vsnprintf(v->err, 511, fmt, va);
    v->err = realloc(v->err, strlen(v->err) + 1);
    va_end(va);

    return v;
}

lval* lval_sym(char* sym) {
    lval* v = malloc(sizeof(lval));
    v->type = LVAL_SYM;
    v->sym = malloc(strlen(sym) + 1);
    strcpy(v->sym, sym);
    return v;
}

lval* lval_fun(lbuiltin func) {
    lval* v = malloc(sizeof(lval));
    v->type = LVAL_FUN;
    v->builtin = func;
    return v;
}

lval* lval_sfun(lbuiltin func) {
    lval* v = malloc(sizeof(lval));
    v->type = LVAL_SFUN;
    v->builtin = func;
    return v;
}

lval* lval_sexpr(void) {
    lval* v = malloc(sizeof(lval));
    v->type = LVAL_SEXPR;
    v->count = 0;
    v->cell = NULL;
    return v;
}

lval* lval_qexpr(void) {
    lval* v = malloc(sizeof(lval));
    v->type = LVAL_QEXPR;
    v->count = 0;
    v->cell = NULL;
    return v;
}

void lval_del(lval* v) {
    switch (v->type) {
        case LVAL_NUM: break;

        case LVAL_ERR: free(v->err); break;

        case LVAL_FUN:
            if (!v->builtin) {
                lval_del(v->formals);
                lval_del(v->body);
                lenv_del(v->env);
            }
            break;
        case LVAL_SFUN:
        case LVAL_SYM: free(v->sym); break;

        case LVAL_QEXPR:
        case LVAL_SEXPR:
            for (int i = 0; i < v->count; i++) {
                lval_del(v->cell[i]);
            }
            free(v->cell);
            break;
    }

    free(v);
}

lval* lval_add(lval* v, lval* x) {
    v->count++;
    v->cell = realloc(v->cell, sizeof(lval*) * v->count);
    v->cell[v->count - 1] = x;
    return v;
}

lval* lval_read_num(mpc_ast_t* t) {
    errno = 0;
    long x = strtol(t->contents, NULL, 10);
    return errno != ERANGE ? lval_num(x) : lval_err("Invalid number.");
}

lval* lval_read(mpc_ast_t* t) {
    if (strstr(t->tag, "number")) { return lval_read_num(t); }
    if (strstr(t->tag, "symbol")) { return lval_sym(t->contents); }

    lval* x = NULL;
    if (strcmp(t->tag, ">") == 0) { x = lval_sexpr(); }
    if (strstr(t->tag, "sexpr"))  { x = lval_sexpr(); }
    if (strstr(t->tag, "qexpr"))  { x = lval_qexpr(); }

    for (int i = 0; i < t->children_num; i++) {
        if (strcmp(t->children[i]->contents, "(") == 0) { continue; }
        if (strcmp(t->children[i]->contents, ")") == 0) { continue; }
        if (strcmp(t->children[i]->contents, "{") == 0) { continue; }
        if (strcmp(t->children[i]->contents, "}") == 0) { continue; }
        if (strcmp(t->children[i]->tag,  "regex") == 0) { continue; }

        x = lval_add(x, lval_read(t->children[i]));
    }
    return x;
}

lval* lval_copy(lval* v) {
    lval* x = malloc(sizeof(lval));
    x->type = v->type;

    switch (v->type) {
        case LVAL_NUM: x->num = v->num; break;
        case LVAL_SFUN:
        case LVAL_FUN: 
            if (v->builtin) {
                x->builtin = v->builtin;
            } else {
                x->builtin = NULL;
                x->formals = lval_copy(v->formals);
                x->body = lval_copy(v->body);
                x->env = lenv_copy(v->env);
            }
            x->sym = malloc(strlen(v->sym) + 1);
            strcpy(x->sym, v->sym);
            break;

        case LVAL_ERR:
            x->err = malloc(strlen(v->err) + 1);
            strcpy(x->err, v->err);
            break;
        case LVAL_SYM:
            x->sym = malloc(strlen(v->sym) + 1);
            strcpy(x->sym, v->sym);
            break;

        case LVAL_QEXPR:
        case LVAL_SEXPR:
            x->count = v->count;
            x->cell = malloc(sizeof(lval*) * x->count);
            for (int i = 0; i < x->count; i++) {
                x->cell[i] = lval_copy(v->cell[i]);
            }
            break;
    }

    return x;
}

void lval_expr_print(lval* v, char open, char close) {
    putchar(open);
    for (int i = 0; i < v->count; i++) {
        lval_print(v->cell[i]);

        if (i != (v->count - 1)) {
            putchar(' ');
        }
    }
    putchar(close);
}

void lval_print(lval* v) {
    switch (v->type) {
        case LVAL_NUM: printf("%li", v->num); break;
        case LVAL_SYM: printf("%s",  v->sym); break;
        case LVAL_SFUN:
        case LVAL_FUN: 
            if (v->builtin) {
                printf("<builtin function '%s'>", v->sym);
            } else {
                printf("(fn ");
                lval_print(v->formals);
                putchar(' ');
                lval_print(v->body);
                putchar(')');
            }
            break;
        case LVAL_ERR: printf("%s",  v->err); break;
        case LVAL_SEXPR: lval_expr_print(v, '(', ')'); break;
        case LVAL_QEXPR: lval_expr_print(v, '{', '}'); break;
    }
}

void lval_println(lval* v) {
    lval_print(v);
    putchar('\n');
}

lval* lval_pop(lval* v, int i) {
    lval* x = v->cell[i];
    memmove(&v->cell[i], &v->cell[i + 1], sizeof(lval*) * (v->count - i - 1));
    v->count--;
    v->cell = realloc(v->cell, sizeof(lval*) * v->count);
    return x;
}

lval* lval_take(lval* v, int i) {
    lval* x = lval_pop(v, i);
    lval_del(v);
    return x;
}

lval* lval_join(lval* x, lval* y) {
    while (y->count > 0) {
        x = lval_add(x, lval_pop(y, 0));
    }
    lval_del(y);
    return x;
}

lenv* lenv_new(void) {
    lenv* env = malloc(sizeof(lenv*));
    env->par = NULL;
    env->count = 0;
    env->syms = NULL;
    env->vals = NULL;
    return env;
}

void lenv_del(lenv* e) {
    for (int i = 0; i < e->count; i++) {
        free(e->syms[i]);
        lval_del(e->vals[i]);
    }
    free(e->syms);
    free(e->vals);
    free(e);
}

lenv* lenv_copy(lenv* e) {
    lenv* n = malloc(sizeof(lenv*));
    n->par = e->par;
    n->count = e->count;
    n->syms = malloc(sizeof(char*) * n->count);
    n->vals = malloc(sizeof(lval*) * n->count);
    for (int i = 0; i < n->count; i++) {
        n->syms[i] = malloc(strlen(e->syms[i]) + 1);
        strcpy(n->syms[i], e->syms[i]);
        n->vals[i] = lval_copy(e->vals[i]);
    }

    lenv_del(e);
    return n;
}

lval* lenv_get(lenv* e, lval* k) {
    for (int i = 0; i < e->count; i++) {
        if (strcmp(e->syms[i], k->sym) == 0)
            return lval_copy(e->vals[i]);
    }
    if (e->par)
        return lenv_get(e->par, k);

    return lval_err("Unbound symbol '%s'.", k->sym);
}

void lenv_put(lenv* e, lval* k, lval* v) {
    for (int i = 0; i < e->count; i++) {
        if (strcmp(e->syms[i], k->sym) == 0) {
            lval_del(e->vals[i]);
            e->vals[i] = lval_copy(v);
            return;
        }
    }
    
    e->count++;
    e->syms = realloc(e->syms, sizeof(char*) * e->count);
    e->vals = realloc(e->vals, sizeof(lval*) * e->count);

    e->syms[e->count - 1] = malloc(strlen(k->sym) + 1);
    strcpy(e->syms[e->count - 1], k->sym);
    e->vals[e->count - 1] = lval_copy(v);
}

lval* lval_lambda(lval* formals, lval* body) {
    lval* v = malloc(sizeof(lval));
    v->builtin = NULL;
    v->env = lenv_new();
    v->type = LVAL_FUN;
    v->formals = formals;
    v->body = body;
    return v;
}

void lenv_def(lenv* e, lval* k, lval* v) {
    while (e->par) {
        e = e->par;
    }
    lenv_put(e, k, v);
}

lval* builtin_head(lenv* e, lval* a) {
    LASSERT(a, a->count == 1, LARG_ERR("head", a->count, 1));
    LASSERT(a, a->cell[0]->type == LVAL_QEXPR, 
            LTYPE_ERR("head", a->cell[0]->type, LVAL_QEXPR));
    LASSERT(a, a->cell[0]->count != 0, LEMP_ERR("head"));

    lval* v = lval_take(a, 0);
    while (v->count > 1) { lval_del(lval_pop(v, 1)); }
    return v;
}

lval* builtin_tail(lenv* e, lval* a) {
    LASSERT(a, a->count == 1, LARG_ERR("tail", a->count, 1));
    LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
            LTYPE_ERR("tail", a->cell[0]->type, LVAL_QEXPR));
    LASSERT(a, a->cell[0]->count != 0, LEMP_ERR("tail"));

    lval* v = lval_take(a, 0);
    while (v->count > 1) { lval_del(lval_pop(v, 0)); }
    return v;
}

lval* builtin_list(lenv* e, lval* a) {
    a->type = LVAL_QEXPR;
    return a;
}

lval* builtin_eval(lenv* e, lval* a) {
    LASSERT(a, a->count == 1, LARG_ERR("eval", a->count, 1));
    LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
            LTYPE_ERR("eval", a->cell[0]->type, LVAL_QEXPR));

    lval* x = lval_take(a, 0);
    x->type = LVAL_SEXPR;

    return lval_eval(e, x);
}

lval* builtin_join(lenv* e, lval* a) {
    for (int i = 0; i < a->count; i++)
        LASSERT(a, a->cell[i]->type == LVAL_QEXPR, 
                LTYPE_ERR("join", a->cell[i]->type, LVAL_QEXPR));
    
    lval* x = lval_pop(a, 0);
    while (a->count > 0) {
        x = lval_join(x, lval_pop(a, 0));
    }

    lval_del(a);
    return x;
}

lval* builtin_cons(lenv* e, lval* a) {
    LASSERT(a, a->count == 2, LARG_ERR("cons", a->count, 2));
    LASSERT(a, a->cell[1]->type == LVAL_QEXPR,
            LTYPE_ERR("cons", a->cell[1]->type, LVAL_QEXPR));

    lval* x = lval_qexpr();
    x = lval_add(x, lval_pop(a, 0));
    x = lval_join(x, lval_pop(a, 0));
    lval_del(a);
    return x;
}

lval* builtin_len(lenv* e, lval* a) {
    LASSERT(a, a->count == 1, LARG_ERR("len", a->count, 1));
    LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
            LTYPE_ERR("len", a->cell[0]->type, LVAL_QEXPR));

    lval* x = lval_num(lval_pop(a, 0)->count);
    lval_del(a);
    return x;
}

lval* builtin_init(lenv* e, lval* a) {
    LASSERT(a, a->count == 1, LARG_ERR(a->sym, a->count, 1));
    LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
            LTYPE_ERR("init", a->cell[0]->type, LVAL_QEXPR));
    
    lval* x = lval_pop(a, 0);
    if (x->count != 0)
        lval_pop(x, x->count - 1);

    lval_del(a);
    return x;
}

lval* builtin_op(lenv* e, lval* v, char* op) {
    for (int i = 0; i < v->count; i++) {
        if (v->cell[i]->type != LVAL_NUM) {
            lval* err = lval_err(LTYPE_ERR(op, v->cell[i]->type, LVAL_NUM));
            lval_del(v);
            return err;
        }
    }

    lval* x = lval_pop(v, 0);
    if (strcmp(op, "-") == 0 && v->count == 0)
        x->num = -x->num;

    while (v->count > 0) {
        lval* y = lval_pop(v, 0);
        if (strcmp(op, "+") == 0) { x->num += y->num; }
        if (strcmp(op, "*") == 0) { x->num *= y->num; }
        if (strcmp(op, "-") == 0) { x->num -= y->num; }
        if (strcmp(op, "/") == 0) {
            if (y->num == 0)
                return lval_err("Error: Division by zero.");

            x->num /= y->num;
        }

        lval_del(y);
    }

    lval_del(v);
    return x;
}

lval* builtin_add(lenv* e, lval* v) {
    return builtin_op(e, v, "+");
}

lval* builtin_sub(lenv* e, lval* v) {
    return builtin_op(e, v, "-");
}

lval* builtin_mul(lenv* e, lval* v) {
    return builtin_op(e, v, "*");
}

lval* builtin_div(lenv* e, lval* v) {
    return builtin_op(e, v, "/");
}

lval* builtin_var(lenv* e, lval* a, char* func) {
    LASSERT(a, a->cell[0]->type == LVAL_QEXPR, 
            LTYPE_ERR(func, a->cell[0]->type, LVAL_QEXPR));

    lval* syms = a->cell[0];
    for (int i = 0; i < syms->count; i++)
        LASSERT(a, syms->cell[i]->type == LVAL_SYM, 
                "Function '%s' cannot define non-symbol.", func);

    LASSERT(a, syms->count == a->count - 1, LARG_ERR(func, a->count - 1, syms->count));

    for (int i = 0; i < syms->count; i++) {
        if (strcmp(func, "def") == 0) {
            lenv_def(e, syms->cell[i], a->cell[i + 1]);
        } 
        if (strcmp(func, "let") == 0) {
            lenv_put(e, syms->cell[i], a->cell[i + 1]);
        }
    }

    lval_del(a);
    return lval_sexpr();
}

lval* builtin_def(lenv* e, lval* a) {
    return builtin_var(e, a, "def");
}

lval* builtin_put(lenv* e, lval* a) {
    return builtin_var(e, a, "let");
}

lval* lval_call(lenv* e, lval* f, lval* a) {
    if (f->builtin)
        return f->builtin(e, a);

    int given = a->count;
    int total = f->formals->count;

    while (a->count) {
        if (f->formals->count == 0) {
            lval_del(a);
            return lval_err("Function passed too many arguments. Got %i, expected %i.", given, total);
        }

        lval* sym = lval_pop(f->formals, 0);
        lval* val = lval_pop(a, 0);

        lenv_put(f->env, sym, val);
        lval_del(sym);
        lval_del(val);
    }

    lval_del(a);

    if (f->formals->count == 0) {
        f->env->par = e;

        return builtin_eval(f->env, lval_add(lval_sexpr(), lval_copy(f->body)));
    } else {
        return lval_copy(f);
    }
}

lval* builtin_print_env(lenv* e, lval* a) {
    LASSERT(a, a->count == 0, LARG_ERR("print-env", a->count, 0));

    for (int i = 0; i < e->count; i++) {
        printf("%s: ", e->syms[i]);
        lval_println(e->vals[i]);
    }

    lval_del(a);
    return lval_sexpr();
}

lval* builtin_exit(lenv* e, lval* a) {
    LASSERT(a, a->count == 0, LARG_ERR("exit", a->count, 0));

    exit(0);
    return lval_sexpr();
}

lval* builtin_lambda(lenv* e, lval* a) {
    LASSERT(a, a->count == 2, LARG_ERR("fn", a->count, 2));
    LASSERT(a, a->cell[0]->type, LTYPE_ERR("fn", a->cell[0]->type, LVAL_QEXPR));
    LASSERT(a, a->cell[1]->type, LTYPE_ERR("fn", a->cell[1]->type, LVAL_QEXPR));

    for (int i = 0; i < a->cell[0]->count; i++) {
        LASSERT(a, a->cell[0]->cell[i]->type == LVAL_SYM, LTYPE_ERR("fn", a->cell[0]->cell[i]->type, LVAL_SYM));
    }

    lval* formals = lval_pop(a, 0);
    lval* body = lval_pop(a, 0);
    lval_del(a);

    return lval_lambda(formals, body);
}

void lenv_add_builtin(lenv* e, char* name, lbuiltin func) {
    lval* k = lval_sym(name);
    lval* v = lval_fun(func);
    v->sym = malloc(strlen(name) + 1);
    strcpy(v->sym, name);
    lenv_def(e, k, v);
    lval_del(k);
    lval_del(v);
}

void lenv_add_sbuiltin(lenv* e, char* name, lbuiltin func) {
    lval* k = lval_sym(name);
    lval* v = lval_sfun(func);
    v->sym = malloc(strlen(name) + 1);
    strcpy(v->sym, name);
    lenv_put(e, k, v);
    lval_del(k);
    lval_del(v);
}

void lenv_add_builtins(lenv* e) {
    // List functions
    lenv_add_builtin(e, "list", builtin_list);
    lenv_add_builtin(e, "head", builtin_head);
    lenv_add_builtin(e, "tail", builtin_tail);
    lenv_add_builtin(e, "join", builtin_join);
    lenv_add_builtin(e, "eval", builtin_eval);
    lenv_add_builtin(e, "cons", builtin_cons);
    lenv_add_builtin(e, "len" , builtin_len );
    lenv_add_builtin(e, "init", builtin_init);

    // Mathematical functions
    lenv_add_builtin(e, "+", builtin_add);
    lenv_add_builtin(e, "-", builtin_sub);
    lenv_add_builtin(e, "*", builtin_mul);
    lenv_add_builtin(e, "/", builtin_div);

    // Variable functions
    lenv_add_builtin(e, "def", builtin_def);
    lenv_add_builtin(e, "let", builtin_put);
    lenv_add_builtin(e, "fn", builtin_lambda);

    // Special functions (takes no arguments)
    lenv_add_sbuiltin(e, "print-env", builtin_print_env);
    lenv_add_sbuiltin(e, "exit", builtin_exit);
}

lval* lval_eval_sexpr(lenv* e, lval* v) {
    // Evaluate children
    for (int i = 0; i < v->count; i++) {
        v->cell[i] = lval_eval(e, v->cell[i]);
    }
    
    // Error checking
    for (int i = 0; i < v->count; i++) {
        if (v->cell[i]->type == LVAL_ERR)
            return lval_take(v, i);
    }

    // Empty expression
    if (v->count == 0)
        return v;
    // Single expression
    if (v->count == 1 && v->cell[0]->type != LVAL_SFUN)
        return lval_take(v, 0);

    lval* f = lval_pop(v, 0);
    if (f->type != LVAL_FUN && f->type != LVAL_SFUN) {
        lval_del(f);
        lval_del(v);
        return lval_err("First element is not a function.");
    }

    lval* result = lval_call(e, f, v);
    lval_del(f);
    return result;
}

lval* lval_eval(lenv* e, lval* v) {
    if (v->type == LVAL_SYM) {
        lval* x = lenv_get(e, v);
        lval_del(v);
        return x;
    }
    if (v->type == LVAL_SEXPR)
        return lval_eval_sexpr(e, v);

    return v;
}

int main() {
    // Create some parsers
    mpc_parser_t* Number = mpc_new("number");
    mpc_parser_t* Symbol = mpc_new("symbol");
    mpc_parser_t* Sexpr = mpc_new("sexpr");
    mpc_parser_t* Qexpr = mpc_new("qexpr");
    mpc_parser_t* Expr = mpc_new("expr");
    mpc_parser_t* Clisp = mpc_new("clisp");

    // Define them with the following language
    mpca_lang(MPCA_LANG_DEFAULT,
        "                                                    \
        number   : /-?[0-9]+/ ;                              \
        symbol   : /[a-zA-Z0-9_+\\-*\\/\\\\=<>!&]+/ ;        \
        sexpr    : '(' <expr>* ')' ;                         \
        qexpr    : '{' <expr>* '}' ;                         \
        expr     : <number> | <symbol> | <sexpr> | <qexpr> ; \
        clisp    : /^/ <expr>* /$/ ;                         \
        ", Number, Symbol, Sexpr, Qexpr, Expr, Clisp);


    puts("Clisp version 0.0.0.1");
    puts("Exit: Ctrl + C \n");

    lenv* env = lenv_new();
    lenv_add_builtins(env);

    while (1) {
        char *input = readline("clisp> ");

        add_history(input);

        // Attempts to parse the user input
        mpc_result_t r;
        if (mpc_parse("<stdin>", input, Clisp, &r)) {
            lval* result = lval_eval(env, lval_read(r.output));
            lval_println(result);
            lval_del(result);

            mpc_ast_delete(r.output);
        } else {
            mpc_err_print(r.error);
            mpc_err_delete(r.error);
        }

        free(input);
    }

    // Free the environment
    lenv_del(env);
    // Free all the parsers
    mpc_cleanup(6, Number, Symbol, Sexpr, Qexpr, Expr, Clisp);
    
    return 0;
}
