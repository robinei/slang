#include "rt.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>


#define SAVED_CONS _saved_cons
#define CONS _curr_cons
#define CAR rt_car(CONS)
#define CDR rt_cdr(CONS)
#define STEP()  do { CONS = rt_cdr(CONS); update_loc(state, CONS); } while (0);
#define END_OF_LIST rt_any_is_nil(CONS)
#define LOC state->loc

#define UNEXPECTED(...) \
    do { \
        parse_error(LOC, __VA_ARGS__); \
        return NULL; \
    } while (0);

#define EXPECT(BoolExpr, ...) \
    do { \
        if (!(BoolExpr)) { \
            UNEXPECTED(__VA_ARGS__); \
        } \
    } while (0);

#define BEGIN_PARSE(Cons) \
    struct rt_any CONS = Cons; \
    EXPECT(rt_any_is_cons(Cons), "expected a list") \
    update_loc(state, CONS);

#define EXPECT_PUSH_LIST(...) \
    do { \
        EXPECT(rt_any_is_cons(CAR), __VA_ARGS__) \
        struct rt_any SAVED_CONS = CONS; \
        CONS = rt_car(CONS); \
        update_loc(state, CONS);

#define EXPECT_POP_LIST(...) \
        EXPECT(rt_any_is_nil(CONS), __VA_ARGS__) \
        CONS = SAVED_CONS; \
        update_loc(state, CONS); \
    } while (0);

#define EXPECT_ANY_SYM(VarName, ...) \
    do { \
        EXPECT(rt_any_is_symbol(CAR), __VA_ARGS__) \
        (VarName) = CAR.u.symbol; \
    } while (0);

#define EXPECT_U64(VarName, ...) \
    do { \
        struct rt_any _temp = rt_any_to_unsigned(CAR); \
        EXPECT(rt_any_is_unsigned(_temp), __VA_ARGS__) \
        (VarName) = rt_any_to_u64(_temp); \
    } while (0);

#define MATCH_ANY_SYM(VarName) \
    do { \
        if (rt_any_is_symbol(CAR)) { \
            (VarName) =  CAR.u.symbol; \
        } else { \
            (VarName) =  NULL; \
        } \
    } while (0);

#define MATCHES_SYM(SymName) (rt_any_equals(CAR, rt_symbols.SymName))



struct parse_state {
    struct rt_task *task;
    struct rt_module *mod;
    struct rt_sourceloc loc, loc_after;
};

static void parse_error(struct rt_sourceloc loc, const char *fmt, ...) {
    printf("line %d, col %d: ", loc.line + 1, loc.col + 1);
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
    exit(1);
}

static void update_loc(struct parse_state *state, struct rt_any cons) {
    if (rt_any_is_nil(cons)) {
        return;
    }
    assert(rt_any_is_cons(cons));
    struct rt_module *mod = state->mod;
    if (mod) {
        rt_sourcemap_get(&mod->location_before_car, cons.u.cons, &state->loc);
        rt_sourcemap_get(&mod->location_after_car, cons.u.cons, &state->loc_after);
    }
}

static struct rt_astnode *make_ast(struct parse_state *state, struct rt_sourceloc loc, enum rt_astnode_type node_type) {
    struct rt_astnode *node = calloc(1, sizeof(struct rt_astnode));
    node->result_type = rt_types.any;
    node->node_type = node_type;
    node->sourceloc = loc;
    return node;
}

static struct rt_astnode *make_literal(struct parse_state *state, struct rt_sourceloc loc, struct rt_any value) {
    struct rt_astnode *node = make_ast(state, loc, RT_ASTNODE_LITERAL);
    node->result_type = rt_any_get_type(value);
    node->is_const = true;
    node->const_value = value;
    return node;
}

static struct rt_astnode *make_block(struct parse_state *state, u32 expr_count, struct rt_astnode **exprs) {
    struct rt_astnode **new_exprs = malloc(sizeof(struct rt_astnode *) * expr_count);
    memcpy(new_exprs, exprs, sizeof(struct rt_astnode *) * expr_count);
    struct rt_astnode *block = make_ast(state, LOC, RT_ASTNODE_BLOCK);
    block->u.block.expr_count = expr_count;
    block->u.block.exprs = new_exprs;
    return block;
}


static struct rt_type *parse_type(struct parse_state *state, struct rt_any form) {
    if (rt_any_is_symbol(form)) {
        struct rt_type *type = rt_lookup_simple_type(form);
        if (!type) {
            UNEXPECTED("unrecognized type")
        }
        return type;
    }
    struct rt_symbol *type_sym;

    BEGIN_PARSE(form)
    EXPECT_ANY_SYM(type_sym, "expected type symbol") STEP()
    if (type_sym == rt_symbols.array.u.symbol) {
        struct rt_type *elem_type;
        u64 length = 0;
        EXPECT(elem_type = parse_type(state, CAR), "expected a valid element type") STEP()
        if (!END_OF_LIST) {
            EXPECT_U64(length, "expected optional array length") STEP()
        }
        EXPECT(END_OF_LIST, "expected end of list while parsing array type")
        return rt_gettype_array(elem_type, length);
    } else if (type_sym == rt_symbols.ptr.u.symbol) {
        struct rt_type *target_type;
        EXPECT(target_type = parse_type(state, CAR), "expected a valid target type") STEP()
        EXPECT(END_OF_LIST, "expected end of list while parsing pointer type")
        return rt_gettype_ptr(target_type); 
    } else {
        UNEXPECTED("Unreconized type: %s", type_sym->data)
    }
}

