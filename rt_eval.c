#include "rt.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include "hashtable.h"

IMPL_HASH_TABLE(rt_symbolmap, struct rt_symbol *, struct rt_astnode *, hashutil_ptr_hash, hashutil_ptr_equals)


struct eval_state {
    struct rt_task *task;
    struct rt_module *mod;
};

static void eval_error(struct rt_astnode *node, const char *fmt, ...) {
    printf("line %d, col %d: ", node->sourceloc.line + 1, node->sourceloc.col + 1);
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    printf("\n");
    exit(1);
}

static struct rt_any rt_ast_eval_expr(struct eval_state *state, struct rt_astnode *node) {
    struct rt_any result = rt_nil;
    switch (node->node_type) {
    case RT_ASTNODE_LITERAL:
        result = node->const_value;
        break;
    case RT_ASTNODE_SCOPE:
    case RT_ASTNODE_BLOCK:
        for (u32 i = 0; i < node->u.block.expr_count; ++i) {
            result = rt_ast_eval_expr(state, node->u.block.exprs[i]);
        }
        break;
    case RT_ASTNODE_GET:
        break;
    case RT_ASTNODE_SET:
        break;
    case RT_ASTNODE_COND: {
        struct rt_any pred_result = rt_ast_eval_expr(state, node->u.cond.pred_expr);
        if (!rt_any_is_bool(pred_result)) {
            eval_error(node, "boolean value required for conditional predicate");
            break;
        }
        bool val = rt_any_to_bool(pred_result);
        if (!val) {
            result = rt_ast_eval_expr(state, node->u.cond.else_expr);
        } else {
            result = rt_ast_eval_expr(state, node->u.cond.then_expr);
        }
        break;
    }
    case RT_ASTNODE_LOOP: {
        for (;;) {
            struct rt_any pred_result = rt_ast_eval_expr(state, node->u.loop.pred_expr);
            if (!rt_any_is_bool(pred_result)) {
                eval_error(node, "boolean value required for loop predicate");
                break;
            }
            bool val = rt_any_to_bool(pred_result);
            if (!val) {
                break;
            }
            result = rt_ast_eval_expr(state, node->u.loop.body_expr);
        }
    }
    case RT_ASTNODE_CALL: {
        struct rt_any func_result = rt_ast_eval_expr(state, node->u.call.func_expr);
        if (!rt_any_is_func(func_result)) {
            eval_error(node, "expected a callable value");
            break;
        }

    }
    }
    return result;
}

static struct rt_any rt_ast_eval_toplevel(struct eval_state *state, struct rt_astnode *node) {
    struct rt_any result = rt_nil;
    switch (node->node_type) {
    case RT_ASTNODE_BLOCK:
        for (u32 i = 0; i < node->u.block.expr_count; ++i) {
            result = rt_ast_eval_toplevel(state, node->u.block.exprs[i]);
        }
        break;
    case RT_ASTNODE_GET: {
        struct rt_astnode *temp;
        if (!rt_symbolmap_get(&state->mod->symbolmap, node->u.get.name, &temp)) {
            eval_error(node, "no toplevel item with name '%s' found", node->u.get.name);
        }
        result = temp->const_value;
        break;
    }
    case RT_ASTNODE_SET: {
        struct rt_astnode *dummy;
        if (rt_symbolmap_get(&state->mod->symbolmap, node->u.set.name, &dummy)) {
            eval_error(node, "redefinition of already defined toplevel name: %s", node->u.set.name);
            break;
        }
        result = rt_ast_eval_expr(state, node->u.set.expr);
        //rt_sourcemap_put(&state->globals, node->u.set.name, result);
        break;
    }
    default:
        eval_error(node, "unexpected toplevel form");
        break;
    }
    return result;
}
