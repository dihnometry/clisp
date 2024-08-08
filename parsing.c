#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <editline/readline.h>

#include "mpc.h"

typedef struct {
    int type;
    long num;
    int err;
} lval;

// Create an enum for possible lval types
enum { LVAL_NUM, LVAL_ERR };

// Create an enum for possible error types
enum { LERR_DIV_ZERO, LERR_BAD_NUM, LERR_BAD_OP };

lval lval_num(long x) {
    lval v;
    v.type = LVAL_NUM;
    v.num = x;
    return v;
}

lval lval_err(int err) {
    lval v;
    v.type = LVAL_ERR;
    v.err = err;
    return v;
}

void lval_print(lval v) {
    switch (v.type) {
        case LVAL_NUM: printf("%li\n", v.num); break;
        case LVAL_ERR:
            if (v.err == LERR_BAD_NUM) {
                printf("Error: Invalid number.\n");
            }
            if (v.err == LERR_DIV_ZERO) {
                printf("Error: Division by zero.\n");
            }
            if (v.err == LERR_BAD_OP) {
                printf("Error: Invalid operator.\n");
            }
            break;
    }
}

lval eval_op(lval x, char* op, lval y) {
    if (x.type == LVAL_ERR) { return x; }
    if (y.type == LVAL_ERR) { return y; }

    if (strcmp(op, "+") == 0) { return lval_num(x.num + y.num); }
    if (strcmp(op, "-") == 0) { return lval_num(x.num - y.num); }
    if (strcmp(op, "*") == 0) { return lval_num(x.num * y.num); }
    if (strcmp(op, "/") == 0) { 
        return y.num == 0 ? lval_err(LERR_DIV_ZERO) : lval_num(x.num / y.num);
    }
    return lval_err(LERR_BAD_OP);
}

lval eval(mpc_ast_t* t) {
    // If tag is number return it directly
    if (strstr(t->tag, "number")) {
        errno = 0;
        long x = strtol(t->contents, NULL, 10);
        return errno != ERANGE ? lval_num(x) : lval_err(LERR_BAD_NUM);
    }

    // Operator is always the second child
    char* op = t->children[1]->contents;

    // Store the third child in x and return if its error
    lval x = eval(t->children[2]);
    if (x.type == LVAL_ERR) { return x; }

    // Iterate the remaining children and combining
    int i = 3;
    while (strstr(t->children[i]->tag, "expr")) {
        x = eval_op(x, op, eval(t->children[i]));
        i++;
    }

    return x;
}

int main() {
    // Create some parsers
    mpc_parser_t* Number = mpc_new("number");
    mpc_parser_t* Operator = mpc_new("operator");
    mpc_parser_t* Expr = mpc_new("expr");
    mpc_parser_t* Lispy = mpc_new("lispy");

    // Define them with the following language
    mpca_lang(MPCA_LANG_DEFAULT,
        "                                                   \
        number   : /-?[0-9]/ ;                             \
        operator : '+' | '-' | '*' | '/' | '%' ;                  \
        expr     : <number> | '(' <operator> <expr>+ ')' ;  \
        lispy    : /^/ <operator> <expr>+ /$/ ;             \
        ", Number, Operator, Expr, Lispy);


    // Prints version information
    puts("Lispy version 0.0.0.1");
    puts("Exit: Ctrl + C \n");

    while (1) {
        char *input = readline("repl> ");

        add_history(input);

        // Attempts to parse the user input
        mpc_result_t r;
        if (mpc_parse("<stdin>", input, Lispy, &r)) {
            mpc_ast_print(r.output);

            lval result = eval(r.output);
            lval_print(result);

            mpc_ast_delete(r.output);
        } else {
            mpc_err_print(r.error);
            mpc_err_delete(r.error);
        }


        free(input);
    }

    // Free all the parsers
    mpc_cleanup(4, Number, Operator, Expr, Lispy);
    
    return 0;
}