#define MAX_PARAMS 100

static void *parse_param_list(struct parse_state *state, struct rt_any list_head, struct rt_func_param *params, u32 *param_count) {
    BEGIN_PARSE(list_head)
    u32 i = 0;
    while (!END_OF_LIST) {
        struct rt_func_param *param = params + i;
        if (rt_any_is_cons(CAR)) {
            EXPECT_PUSH_LIST("expected argument list for fn form")
            EXPECT(MATCHES_SYM(ascribe), "expected type ascription") STEP()
            EXPECT_ANY_SYM(param->name, "expected a parameter name") STEP()
            EXPECT(param->type = parse_type(state, CAR), "expected a valid parameter type") STEP()
            EXPECT_POP_LIST("expected end of parameter specification") STEP()
        } else {
            param->type = rt_types.any;
            EXPECT_ANY_SYM(param->name, "expected a parameter name") STEP()
        }
    }
    *param_count = i;
    return params; /* just returned to distinguish success from failure (NULL) */
}

static struct rt_astnode *parse_expression(struct parse_state *state, struct rt_any form);

static struct rt_astnode *parse_block(struct parse_state *state, struct rt_any form) {
    BEGIN_PARSE(form)
    struct rt_astnode *exprs[1000];
    u32 expr_count = 0;
    while (!END_OF_LIST) {
        struct rt_astnode *expr = NULL;
        EXPECT(expr = parse_expression(state, CAR), "expected 'else' expression for if form") STEP()
        exprs[expr_count++] = expr;
    }
    return make_block(state, expr_count, exprs);
}

static struct rt_astnode *parse_expression(struct parse_state *state, struct rt_any form) {
    if (!rt_any_is_cons(form)) {
        return make_literal(state, LOC, form);
    }
    BEGIN_PARSE(form)
    struct rt_symbol *head_sym;
    MATCH_ANY_SYM(head_sym)
    if (head_sym) {
        if (head_sym == rt_symbols.fn.u.symbol) {
            struct rt_type *return_type = NULL;
            struct rt_func_param params[MAX_PARAMS];
            u32 param_count = 0;
            struct rt_astnode *body_expr;
            
            STEP()
            EXPECT_PUSH_LIST("expected parameter list for fn form")
            if (MATCHES_SYM(ascribe)) {
                STEP()
                EXPECT(parse_param_list(state, CAR, params, &param_count), "expected valid parameter list") STEP()
                EXPECT(return_type = parse_type(state, CAR), "expected a valid return type") STEP()
            } else {
                EXPECT(parse_param_list(state, CONS, params, &param_count), "expected valid parameter list") STEP()
            }
            EXPECT_POP_LIST("expected end of parameter list") STEP()
            EXPECT(body_expr = parse_block(state, CONS), "expected function body")

            if (!return_type) {
                return_type = rt_types.any;
            }

            struct rt_type *func_type = rt_gettype_boxed(rt_gettype_func(return_type, param_count, params));
            struct rt_func *func_ptr = rt_gc_alloc(state->task, sizeof(struct rt_func));
            func_ptr->body_expr = body_expr;
            struct rt_any func = { func_type, { .func = func_ptr } };
            return make_literal(state, LOC, func);
        }

        if (head_sym == rt_symbols._if.u.symbol) {
            struct rt_astnode *pred_expr, *then_expr, *else_expr;

            STEP()
            EXPECT(pred_expr = parse_expression(state, CAR), "expected predicate expression for if form") STEP()
            EXPECT(then_expr = parse_expression(state, CAR), "expected 'then' expression for if form") STEP()
            EXPECT(else_expr = parse_expression(state, CAR), "expected 'else' expression for if form") STEP()
            
            struct rt_astnode *result = make_ast(state, LOC, RT_ASTNODE_COND);
            result->u.cond.pred_expr = pred_expr;
            result->u.cond.then_expr = then_expr;
            result->u.cond.else_expr = else_expr;
            return result;
        }
    }

    return NULL;
}

struct rt_astnode *rt_parse_module(struct rt_task *task, struct rt_any toplevel_module_list) {
    struct parse_state state_val = {0,};
    state_val.task = task;
    state_val.mod = task->current_module;
    struct parse_state *state = &state_val;

    struct rt_symbol *form_sym;
    struct rt_symbol *name_sym;
    struct rt_astnode *expr;

    struct rt_astnode *exprs[1000];
    u32 expr_count = 0;

    BEGIN_PARSE(toplevel_module_list)
    while (!END_OF_LIST) {
        EXPECT_PUSH_LIST("expecting only list forms at top-level")
        EXPECT_ANY_SYM(form_sym, "expected top-level form symbol") STEP()

        if (form_sym == rt_symbols.def.u.symbol) {
            EXPECT_ANY_SYM(name_sym, "expected name for def form") STEP()
            EXPECT(expr = parse_expression(state, CAR), "expected value for def form") STEP()
            exprs[expr_count++] = expr;
        } else {
            UNEXPECTED("unexpected top-level form: %s", form_sym->data)
        }

        EXPECT_POP_LIST("expected end of def form") STEP()
    }

    return make_block(state, expr_count, exprs);
}
